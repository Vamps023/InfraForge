import { spawn, type ChildProcessByStdio } from 'node:child_process'
import path from 'node:path'
import readline from 'node:readline'
import type { Readable, Writable } from 'node:stream'
import { stat } from 'node:fs/promises'

export interface ViewportPlacement {
  screenX: number
  screenY: number
  width: number
  height: number
  dpiScale: number
}

export interface ViewportStatus {
  state: 'unavailable' | 'starting' | 'ready' | 'suspended' | 'recreating' | 'failed' | 'stopped'
  detail: string
  validation?: boolean
  gpu?: string
  vulkan?: string
}

const READY_PREFIX = 'INFRAFORGE_VIEWPORT_READY '
const STATUS_PREFIX = 'INFRAFORGE_VIEWPORT_STATUS '
const STARTUP_TIMEOUT_MS = 10_000
const SHUTDOWN_GRACE_MS = 2_000

type ViewportChild = ChildProcessByStdio<Writable, Readable, Readable>

function isStatusState(value: unknown): value is ViewportStatus['state'] {
  return (
    value === 'unavailable' ||
    value === 'starting' ||
    value === 'ready' ||
    value === 'suspended' ||
    value === 'recreating' ||
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
  private status: ViewportStatus = {
    state: 'unavailable',
    detail: 'Native viewport has not been started.',
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

    const viewportPath = await resolveConfiguredViewportPath()
    if (!viewportPath) {
      this.publish({
        state: 'unavailable',
        detail: 'Set INFRAFORGE_VIEWPORT_PATH to the built infraforge-viewport executable for desktop development.',
      })
      return
    }

    this.publish({ state: 'starting', detail: 'Starting native viewport surface…' })

    const child = spawn(
      viewportPath,
      [
        '--parent-window', readWindowHandleHex(parentWindowHandle),
        '--screen-x', String(initialPlacement.screenX),
        '--screen-y', String(initialPlacement.screenY),
        '--width', String(initialPlacement.width),
        '--height', String(initialPlacement.height),
        '--dpi-scale', String(Math.round(initialPlacement.dpiScale * 100)),
      ],
      { stdio: ['pipe', 'pipe', 'pipe'], windowsHide: true },
    )
    this.child = child

    const ready = new Promise<void>((resolve, reject) => {
      const lines = readline.createInterface({ input: child.stdout })
      let settled = false
      const timer = setTimeout(() => {
        if (!settled) {
          settled = true
          reject(new Error('Native viewport did not report readiness before the startup timeout.'))
        }
      }, STARTUP_TIMEOUT_MS)

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

    try {
      await ready
    } catch (error) {
      this.child = null
      this.publish({
        state: 'failed',
        detail: error instanceof Error ? error.message : String(error),
      })
      this.killChild()
      return
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

  private sendControl(command: Record<string, unknown>): void {
    const child = this.child
    if (!child || !child.stdin.writable) {
      return
    }
    try {
      child.stdin.write(`${JSON.stringify(command)}\n`)
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

async function resolveConfiguredViewportPath(): Promise<string | null> {
  const configured = process.env.INFRAFORGE_VIEWPORT_PATH
  if (!configured) {
    return null
  }
  const resolved = path.resolve(configured)
  const info = await stat(resolved)
  if (!info.isFile()) {
    throw new Error(`INFRAFORGE_VIEWPORT_PATH is not a file: ${resolved}`)
  }
  return resolved
}
