import { create, fromBinary, toBinary } from '@bufbuild/protobuf'
import {
  ClientHelloSchema,
  FrameSchema,
  ProtocolVersionSchema,
} from '@infraforge/protocol'

export type EngineSessionStatus =
  | { state: 'starting'; message: string }
  | { state: 'ready'; message: string }
  | { state: 'unavailable' | 'failed' | 'disconnected'; message: string }

const CLIENT_NAME = 'infraforge-frontend'
const CLIENT_VERSION = '0.1.0'
const HELLO_TIMEOUT_MS = 5_000

export async function connectEngineSession(
  onStatus: (status: EngineSessionStatus) => void,
): Promise<() => void> {
  const desktop = window.infraforgeDesktop
  if (!desktop) {
    onStatus({ state: 'unavailable', message: 'Desktop bridge is unavailable in this renderer.' })
    return () => undefined
  }

  onStatus({ state: 'starting', message: 'Starting native engine session…' })
  const bootstrap = await desktop.getEngineBootstrap()
  if (bootstrap.state !== 'ready') {
    onStatus({ state: bootstrap.state, message: bootstrap.message })
    return () => undefined
  }

  const { connection } = bootstrap
  const socket = new WebSocket(`ws://${connection.host}:${connection.port}/`)
  socket.binaryType = 'arraybuffer'

  let authenticated = false
  let disposed = false
  let helloTimer: number | undefined

  socket.addEventListener('open', () => {
    const protocol = create(ProtocolVersionSchema, {
      major: connection.protocolMajor,
      minor: connection.protocolMinor,
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
        onStatus({ state: 'failed', message: 'Native engine did not complete protocol authentication.' })
      }
    }, HELLO_TIMEOUT_MS)
  })

  socket.addEventListener('message', (event) => {
    if (!(event.data instanceof ArrayBuffer)) {
      socket.close(1003, 'binary protobuf frames required')
      onStatus({ state: 'failed', message: 'Native engine sent a non-binary protocol frame.' })
      return
    }

    try {
      const frame = fromBinary(FrameSchema, new Uint8Array(event.data))
      if (frame.payload.case === 'serverHello') {
        const hello = frame.payload.value
        if (!hello.protocol || hello.protocol.major !== connection.protocolMajor || hello.protocol.minor > connection.protocolMinor) {
          socket.close(1002, 'protocol mismatch')
          onStatus({ state: 'failed', message: 'Native engine protocol version is incompatible with the frontend.' })
          return
        }
        authenticated = true
        if (helloTimer !== undefined) {
          window.clearTimeout(helloTimer)
          helloTimer = undefined
        }
        onStatus({ state: 'ready', message: `Engine ${hello.engineVersion} connected` })
        return
      }

      if (frame.payload.case === 'error') {
        onStatus({ state: 'failed', message: frame.payload.value.message || 'Native engine returned a protocol error.' })
      }
    } catch (error) {
      socket.close(1002, 'malformed protobuf frame')
      onStatus({ state: 'failed', message: error instanceof Error ? error.message : 'Failed to decode native engine frame.' })
    }
  })

  socket.addEventListener('error', () => {
    if (!disposed) {
      onStatus({ state: 'failed', message: 'Native engine WebSocket connection failed.' })
    }
  })

  socket.addEventListener('close', () => {
    if (helloTimer !== undefined) {
      window.clearTimeout(helloTimer)
    }
    if (!disposed && authenticated) {
      onStatus({ state: 'disconnected', message: 'Native engine connection closed.' })
    }
  })

  return () => {
    disposed = true
    if (helloTimer !== undefined) {
      window.clearTimeout(helloTimer)
    }
    if (socket.readyState === WebSocket.OPEN || socket.readyState === WebSocket.CONNECTING) {
      socket.close(1000, 'renderer shutdown')
    }
  }
}
