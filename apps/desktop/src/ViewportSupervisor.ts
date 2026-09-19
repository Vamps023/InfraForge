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
const INTERACTION_PREFIX = 'INFRAFORGE_VIEWPORT_INTERACTION '
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

export interface RoadPreviewPoint { easting: number; northing: number }
const ROAD_PREVIEW_HALF_WIDTH = 5

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
  private interactionListener: ((interaction: ViewportInteraction) => void) | null = null
  private latestRoadScene: Record<string, unknown> | null = null
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

  setInteractionListener(listener: (interaction: ViewportInteraction) => void): void {
    this.interactionListener = listener
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

  // Blocker 1: Forwards the engine-derived road scene projection to the
  // viewport through the single coherent "scene" control message type.
  // The native ControlProtocol's "scene" handler parses BOTH terrain
  // (parseTerrainScene) and road (parseRoadScene) data from the same JSON.
  // Road-only updates send a "scene" with a "roads" field and no "tiles"
  // field, so the terrain scene is empty and only the road scene updates.
  // This avoids a competing "roadScene" control type that the native
  // protocol does not recognize.
  sendRoadScene(scene: Record<string, unknown>): void {
    // Validate the road scene payload is a bounded plain object with roads.
    if (typeof scene !== 'object' || scene === null || Array.isArray(scene)) {
      return
    }
    const record = scene as Record<string, unknown>
    if (!Array.isArray(record.meshes)) {
      return
    }
    this.latestRoadScene = record
    this.sendRoadSceneWithPreview(record, [])
  }

  sendRoadPreview(points: RoadPreviewPoint[]): void {
    if (this.latestRoadScene) this.sendRoadSceneWithPreview(this.latestRoadScene, points)
  }

  private sendRoadSceneWithPreview(record: Record<string, unknown>, points: RoadPreviewPoint[]): void {
    const meshes = [...(record.meshes as unknown[])]
    if (points.length >= 2) {
      const originEasting = Number(record.originEasting ?? 0)
      const originNorthing = Number(record.originNorthing ?? 0)
      const vertices: Array<Record<string, number>> = []
      const indices: number[] = []
      for (let i = 0; i < points.length; i += 1) {
        const previous = points[Math.max(0, i - 1)]!
        const next = points[Math.min(points.length - 1, i + 1)]!
        const point = points[i]!
        const dx = next.easting - previous.easting
        const dy = next.northing - previous.northing
        const length = Math.hypot(dx, dy)
        if (!Number.isFinite(length) || length === 0) return
        const offsetX = -dy / length * ROAD_PREVIEW_HALF_WIDTH
        const offsetY = dx / length * ROAD_PREVIEW_HALF_WIDTH
        vertices.push({ x: point.easting + offsetX - originEasting,
          y: point.northing + offsetY - originNorthing, z: 0, nx: 0, ny: 0, nz: 1 })
        vertices.push({ x: point.easting - offsetX - originEasting,
          y: point.northing - offsetY - originNorthing, z: 0, nx: 0, ny: 0, nz: 1 })
        if (i > 0) {
          const base = i * 2
          indices.push(base - 2, base - 1, base, base - 1, base + 1, base)
        }
      }
      meshes.push({ roadId: '__authoring_preview__', chunkX: '0', chunkY: '0', vertices, indices })
    }
    // Forward as a "scene" type with "roads" field so the native viewport's
    // single scene parser handles both terrain and road data coherently.
    const sceneControl: Record<string, unknown> = {
      type: 'scene',
      roads: {
        originEasting: record.originEasting,
        originNorthing: record.originNorthing,
        originHeight: record.originHeight,
        roads: meshes,
        roadRevision: typeof record.revision === 'bigint'
          ? record.revision.toString() : String(record.revision ?? 0),
      },
    }
    this.sendControl(sceneControl)
  }

  sendEmptyScene(): void {
    this.latestRoadScene = null
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
          return
        }
        if (line.startsWith(INTERACTION_PREFIX)) {
          this.handleInteractionLine(line.slice(INTERACTION_PREFIX.length))
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

  private handleInteractionLine(payload: string): void {
    try {
      const parsed = JSON.parse(payload) as Record<string, unknown>
      if (parsed.kind !== 'pointer-move' && parsed.kind !== 'pointer-leave' && parsed.kind !== 'primary-click') {
        return
      }
      const kind = parsed.kind
      if (kind === 'pointer-leave') {
        this.interactionListener?.({ kind: 'pointer-leave', easting: 0, northing: 0, height: 0 })
        return
      }
      if (typeof parsed.easting !== 'number' ||
          typeof parsed.northing !== 'number' || typeof parsed.height !== 'number' ||
          !Number.isFinite(parsed.easting) || !Number.isFinite(parsed.northing) ||
          !Number.isFinite(parsed.height)) return
      this.interactionListener?.({ kind, easting: parsed.easting,
        northing: parsed.northing, height: parsed.height,
        roadId: typeof parsed.roadId === 'string' ? parsed.roadId : undefined })
    } catch {
      // Malformed child output is ignored; it never becomes an editor action.
    }
  }

  private killChild(): void {
    const child = this.child
    this.child = null
    if (child && !child.killed) {
      child.kill()
    }
  }
}

export interface ViewportInteraction {
  kind: 'primary-click' | 'pointer-move' | 'pointer-leave'
  easting: number
  northing: number
  height: number
  roadId?: string
}

function readWindowHandleHex(handle: Buffer): string {
  return handle.readBigUInt64LE(0).toString(16)
}
