// End-to-end engine verification: launches the real infraforge-engine
// process, connects over authenticated loopback WebSocket, and exercises the
// project lifecycle commands through the versioned protobuf protocol.
// Reports pass/fail honestly; any failure exits non-zero.

import { spawn } from 'node:child_process'
import { randomBytes } from 'node:crypto'
import { mkdir, mkdtemp, rm } from 'node:fs/promises'
import net from 'node:net'
import { tmpdir } from 'node:os'
import path from 'node:path'
import readline from 'node:readline'
import { create, fromBinary, toBinary } from '@bufbuild/protobuf'
import {
  AxisConvention,
  ClientHelloSchema,
  CloseProjectCommandSchema,
  CommandEnvelopeSchema,
  CreateProjectCommandSchema,
  FrameSchema,
  GeoreferenceConfigSchema,
  GetProjectSummaryCommandSchema,
  OpenProjectCommandSchema,
  ProtocolVersionSchema,
  SaveProjectCommandSchema,
  TrafficSide,
  type CommandEnvelope,
  type EventEnvelope,
  type ResultEnvelope,
} from '@infraforge/protocol'

type ProjectCommand = NonNullable<CommandEnvelope['command']>

interface ReadyRecord {
  host: string
  port: number
  protocolMajor: number
  protocolMinor: number
  engineVersion: string
}

const READY_PREFIX = 'INFRAFORGE_ENGINE_READY '
const STARTUP_TIMEOUT_MS = 15_000
const FRAME_TIMEOUT_MS = 10_000

function fail(message: string): never {
  console.error(`ENGINE-SMOKE-FAIL ${message}`)
  process.exit(1)
}

async function chooseLoopbackPort(): Promise<number> {
  return await new Promise((resolve, reject) => {
    const probe = net.createServer()
    probe.once('error', reject)
    probe.listen({ host: '127.0.0.1', port: 0, exclusive: true }, () => {
      const address = probe.address()
      if (!address || typeof address === 'string') {
        probe.close()
        reject(new Error('failed to probe loopback port'))
        return
      }
      const port = address.port
      probe.close((error) => (error ? reject(error) : resolve(port)))
    })
  })
}

function waitForReadyRecord(child: ReturnType<typeof spawn>, expectedPort: number): Promise<ReadyRecord> {
  return new Promise((resolve, reject) => {
    const lines = readline.createInterface({ input: child.stdout! })
    const timer = setTimeout(() => {
      reject(new Error('engine did not report readiness in time'))
    }, STARTUP_TIMEOUT_MS)
    lines.on('line', (line) => {
      if (!line.startsWith(READY_PREFIX)) {
        console.log(`engine: ${line}`)
        return
      }
      try {
        const parsed = JSON.parse(line.slice(READY_PREFIX.length)) as ReadyRecord
        if (parsed.host !== '127.0.0.1' || parsed.port !== expectedPort) {
          throw new Error('readiness record does not match requested endpoint')
        }
        clearTimeout(timer)
        resolve(parsed)
      } catch (error) {
        clearTimeout(timer)
        reject(error instanceof Error ? error : new Error(String(error)))
      }
    })
    child.once('exit', (code) => {
      clearTimeout(timer)
      reject(new Error(`engine exited before readiness (code=${code})`))
    })
  })
}

async function main() {
  const enginePath = process.env.INFRAFORGE_ENGINE_PATH
  if (!enginePath) {
    fail('INFRAFORGE_ENGINE_PATH must point at the built infraforge-engine executable')
  }

  const scratch = await mkdtemp(path.join(tmpdir(), 'infraforge-smoke-'))
  const projectsParent = path.join(scratch, 'projects')
  await mkdir(projectsParent, { recursive: true })

  const port = await chooseLoopbackPort()
  const sessionToken = randomBytes(32).toString('hex')

  const child = spawn(enginePath, [
    '--serve',
    '--host', '127.0.0.1',
    '--port', String(port),
    '--session-token', sessionToken,
  ], { stdio: ['ignore', 'pipe', 'pipe'], windowsHide: true })
  child.stderr!.on('data', (chunk: Buffer) => process.stderr.write(chunk))

  let ready: ReadyRecord
  try {
    ready = await waitForReadyRecord(child, port)
  } catch (error) {
    child.kill()
    fail(`startup failed: ${error instanceof Error ? error.message : String(error)}`)
  }

  const socket = new WebSocket(`ws://127.0.0.1:${ready.port}/`)
  socket.binaryType = 'arraybuffer'

  const pending = new Map<string, { resolve: (outcome: ResultEnvelope['outcome']) => void; timer: NodeJS.Timeout }>()
  const receivedEvents: EventEnvelope[] = []
  let authenticated = false

  const framePromise = new Promise<void>((resolve, reject) => {
    socket.addEventListener('open', () => {
      const hello = create(FrameSchema, {
        requestId: 'smoke-hello',
        payload: {
          case: 'clientHello',
          value: create(ClientHelloSchema, {
            protocol: create(ProtocolVersionSchema, {
              major: ready.protocolMajor,
              minor: ready.protocolMinor,
            }),
            sessionToken,
            clientName: 'infraforge-engine-smoke',
            clientVersion: '0.2.0',
          }),
        },
      })
      socket.send(toBinary(FrameSchema, hello))
    })
    socket.addEventListener('message', (event) => {
      if (!(event.data instanceof ArrayBuffer)) {
        reject(new Error('engine sent a non-binary frame'))
        return
      }
      const frame = fromBinary(FrameSchema, new Uint8Array(event.data))
      if (frame.payload.case === 'serverHello') {
        authenticated = true
        resolve()
        return
      }
      if (frame.payload.case === 'result') {
        const entry = pending.get(frame.requestId)
        if (entry) {
          pending.delete(frame.requestId)
          clearTimeout(entry.timer)
          entry.resolve(frame.payload.value.outcome)
        }
        return
      }
      if (frame.payload.case === 'event') {
        receivedEvents.push(frame.payload.value)
      }
    })
    socket.addEventListener('error', () => reject(new Error('engine socket error')))
    socket.addEventListener('close', () => {
      if (!authenticated) reject(new Error('engine closed before authentication'))
    })
  })

  const closeTimer = setTimeout(() => fail('authentication timed out'), FRAME_TIMEOUT_MS)
  try {
    await framePromise
  } catch (error) {
    fail(`authentication failed: ${error instanceof Error ? error.message : String(error)}`)
  }
  clearTimeout(closeTimer)

  const sendCommand = async (requestId: string, command: ProjectCommand): Promise<ResultEnvelope['outcome']> => {
    const envelope = create(CommandEnvelopeSchema, { command })
    const frame = create(FrameSchema, {
      requestId,
      payload: { case: 'command', value: envelope },
    })
    const result = await new Promise<ResultEnvelope['outcome']>((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error(`command ${requestId} timed out`)), FRAME_TIMEOUT_MS)
      pending.set(requestId, { resolve, timer })
      socket.send(toBinary(FrameSchema, frame))
    })
    return result
  }

  const expectState = (outcome: ResultEnvelope['outcome'], label: string) => {
    if (outcome.case !== 'projectState' || !outcome.value.summary) {
      fail(`${label} returned ${outcome.case ?? 'empty'} instead of projectState`)
    }
    return outcome.value.summary
  }

  // 1. Create
  const createOutcome = await sendCommand('smoke-create', {
    case: 'createProject',
    value: create(CreateProjectCommandSchema, {
      displayName: 'Smoke Project',
      parentDirectory: projectsParent,
      georeference: create(GeoreferenceConfigSchema, {
        horizontalCrs: 'EPSG:32633',
        linearUnit: 'metre',
        axisConvention: AxisConvention.EASTING_NORTHING_UP,
        originEasting: 0,
        originNorthing: 0,
        verticalCrs: '',
      }),
      trafficSide: TrafficSide.RIGHT,
    }),
  }).catch((error: unknown) => fail(`create failed: ${String(error)}`))
  const created = expectState(createOutcome, 'project.create')
  if (created.revision !== 1n || created.projectUuid.length === 0) {
    fail(`unexpected created summary: revision=${created.revision} uuid=${created.projectUuid}`)
  }
  console.log(`smoke: created project ${created.projectUuid} at ${created.directory}`)

  // 2. get_summary
  const summaryOutcome = await sendCommand('smoke-summary', {
    case: 'getProjectSummary',
    value: create(GetProjectSummaryCommandSchema, {}),
  }).catch((error: unknown) => fail(`get_summary failed: ${String(error)}`))
  const summary = expectState(summaryOutcome, 'project.get_summary')
  if (summary.projectUuid !== created.projectUuid) {
    fail('get_summary returned a different project identity')
  }

  // 3. save
  const saveOutcome = await sendCommand('smoke-save', {
    case: 'saveProject',
    value: create(SaveProjectCommandSchema, {}),
  }).catch((error: unknown) => fail(`save failed: ${String(error)}`))
  const saved = expectState(saveOutcome, 'project.save')
  if (saved.dirty) {
    fail('project still dirty after save')
  }

  // 4. close
  const closeOutcome = await sendCommand('smoke-close', {
    case: 'closeProject',
    value: create(CloseProjectCommandSchema, {}),
  }).catch((error: unknown) => fail(`close failed: ${String(error)}`))
  if (closeOutcome.case !== 'projectClosed') {
    fail(`project.close returned ${closeOutcome.case ?? 'empty'} instead of projectClosed`)
  }
  if (closeOutcome.value.projectUuid !== created.projectUuid) {
    fail('project.close returned a different identity')
  }

  // 5. reopen from disk
  const reopenOutcome = await sendCommand('smoke-reopen', {
    case: 'openProject',
    value: create(OpenProjectCommandSchema, { projectDirectory: created.directory }),
  }).catch((error: unknown) => fail(`reopen failed: ${String(error)}`))
  const reopened = expectState(reopenOutcome, 'project.open')
  if (reopened.projectUuid !== created.projectUuid || reopened.revision !== created.revision) {
    fail(`reopened project mismatch: uuid=${reopened.projectUuid} revision=${reopened.revision}`)
  }

  // 6. Event stream sanity: opened events for create+reopen, closed for
  // close. Events are broadcast after the correlated result, so allow the
  // final event frames to arrive before judging the stream.
  const waitForEventCount = async (count: number): Promise<void> => {
    const deadline = Date.now() + FRAME_TIMEOUT_MS
    while (receivedEvents.length < count) {
      if (Date.now() > deadline) {
        fail(`timed out waiting for project events (have ${receivedEvents.length}, want ${count})`)
      }
      await new Promise((resolveTimer) => setTimeout(resolveTimer, 25))
    }
  }
  await waitForEventCount(3)
  const kinds = receivedEvents.map((event) => event.event.case)
  const openedCount = kinds.filter((kind) => kind === 'projectOpened').length
  const closedCount = kinds.filter((kind) => kind === 'projectClosed').length
  if (openedCount < 2 || closedCount < 1) {
    fail(`unexpected event stream: ${JSON.stringify(kinds)}`)
  }

  socket.close()
  child.kill()
  await new Promise<void>((resolve) => {
    if (child.exitCode !== null) {
      resolve()
      return
    }
    const exitGuard = setTimeout(resolve, 3_000)
    child.once('exit', () => {
      clearTimeout(exitGuard)
      resolve()
    })
  })

  try {
    await rm(scratch, { recursive: true, force: true, maxRetries: 5, retryDelay: 100 })
  } catch {
    // Scratch cleanup must never mask a passing verification run.
    console.log(`smoke: scratch directory left in place: ${scratch}`)
  }

  console.log(
    `ENGINE-SMOKE-OK {"projectUuid":"${created.projectUuid}","revision":${created.revision},"events":[${kinds.join(',')}]}`,
  )
  process.exit(0)
}

await main()
