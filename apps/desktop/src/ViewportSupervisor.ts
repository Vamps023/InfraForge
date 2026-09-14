import { spawn, type ChildProcessByStdio } from 'node:child_process'
import readline from 'node:readline'
import type { Readable, Writable } from 'node:stream'
import { adaptTerrainScene, emptyViewportScene } from './ViewportSceneAdapter.js'
import { resolveNativeExecutable } from './NativeExecutableResolver.js'

export interface ViewportPlacement {
  screenX: number
  screenY: number
  width: number
  height: number
  dpiScale: number
}
export interface ViewportStatus {
  state: 'unavailable' | 'starting' | 'ready' | 'suspended' | 'recreating' | 'device_lost' | 'failed' | 'stopped'
  detail: string
  validation?: boolean
  gpu?: string
  vulkan?: string
}

const READY_PREFIX = 'INFRAFORGE_VIEWPORT_READY '
const STATUS_PREFIX = 'INFRAFORGE_VIEWPORT_STATUS '
const DEFAULT_STARTUP_TIMEOUT_MS = 10_000
const SHUTDOWN_GRACE_MS = 2_000

type ViewportChild = ChildProcessByStdio<Writable, Readable, Readable>

export interface ViewportSupervisorOptions {
  // Lifecycle-test seam: spawns this real command prefix (e.g. node with a
  // fixture script) in place of the configured viewport executable.
  // Production leaves it unset.
  commandOverride?: { executable: string; leadingArgs?: string[] }
  // Lifecycle-test seam: readiness timeout. Production uses 10 seconds.
  startupTimeoutMs?: number
}

function isStatusState(value: unknown): value is ViewportStatus['state'] {
  return (
    value === 'unavailable' ||
    value === 'starting' ||
    value === 'ready' ||
    value === 'suspended' ||
    value === 'recreating' ||
    value === 'device_lost' ||
    value === 'failed' ||
    value === 'stopped'
  )
}

// Supervises the native viewport process: launches it parented into the
// shell window, forwards placement/visibility control commands, and exposes
// renderer status records. The supervisor owns no business logic; it is the
// OS/process integration the desktop shell is responsible for.
export class ViewportSupervisor {
  private child: ViewportChild | null = null
  private starting = false
  private statusListener: ((status: ViewportStatus) => void) | null = null
  private readonly options: ViewportSupervisorOptions
  private readonly startupTimeoutMs: number
  private status: ViewportStatus = {
    state: 'unavailable',
    detail: 'Native viewport has not been started.',
  }

  constructor(options: ViewportSupervisorOptions = {}) {
    this.options = options
    this.startupTimeoutMs = options.startupTimeoutMs ?? DEFAULT_STARTUP_TIMEOUT_MS
  }

  setStatusListener(listener: (status: ViewportStatus) => void): void {
    this.statusListener = listener
  }

  snapshot(): ViewportStatus {
    return this.status
  }

  async start(parentWindowHandle: Buffer, initialPlacement: ViewportPlacement): Promise<void> {
    if (this.child || this.starting) {
      return
    }
    this.starting = true
    try {
      const viewportPath = await resolveNativeExecutable('viewport')
      if (!viewportPath) {
        this.publish({
          state: 'unavailable',
          detail: 'Native viewport was not found in packaged resources or the source build directory.',
        })
        return
      }

      this.publish({ state: 'starting', detail: 'Starting native viewport surface…' })
      this.child = this.spawnChild(viewportPath, parentWindowHandle, initialPlacement)
      await this.awaitReadiness(this.child)
    } catch (error) {
      // Every failure path — unresolvable path, spawn error, readiness
      // timeout, exit before readiness — lands here: publish the explicit
      // failure, kill any child we spawned, and reset `starting` in the
      // finally below so the supervisor stays startable.
      this.publish({
        state: 'failed',
        detail: error instanceof Error ? error.message : String(error),
      })
      this.killChild()
    } finally {
      this.starting = false
    }
  }

  place(placement: ViewportPlacement): void {
    this.sendControl({
      type: 'place',
      screenX: placement.screenX,
      screenY: placement.screenY,
      width: placement.width,
      height: placement.height,
      dpiScale: placement.dpiScale,
    })
  }

  // Forwards the engine-derived terrain scene projection to the viewport.
  // BLOCKER 6: Uses an explicit adapter to map protobuf camelCase fields
  // (absolutePath, minEasting, etc.) to the native viewport's expected
  // field names (path, minE, etc.). BigInt values are serialized as
  // decimal strings so 64-bit values survive JSON transport losslessly.
  sendScene(scene: Record<string, unknown>): void {
    const adapted = adaptTerrainScene(scene)
    if (adapted) {
      this.sendControl(adapted as unknown as Record<string, unknown>)
    }
  }

  // Blocker 10: Forwards the engine-derived road scene projection to the
  // viewport as a separate narrow scene channel. Road scenes are NOT
  // terrain scenes and must not be rejected by the terrain-only validation
  // in the terrain scene path. The road scene is forwarded as a dedicated
  // control message so the native viewport's RoadPass can update
  // incrementally without a full terrain scene rebuild.
  sendRoadScene(scene: Record<string, unknown>): void {
    // Validate the road scene payload is a bounded plain object with roads.
    if (typeof scene !== 'object' || scene === null || Array.isArray(scene)) {
      return
    }
    const record = scene as Record<string, unknown>
    if (!Array.isArray(record.meshes)) {
      return
    }
    // Forward as a typed road scene control message. The native viewport
    // parses the "roads" field and the "roadRevision" field.
    const roadControl: Record<string, unknown> = {
      type: 'roadScene',
      roads: record.meshes,
    }
    // Preserve revision as a string for lossless uint64 transport.
    if (typeof record.revision === 'bigint') {
      roadControl.roadRevision = (record.revision as bigint).toString()
    } else if (typeof record.revision === 'number') {
      roadControl.roadRevision = String(record.revision)
    } else if (typeof record.revision === 'string') {
      roadControl.roadRevision = record.revision
    }
    this.sendControl(roadControl)
  }

  sendEmptyScene(): void {
    this.sendControl(emptyViewportScene() as unknown as Record<string, unknown>)
  }

  sendCameraAction(action: 'focus-terrain' | 'frame-all' | 'perspective' | 'top', datasetUuid?: string): void {
    this.sendControl({ type: 'camera', action, ...(datasetUuid ? { datasetUuid } : {}) })
  }

  setVisible(visible: boolean): void {
    this.sendControl({ type: 'visibility', visible })
  }

  stop(): void {
    const child = this.child
    if (!child) {
      return
    }
    this.child = null
    try {
      child.stdin.write(`${JSON.stringify({ type: 'shutdown' })}\n`)
      child.stdin.end()
    } catch {
      // stdin may already be gone; the kill path below still applies.
    }
    const killTimer = setTimeout(() => {
      if (!child.killed) {
        child.kill()
      }
    }, SHUTDOWN_GRACE_MS)
    child.once('exit', () => clearTimeout(killTimer))
  }

  private spawnChild(
    viewportPath: string,
    parentWindowHandle: Buffer,
    initialPlacement: ViewportPlacement,
  ): ViewportChild {
    // The spawn arguments deliberately carry no visibility state: the
    // native surface is always created hidden, so no startup timing
    // (overlay/minimize changing while the process launches) can produce a
    // visible window. Visibility only begins through the runtime control
    // path after the shell applies its visibility policy at readiness.
    const args = [
        '--parent-window', readWindowHandleHex(parentWindowHandle),
        '--screen-x', String(Math.round(initialPlacement.screenX)),
        '--screen-y', String(Math.round(initialPlacement.screenY)),
        '--width', String(Math.max(1, Math.round(initialPlacement.width))),
        '--height', String(Math.max(1, Math.round(initialPlacement.height))),
        '--dpi-scale', String(Math.max(5, Math.round(initialPlacement.dpiScale * 100))),
    ]
    if (process.env.INFRAFORGE_VIEWPORT_VALIDATE === '1') {
      // Development validation mode: KHONOS validation findings are logged
      // and treated as failures by the lifecycle verification process.
      args.push('--validate')
    }
    const executable = this.options.commandOverride?.executable ?? viewportPath
    const leadingArgs = this.options.commandOverride?.leadingArgs ?? []
    return spawn(
      executable,
      [...leadingArgs, ...args],
      { stdio: ['pipe', 'pipe', 'pipe'], windowsHide: true },
    )
  }

  private awaitReadiness(child: ViewportChild): Promise<void> {
    return new Promise<void>((resolve, reject) => {
      const lines = readline.createInterface({ input: child.stdout })
      let settled = false
      const timer = setTimeout(() => {
        if (!settled) {
          settled = true
          reject(new Error('Native viewport did not report readiness before the startup timeout.'))
        }
      }, this.startupTimeoutMs)

      lines.on('line', (line) => {
        if (line.startsWith(READY_PREFIX)) {
          if (!settled) {
            settled = true
            clearTimeout(timer)
            resolve()
          }
          return
        }
        if (line.startsWith(STATUS_PREFIX)) {
          this.handleStatusLine(line.slice(STATUS_PREFIX.length))
        }
      })

      child.stderr?.on('data', (chunk: Buffer) => {
        process.stderr.write(`[viewport] ${chunk.toString('utf8')}`)
      })

      child.once('exit', (code, signal) => {
        if (!settled) {
          settled = true
          clearTimeout(timer)
          reject(new Error(`Native viewport exited before readiness (code=${code ?? 'null'}, signal=${signal ?? 'none'}).`))
          return
        }
        clearTimeout(timer)
        if (this.child === child) {
          this.child = null
          this.publish({
            state: 'stopped',
            detail: `Native viewport exited (code=${code ?? 'null'}).`,
          })
        }
      })
      child.once('error', (error) => {
        if (!settled) {
          settled = true
          clearTimeout(timer)
          reject(error)
        }
      })
    })
  }

  private sendControl(command: Record<string, unknown>): void {
    const child = this.child
    if (!child || !child.stdin.writable) {
      return
    }
    try {
      // BigInt-safe JSON serialization: 64-bit integer values (datasetRevision,
      // chunkX, chunkY, revision) are serialized as decimal strings so they
      // survive JSON transport losslessly beyond Number.MAX_SAFE_INTEGER.
      child.stdin.write(`${JSON.stringify(command, (_key, value) => typeof value === 'bigint' ? value.toString() : value)}\n`)
    } catch (error) {
      this.publish({
        state: 'failed',
        detail: `Failed to send viewport control command: ${error instanceof Error ? error.message : String(error)}`,
      })
    }
  }

  private handleStatusLine(payload: string): void {
    try {
      const parsed = JSON.parse(payload) as Record<string, unknown>
      if (!isStatusState(parsed.state) || typeof parsed.detail !== 'string') {
        throw new Error('malformed viewport status record')
      }
      const status: ViewportStatus = {
        state: parsed.state,
        detail: parsed.detail,
        validation: parsed.validation === true,
      }
      if (typeof parsed.gpu === 'string') {
        status.gpu = parsed.gpu
      }
      if (typeof parsed.vulkan === 'string') {
        status.vulkan = parsed.vulkan
      }
      this.publish(status)
    } catch (error) {
      process.stderr.write(`[viewport] malformed status record: ${error instanceof Error ? error.message : String(error)}\n`)
    }
  }

  private publish(status: ViewportStatus): void {
    this.status = status
    this.statusListener?.(status)
  }

  private killChild(): void {
    const child = this.child
    this.child = null
    if (child && !child.killed) {
      child.kill()
    }
  }
}

function readWindowHandleHex(handle: Buffer): string {
  return handle.readBigUInt64LE(0).toString(16)
}
