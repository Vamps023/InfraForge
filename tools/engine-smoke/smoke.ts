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
  GetGeoreferenceCommandSchema,
  GetProjectSummaryCommandSchema,
  OpenProjectCommandSchema,
  ProtocolVersionSchema,
  SaveProjectCommandSchema,
  SetGeoreferenceCommandSchema,
  TrafficSide,
  TransformToProjectGlobalCommandSchema,
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

  // 6. Canonical georeference query: resolved metadata + persisted config.
  const geoOutcome = await sendCommand('smoke-geo-get', {
    case: 'getGeoreference',
    value: create(GetGeoreferenceCommandSchema, {}),
  }).catch((error: unknown) => fail(`geo.get_georeference failed: ${String(error)}`))
  if (geoOutcome.case !== 'georeferenceState' || !geoOutcome.value.georeference) {
    fail(`geo.get_georeference returned ${geoOutcome.case ?? 'empty'} instead of georeferenceState`)
  }
  const geoInfo = geoOutcome.value.georeference
  if (
    geoInfo.config?.horizontalCrs !== 'EPSG:32633' ||
    geoInfo.horizontalCrs?.identifier !== 'EPSG:32633' ||
    geoInfo.horizontalCrs?.kind !== 'PROJECTED_CRS'
  ) {
    fail(`unexpected georeference info: ${JSON.stringify(geoInfo)}`)
  }

  // 7. Source->project transform through the Geo service. Control point:
  // lon 15 is the UTM zone 33N central meridian (easting = 500000 m); the
  // northing is k0 * meridional arc(55 deg) ~= 6094791.42 m.
  const transformOutcome = await sendCommand('smoke-geo-transform', {
    case: 'transformToProjectGlobal',
    value: create(TransformToProjectGlobalCommandSchema, {
      sourceCrs: 'EPSG:4326',
      coordinates: [{ x: 15.0, y: 55.0, z: 0.0 }],
    }),
  }).catch((error: unknown) => fail(`geo.transform_to_project_global failed: ${String(error)}`))
  if (transformOutcome.case !== 'transformToProjectGlobal') {
    fail(`geo.transform returned ${transformOutcome.case ?? 'empty'} instead of transformToProjectGlobal`)
  }
  const transformed = transformOutcome.value.coordinates[0]
  if (
    !transformed ||
    Math.abs(transformed.easting - 500000.0) > 0.001 ||
    Math.abs(transformed.northing - 6094791.42) > 0.01
  ) {
    fail(`control-point transform mismatch: ${JSON.stringify(transformed)}`)
  }
  console.log(`smoke: transform verified E=${transformed.easting.toFixed(4)} N=${transformed.northing.toFixed(4)}`)

  // 8. Canonical georeference update: persists, bumps revision, broadcasts.
  const setOutcome = await sendCommand('smoke-geo-set', {
    case: 'setGeoreference',
    value: create(SetGeoreferenceCommandSchema, {
      georeference: create(GeoreferenceConfigSchema, {
        horizontalCrs: 'EPSG:32632',
        linearUnit: 'metre',
        axisConvention: AxisConvention.EASTING_NORTHING_UP,
        originEasting: 300000.0,
        originNorthing: 5500000.0,
        originHeight: 12.5,
        verticalCrs: 'EPSG:3855',
      }),
      expectedRevision: reopened.revision,
    }),
  }).catch((error: unknown) => fail(`geo.set_georeference failed: ${String(error)}`))
  if (setOutcome.case !== 'georeferenceState' || setOutcome.value.georeference?.config?.horizontalCrs !== 'EPSG:32632') {
    fail(`geo.set_georeference returned ${setOutcome.case ?? 'empty'} or the wrong CRS`)
  }
  if (setOutcome.value.revision !== reopened.revision + 1n) {
    fail(`georeference update did not advance the revision (got ${setOutcome.value.revision})`)
  }

  // 9. save + close + reopen: the updated canonical georeference survives.
  await sendCommand('smoke-save-2', {
    case: 'saveProject',
    value: create(SaveProjectCommandSchema, {}),
  }).catch((error: unknown) => fail(`second save failed: ${String(error)}`))
  await sendCommand('smoke-close-2', {
    case: 'closeProject',
    value: create(CloseProjectCommandSchema, {}),
  }).catch((error: unknown) => fail(`second close failed: ${String(error)}`))
  const reopen2Outcome = await sendCommand('smoke-reopen-2', {
    case: 'openProject',
    value: create(OpenProjectCommandSchema, { projectDirectory: created.directory }),
  }).catch((error: unknown) => fail(`second reopen failed: ${String(error)}`))
  const reopened2 = expectState(reopen2Outcome, 'project.open(2)')
  const persistedGeo = reopened2.georeference
  if (
    persistedGeo?.horizontalCrs !== 'EPSG:32632' ||
    persistedGeo.originHeight !== 12.5 ||
    persistedGeo.verticalCrs !== 'EPSG:3855'
  ) {
    fail(`persisted georeference mismatch after reopen: ${JSON.stringify(persistedGeo)}`)
  }
  await sendCommand('smoke-close-3', {
    case: 'closeProject',
    value: create(CloseProjectCommandSchema, {}),
  }).catch((error: unknown) => fail(`final close failed: ${String(error)}`))

  // 10. Event stream sanity: opened events for create+reopens, closed for
  // closes, georeference/revision/dirty events for the canonical update.
  // Events are broadcast after the correlated result, so allow the final
  // event frames to arrive before judging the stream.
  const waitForEventCount = async (count: number): Promise<void> => {
    const deadline = Date.now() + FRAME_TIMEOUT_MS
    while (receivedEvents.length < count) {
      if (Date.now() > deadline) {
        fail(`timed out waiting for project events (have ${receivedEvents.length}, want ${count})`)
      }
      await new Promise((resolveTimer) => setTimeout(resolveTimer, 25))
    }
  }
  await waitForEventCount(9)
  const kinds = receivedEvents.map((event) => event.event.case)
  const openedCount = kinds.filter((kind) => kind === 'projectOpened').length
  const closedCount = kinds.filter((kind) => kind === 'projectClosed').length
  const geoChangedCount = kinds.filter((kind) => kind === 'georeferenceChanged').length
  if (openedCount < 3 || closedCount < 2 || geoChangedCount < 1) {
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
