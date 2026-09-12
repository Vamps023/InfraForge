import { describe, expect, it, vi, afterEach } from 'vitest'
import { mkdtemp, rm, writeFile, readFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { ViewportSupervisor, type ViewportPlacement, type ViewportStatus } from '../src/ViewportSupervisor.js'

// The fixtures are real node child processes speaking the real stdio
// protocol; the supervisor's spawn seam (commandOverride) redirects the
// executable to node instead of the native viewport binary. No part of the
// supervisor is mocked.
//
// One mode-driven fixture serves all scenarios: the mode file content says
// how the invocation behaves, and a test flips the content between two
// starts of the SAME supervisor instance to prove that failed starts reset
// the supervisor's starting state.

const VIEWPORT_FIXTURE = `
const fs = require('node:fs')
const [, , modeFile, hangPidFile, argvFile] = process.argv
if (argvFile) {
  fs.writeFileSync(argvFile, JSON.stringify(process.argv.slice(2)))
}
const mode = fs.readFileSync(modeFile, 'utf8').trim()
const status = (state) => process.stdout.write(
  'INFRAFORGE_VIEWPORT_STATUS ' + JSON.stringify({ state, detail: 'fixture' }) + '\\n')
if (mode === 'crash') {
  process.exit(3)
}
if (mode === 'hang') {
  fs.writeFileSync(hangPidFile, String(process.pid))
  status('starting')
  setInterval(() => {}, 1000)
} else {
  status('ready')
  process.stdout.write('INFRAFORGE_VIEWPORT_READY ' + JSON.stringify({ platform: 'test' }) + '\\n')
  const rl = require('node:readline').createInterface({ input: process.stdin })
  rl.on('line', (line) => {
    if (line.includes('"shutdown"')) {
      status('stopped')
      process.exit(0)
    }
  })
  rl.on('close', () => process.exit(0))
}
`

const parentWindowHandle = Buffer.alloc(8)
parentWindowHandle.writeBigUInt64LE(0x1234n, 0)

const placement: ViewportPlacement = {
  screenX: 10,
  screenY: 20,
  width: 800,
  height: 600,
  dpiScale: 1,
}

let workDir = ''
let fixturePath = ''
let modeFile = ''

afterEach(async () => {
  delete process.env.INFRAFORGE_VIEWPORT_PATH
  if (workDir) {
    await rm(workDir, { recursive: true, force: true })
    workDir = ''
    fixturePath = ''
    modeFile = ''
  }
})

async function prepareFixture(): Promise<void> {
  workDir = await mkdtemp(path.join(tmpdir(), 'viewport-supervisor-test-'))
  fixturePath = path.join(workDir, 'viewport-fixture.cjs')
  modeFile = path.join(workDir, 'fixture-mode')
  await writeFile(fixturePath, VIEWPORT_FIXTURE)
  // The supervisor must resolve a configured viewport path before spawning.
  process.env.INFRAFORGE_VIEWPORT_PATH = fixturePath
}

async function setMode(mode: string): Promise<void> {
  await writeFile(modeFile, mode)
}

async function processAlive(pid: number): Promise<boolean> {
  try {
    process.kill(pid, 0)
    return true
  } catch {
    return false
  }
}

function createSupervisor(hangPidFile?: string): ViewportSupervisor {
  // All four fixture argument slots (script, modeFile, hangPidFile, argvFile)
  // are reserved with placeholders so the supervisor's control arguments can
  // never slide into a slot and become a stray output filename.
  return new ViewportSupervisor({
    commandOverride: {
      executable: process.execPath,
      leadingArgs: [fixturePath, modeFile, hangPidFile ?? '', ''],
    },
    startupTimeoutMs: 3_000,
  })
}

async function assertStartableAndStoppable(supervisor: ViewportSupervisor): Promise<void> {
  await setMode('ready')
  await supervisor.start(parentWindowHandle, placement)
  expect(supervisor.snapshot().state).toBe('ready')
  supervisor.stop()
  await vi.waitUntil(() => supervisor.snapshot().state === 'stopped', { timeout: 5_000 })
}

describe('ViewportSupervisor lifecycle', () => {
  it('publishes unavailable without INFRAFORGE_VIEWPORT_PATH and stays startable', async () => {
    await prepareFixture()
    delete process.env.INFRAFORGE_VIEWPORT_PATH

    const statuses: ViewportStatus[] = []
    const supervisor = createSupervisor()
    supervisor.setStatusListener((status) => statuses.push(status))

    await supervisor.start(parentWindowHandle, placement)
    expect(supervisor.snapshot().state).toBe('unavailable')

    // Regression: the aborted lookup must not leave `starting` stuck — the
    // same supervisor instance must be able to perform a real start.
    process.env.INFRAFORGE_VIEWPORT_PATH = fixturePath
    await assertStartableAndStoppable(supervisor)
    expect(statuses.some((status) => status.state === 'stopped')).toBe(true)
  })

  it('kills a child that never reports ready and stays startable', async () => {
    await prepareFixture()
    const hangPidFile = path.join(workDir, 'hang.pid')
    const supervisor = createSupervisor(hangPidFile)

    await setMode('hang')
    await supervisor.start(parentWindowHandle, placement)
    expect(supervisor.snapshot().state).toBe('failed')

    // Regression: the timed-out child must actually be killed, not orphaned.
    const pid = Number(await readFile(hangPidFile, 'utf8'))
    expect(Number.isFinite(pid)).toBe(true)
    await vi.waitUntil(async () => !(await processAlive(pid)), { timeout: 5_000 })

    // Regression: the failed start must not leave `starting` stuck.
    await assertStartableAndStoppable(supervisor)
  })

  it('passes --initial-visible 1 to the viewport process by default', async () => {
    await prepareFixture()
    const argvFile = path.join(workDir, 'argv.json')
    const supervisor = new ViewportSupervisor({
      commandOverride: {
        executable: process.execPath,
        leadingArgs: [fixturePath, modeFile, '', argvFile],
      },
      startupTimeoutMs: 3_000,
    })

    await setMode('ready')
    await supervisor.start(parentWindowHandle, placement)
    expect(supervisor.snapshot().state).toBe('ready')

    const argv = JSON.parse(await readFile(argvFile, 'utf8')) as string[]
    expect(argv).toContain('--initial-visible')
    expect(argv[argv.indexOf('--initial-visible') + 1]).toBe('1')

    supervisor.stop()
    await vi.waitUntil(() => supervisor.snapshot().state === 'stopped', { timeout: 5_000 })
  })

  it('passes --initial-visible 0 when startup visibility is hidden', async () => {
    await prepareFixture()
    const argvFile = path.join(workDir, 'argv.json')
    const supervisor = new ViewportSupervisor({
      commandOverride: {
        executable: process.execPath,
        leadingArgs: [fixturePath, modeFile, '', argvFile],
      },
      startupTimeoutMs: 3_000,
    })

    await setMode('ready')
    await supervisor.start(parentWindowHandle, placement, { initialVisible: false })
    expect(supervisor.snapshot().state).toBe('ready')

    const argv = JSON.parse(await readFile(argvFile, 'utf8')) as string[]
    expect(argv).toContain('--initial-visible')
    expect(argv[argv.indexOf('--initial-visible') + 1]).toBe('0')

    // The control path still works after a hidden startup: visibility=true
    // flows through the normal set-visible channel (fixture exits on stdin
    // close, proving the channel is alive).
    supervisor.setVisible(true)
    supervisor.stop()
    await vi.waitUntil(() => supervisor.snapshot().state === 'stopped', { timeout: 5_000 })
  })

  it('reports an explicit failure when the viewport exits before readiness', async () => {
    await prepareFixture()
    const supervisor = createSupervisor()

    await setMode('crash')
    await supervisor.start(parentWindowHandle, placement)
    expect(supervisor.snapshot().state).toBe('failed')
    expect(supervisor.snapshot().detail).toContain('code=3')

    // Regression: the failed start must not leave `starting` stuck.
    await assertStartableAndStoppable(supervisor)
  })
})
