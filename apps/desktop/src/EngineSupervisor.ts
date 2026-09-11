import { randomBytes } from 'node:crypto'
import { spawn, type ChildProcessByStdio } from 'node:child_process'
import { stat } from 'node:fs/promises'
import net from 'node:net'
import path from 'node:path'
import readline from 'node:readline'
import type { Readable } from 'node:stream'

export interface EngineConnectionInfo {
  host: '127.0.0.1'
  port: number
  sessionToken: string
  protocolMajor: number
  protocolMinor: number
  engineVersion: string
}

export type EngineBootstrap =
  | { state: 'ready'; connection: EngineConnectionInfo }
  | { state: 'unavailable' | 'failed'; message: string }

type EngineChildProcess = ChildProcessByStdio<null, Readable, Readable>

const READY_PREFIX = 'INFRAFORGE_ENGINE_READY '
const STARTUP_TIMEOUT_MS = 10_000
const MAX_BIND_ATTEMPTS = 4

async function chooseLoopbackPortCandidate(): Promise<number> {
  return await new Promise((resolve, reject) => {
    const server = net.createServer()
    server.once('error', reject)
    server.listen({ host: '127.0.0.1', port: 0, exclusive: true }, () => {
      const address = server.address()
      if (!address || typeof address === 'string') {
        server.close()
        reject(new Error('Failed to choose a loopback TCP port candidate'))
        return
      }
      const port = address.port
      server.close((error) => error ? reject(error) : resolve(port))
    })
  })
}

async function resolveConfiguredEnginePath(): Promise<string | null> {
  const configured = process.env.INFRAFORGE_ENGINE_PATH
  if (!configured) {
    return null
  }

  const resolved = path.resolve(configured)
  const info = await stat(resolved)
  if (!info.isFile()) {
    throw new Error(`INFRAFORGE_ENGINE_PATH is not a file: ${resolved}`)
  }
  return resolved
}

export class EngineSupervisor {
  private child: EngineChildProcess | null = null
  private bootstrap: EngineBootstrap = {
    state: 'unavailable',
    message: 'Native engine has not been started.',
  }

  async start(): Promise<void> {
    let enginePath: string | null
    try {
      enginePath = await resolveConfiguredEnginePath()
    } catch (error) {
      this.bootstrap = { state: 'failed', message: error instanceof Error ? error.message : String(error) }
      return
    }

    if (!enginePath) {
      this.bootstrap = {
        state: 'unavailable',
        message: 'Set INFRAFORGE_ENGINE_PATH to the built infraforge-engine executable for desktop development.',
      }
      return
    }

    const sessionToken = randomBytes(32).toString('hex')
    let lastError: Error | null = null

    for (let attempt = 1; attempt <= MAX_BIND_ATTEMPTS; attempt += 1) {
      const port = await chooseLoopbackPortCandidate()
      const child = spawn(enginePath, [
        '--serve',
        '--host', '127.0.0.1',
        '--port', String(port),
        '--session-token', sessionToken,
      ], {
        stdio: ['ignore', 'pipe', 'pipe'],
        windowsHide: true,
      })

      this.child = child

      try {
        const ready = await this.waitForReady(child, port)
        this.bootstrap = {
          state: 'ready',
          connection: {
            host: '127.0.0.1',
            port,
            sessionToken,
            protocolMajor: ready.protocolMajor,
            protocolMinor: ready.protocolMinor,
            engineVersion: ready.engineVersion,
          },
        }
        return
      } catch (error) {
        lastError = error instanceof Error ? error : new Error(String(error))
        if (!child.killed) {
          child.kill()
        }
        this.child = null
      }
    }

    this.bootstrap = {
      state: 'failed',
      message: `Native engine failed to bind/start after ${MAX_BIND_ATTEMPTS} attempts. ${lastError?.message ?? ''}`.trim(),
    }
  }

  snapshot(): EngineBootstrap {
    return this.bootstrap
  }

  stop(): void {
    const child = this.child
    this.child = null
    if (child && !child.killed) {
      child.kill()
    }
  }

  private async waitForReady(child: EngineChildProcess, expectedPort: number): Promise<{
    protocolMajor: number
    protocolMinor: number
    engineVersion: string
  }> {
    return await new Promise((resolve, reject) => {
      const stderrChunks: string[] = []
      const lines = readline.createInterface({ input: child.stdout })
      let timeout: NodeJS.Timeout | undefined

      const cleanup = () => {
        if (timeout !== undefined) {
          clearTimeout(timeout)
          timeout = undefined
        }
        lines.close()
        child.stderr.removeListener('data', onStderr)
        child.removeListener('exit', onExit)
      }

      const onStderr = (chunk: Buffer) => {
        stderrChunks.push(chunk.toString('utf8'))
        if (stderrChunks.join('').length > 8_192) {
          stderrChunks.shift()
        }
      }

      const onExit = (code: number | null, signal: NodeJS.Signals | null) => {
        cleanup()
        const detail = stderrChunks.join('').trim()
        reject(new Error(`Native engine exited before readiness (code=${code ?? 'null'}, signal=${signal ?? 'none'}).${detail ? ` ${detail}` : ''}`))
      }

      timeout = setTimeout(() => {
        cleanup()
        reject(new Error('Native engine did not report readiness before the startup timeout.'))
      }, STARTUP_TIMEOUT_MS)

      child.stderr.on('data', onStderr)
      child.once('exit', onExit)

      lines.on('line', (line) => {
        if (!line.startsWith(READY_PREFIX)) {
          return
        }

        try {
          const parsed = JSON.parse(line.slice(READY_PREFIX.length)) as Record<string, unknown>
          if (parsed.host !== '127.0.0.1' || parsed.port !== expectedPort) {
            throw new Error('Native engine readiness endpoint does not match the requested loopback endpoint.')
          }
          if (!Number.isInteger(parsed.protocolMajor) || !Number.isInteger(parsed.protocolMinor) || typeof parsed.engineVersion !== 'string') {
            throw new Error('Native engine readiness record is malformed.')
          }

          cleanup()
          resolve({
            protocolMajor: parsed.protocolMajor as number,
            protocolMinor: parsed.protocolMinor as number,
            engineVersion: parsed.engineVersion,
          })
        } catch (error) {
          cleanup()
          reject(error)
        }
      })
    })
  }
}
