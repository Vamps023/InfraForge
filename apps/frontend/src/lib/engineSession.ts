import { create, fromBinary, toBinary } from '@bufbuild/protobuf'
import {
  ClientHelloSchema,
  CommandEnvelopeSchema,
  type CommandEnvelope,
  EventEnvelopeSchema,
  FrameSchema,
  ProtocolVersionSchema,
  ResultEnvelopeSchema,
  type CommandErrorCode,
  type EventEnvelope,
  type ResultEnvelope,
} from '@infraforge/protocol'

export type EngineSessionStatus =
  | { state: 'starting'; message: string }
  | { state: 'ready'; message: string }
  | { state: 'unavailable' | 'failed' | 'disconnected'; message: string }

export class EngineCommandError extends Error {
  constructor(
    readonly code: CommandErrorCode | 'timeout',
    message: string,
  ) {
    super(message)
  }
}

export interface EngineClient {
  sendCommand: (command: CommandEnvelope['command']) => Promise<ResultEnvelope['outcome']>
  onEvent: (listener: (event: EventEnvelope) => void) => () => void
}

export interface EngineSession {
  client: EngineClient
  dispose: () => void
}

export type EngineConnectionResult =
  | { state: 'ready'; session: EngineSession }
  | { state: 'unavailable' | 'failed'; message: string }

const CLIENT_NAME = 'infraforge-frontend'
const CLIENT_VERSION = '0.2.0'
const HELLO_TIMEOUT_MS = 5_000
const COMMAND_TIMEOUT_MS = 30_000

// The protocol version this frontend was generated against. The frontend
// declares its own version instead of echoing the engine's advertised one;
// the engine decides compatibility (major must match, minor must not exceed
// the engine's).
const FRONTEND_PROTOCOL_MAJOR = 1
const FRONTEND_PROTOCOL_MINOR = 9

interface PendingCommand {
  resolve: (outcome: ResultEnvelope['outcome']) => void
  reject: (error: Error) => void
  timer: number
}

// Establishes the authenticated loopback WebSocket session and returns a
// typed command/event client. The frontend owns presentation state only;
// canonical project state stays in the native engine (ADR-0009).
export async function connectEngineSession(
  onStatus: (status: EngineSessionStatus) => void,
): Promise<EngineConnectionResult> {
  const desktop = window.infraforgeDesktop
  if (!desktop) {
    const message = 'Desktop bridge is unavailable in this renderer.'
    onStatus({ state: 'unavailable', message })
    return { state: 'unavailable', message }
  }

  onStatus({ state: 'starting', message: 'Starting native engine session…' })
  const bootstrap = await desktop.getEngineBootstrap()
  if (bootstrap.state !== 'ready') {
    onStatus({ state: bootstrap.state, message: bootstrap.message })
    return { state: bootstrap.state, message: bootstrap.message }
  }

  const { connection } = bootstrap
  const socket = new WebSocket(`ws://${connection.host}:${connection.port}/`)
  socket.binaryType = 'arraybuffer'

  const pendingCommands = new Map<string, PendingCommand>()
  const eventListeners = new Set<(event: EventEnvelope) => void>()

  let authenticated = false
  let disposed = false
  let helloTimer: number | undefined
  let resolveAuthentication: (() => void) | undefined
  let rejectAuthentication: ((error: Error) => void) | undefined

  const authenticationPromise = new Promise<void>((resolve, reject) => {
    resolveAuthentication = resolve
    rejectAuthentication = reject
  })

  const rejectPendingCommands = (error: Error) => {
    for (const pending of pendingCommands.values()) {
      window.clearTimeout(pending.timer)
      pending.reject(error)
    }
    pendingCommands.clear()
  }

  socket.addEventListener('open', () => {
    const protocol = create(ProtocolVersionSchema, {
      major: FRONTEND_PROTOCOL_MAJOR,
      minor: FRONTEND_PROTOCOL_MINOR,
    })
    const hello = create(ClientHelloSchema, {
      protocol,
      sessionToken: connection.sessionToken,
      clientName: CLIENT_NAME,
      clientVersion: CLIENT_VERSION,
    })
    const frame = create(FrameSchema, {
      requestId: crypto.randomUUID(),
      payload: { case: 'clientHello', value: hello },
    })

    socket.send(toBinary(FrameSchema, frame))
    helloTimer = window.setTimeout(() => {
      if (!authenticated) {
        socket.close(1000, 'server hello timeout')
        const message = 'Native engine did not complete protocol authentication.'
        rejectAuthentication?.(new Error(message))
        onStatus({ state: 'failed', message })
      }
    }, HELLO_TIMEOUT_MS)
  })

  socket.addEventListener('message', (event) => {
    if (!(event.data instanceof ArrayBuffer)) {
      socket.close(1003, 'binary protobuf frames required')
      onStatus({ state: 'failed', message: 'Native engine sent a non-binary protocol frame.' })
      return
    }

    let frame: ReturnType<typeof decodeFrame>
    try {
      frame = decodeFrame(new Uint8Array(event.data))
    } catch (error) {
      socket.close(1002, 'malformed protobuf frame')
      const message = error instanceof Error ? error.message : 'Failed to decode native engine frame.'
      onStatus({ state: 'failed', message })
      rejectAuthentication?.(new Error(message))
      return
    }

    if (frame.payload.case === 'serverHello') {
      const hello = frame.payload.value
      if (
        !hello.protocol ||
        hello.protocol.major !== FRONTEND_PROTOCOL_MAJOR ||
        hello.protocol.minor < FRONTEND_PROTOCOL_MINOR
      ) {
        socket.close(1002, 'protocol mismatch')
        const message = 'Native engine protocol version is incompatible with the frontend.'
        onStatus({ state: 'failed', message })
        rejectAuthentication?.(new Error(message))
        return
      }
      authenticated = true
      if (helloTimer !== undefined) {
        window.clearTimeout(helloTimer)
        helloTimer = undefined
      }
      onStatus({ state: 'ready', message: `Engine ${hello.engineVersion} connected` })
      resolveAuthentication?.()
      return
    }

    if (frame.payload.case === 'result') {
      const pending = pendingCommands.get(frame.requestId)
      if (pending) {
        pendingCommands.delete(frame.requestId)
        window.clearTimeout(pending.timer)
        pending.resolve(frame.payload.value.outcome)
      }
      return
    }

    if (frame.payload.case === 'event') {
      for (const listener of eventListeners) {
        listener(frame.payload.value)
      }
      return
    }

    if (frame.payload.case === 'error') {
      // Transport-level error: no pending command can complete normally.
      rejectPendingCommands(
        new EngineCommandError('timeout', frame.payload.value.message || 'Native engine returned a protocol error.'),
      )
    }
  })

  socket.addEventListener('error', () => {
    if (!disposed) {
      const message = 'Native engine WebSocket connection failed.'
      rejectPendingCommands(new EngineCommandError('timeout', message))
      onStatus({ state: 'failed', message })
      rejectAuthentication?.(new Error(message))
    }
  })

  socket.addEventListener('close', () => {
    if (helloTimer !== undefined) {
      window.clearTimeout(helloTimer)
    }
    rejectPendingCommands(new EngineCommandError('timeout', 'Native engine connection closed while a command was pending.'))
    if (!disposed && authenticated) {
      onStatus({ state: 'disconnected', message: 'Native engine connection closed.' })
    }
    rejectAuthentication?.(new Error('Native engine session closed during authentication.'))
  })

  const dispose = () => {
    disposed = true
    if (helloTimer !== undefined) {
      window.clearTimeout(helloTimer)
    }
    rejectPendingCommands(new EngineCommandError('timeout', 'Engine session disposed while a command was pending.'))
    if (socket.readyState === WebSocket.OPEN || socket.readyState === WebSocket.CONNECTING) {
      socket.close(1000, 'renderer shutdown')
    }
  }

  try {
    await authenticationPromise
  } catch (error) {
    return { state: 'failed', message: error instanceof Error ? error.message : String(error) }
  }

  const client: EngineClient = {
    sendCommand: (command) =>
      new Promise<ResultEnvelope['outcome']>((resolve, reject) => {
        if (socket.readyState !== WebSocket.OPEN || !authenticated) {
          reject(new EngineCommandError('timeout', 'Native engine session is not connected.'))
          return
        }

        const requestId = crypto.randomUUID()
        const timer = window.setTimeout(() => {
          pendingCommands.delete(requestId)
          reject(new EngineCommandError('timeout', `Command timed out after ${COMMAND_TIMEOUT_MS / 1000}s.`))
        }, COMMAND_TIMEOUT_MS)

        pendingCommands.set(requestId, { resolve, reject, timer })

        const envelope = create(CommandEnvelopeSchema, { command })
        const frame = create(FrameSchema, {
          requestId,
          payload: { case: 'command', value: envelope },
        })
        socket.send(toBinary(FrameSchema, frame))
      }),
    onEvent: (listener) => {
      eventListeners.add(listener)
      return () => {
        eventListeners.delete(listener)
      }
    },
  }

  return { state: 'ready', session: { client, dispose } }
}

function decodeFrame(data: Uint8Array) {
  return fromBinary(FrameSchema, data)
}

// Exported for consumers that build event envelopes from the same schema.
export const engineEventSchema = EventEnvelopeSchema
