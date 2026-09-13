import { beforeEach, describe, expect, it, vi } from 'vitest'
import {
  commandRegistry,
  executeCommand,
  resolveCommandAvailability,
  type CommandDefinition,
} from './commandRegistry'
import {
  deriveAvailability,
  type AvailabilityContext,
} from '../availability'
import type { EngineSessionStatus } from '../../lib/engineSession'

function readyContext(): AvailabilityContext {
  return {
    engine: 'ready',
    engineMessage: 'ready',
    project: 'project-open',
    viewportActive: true,
  }
}

function noProjectContext(): AvailabilityContext {
  return { ...readyContext(), project: 'no-project' }
}

function noEngineContext(): AvailabilityContext {
  return { ...readyContext(), engine: 'starting', engineMessage: 'starting' }
}

function ctx(availability: AvailabilityContext) {
  return { availability }
}

function makeCommand(overrides: Partial<CommandDefinition> = {}): CommandDefinition {
  return {
    id: 'test.cmd',
    label: 'Test',
    category: 'Test',
    execute: vi.fn(),
    ...overrides,
  }
}

beforeEach(() => {
  for (const command of commandRegistry.all()) {
    commandRegistry.unregister(command.id)
  }
})

describe('commandRegistry registration', () => {
  it('registers and retrieves a command', () => {
    const command = makeCommand()
    commandRegistry.register(command)
    expect(commandRegistry.get('test.cmd')).toBe(command)
    expect(commandRegistry.all()).toContain(command)
  })

  it('rejects duplicate command ids', () => {
    commandRegistry.register(makeCommand())
    expect(() => commandRegistry.register(makeCommand())).toThrowError(/Duplicate command id/)
  })

  it('unregisters a command', () => {
    commandRegistry.register(makeCommand())
    commandRegistry.unregister('test.cmd')
    expect(commandRegistry.get('test.cmd')).toBeUndefined()
  })

  it('byCategory filters commands', () => {
    commandRegistry.register(makeCommand({ id: 'a', category: 'Project' }))
    commandRegistry.register(makeCommand({ id: 'b', category: 'View' }))
    expect(commandRegistry.byCategory('Project').map((c) => c.id)).toEqual(['a'])
  })
})

describe('commandRegistry availability gating', () => {
  it('enables a command with no requirements when engine is ready and project open', () => {
    const command = makeCommand()
    expect(resolveCommandAvailability(command, ctx(readyContext())).enabled).toBe(true)
  })

  it('disables a requiresEngine command when engine is not ready', () => {
    const command = makeCommand({ requiresEngine: true })
    const availability = resolveCommandAvailability(command, ctx(noEngineContext()))
    expect(availability.enabled).toBe(false)
    expect(availability.disabledReason).toMatch(/engine/i)
  })

  it('disables a requiresProject command when no project is open', () => {
    const command = makeCommand({ requiresProject: true })
    const availability = resolveCommandAvailability(command, ctx(noProjectContext()))
    expect(availability.enabled).toBe(false)
    expect(availability.disabledReason).toMatch(/no project/i)
  })

  it('disables a requiresProject command when a project operation is busy', () => {
    const command = makeCommand({ requiresProject: true })
    const availability = resolveCommandAvailability(command, ctx({ ...readyContext(), project: 'busy' }))
    expect(availability.enabled).toBe(false)
    expect(availability.disabledReason).toMatch(/operation is in progress/i)
  })

  it('honors an enabled predicate beyond declared requirements', () => {
    const command = makeCommand({ enabled: () => false })
    expect(resolveCommandAvailability(command, ctx(readyContext())).enabled).toBe(false)
  })

  it('does not require engine by default', () => {
    const command = makeCommand()
    expect(resolveCommandAvailability(command, ctx(noEngineContext())).enabled).toBe(true)
  })
})

describe('commandRegistry execution', () => {
  it('executes an enabled command', async () => {
    const execute = vi.fn()
    commandRegistry.register(makeCommand({ id: 'run', execute }))
    const ran = await executeCommand('run', ctx(readyContext()))
    expect(ran).toBe(true)
    expect(execute).toHaveBeenCalledTimes(1)
  })

  it('does not execute a gated command', async () => {
    const execute = vi.fn()
    commandRegistry.register(makeCommand({ id: 'gated', requiresProject: true, execute }))
    const ran = await executeCommand('gated', ctx(noProjectContext()))
    expect(ran).toBe(false)
    expect(execute).not.toHaveBeenCalled()
  })

  it('returns false for an unknown command', async () => {
    const ran = await executeCommand('does.not.exist', ctx(readyContext()))
    expect(ran).toBe(false)
  })
})

describe('shared command identity across surfaces', () => {
  it('the same registered command object drives menu, toolbar, and shortcut resolution', () => {
    const execute = vi.fn()
    commandRegistry.register(makeCommand({ id: 'shared', execute }))
    const command = commandRegistry.get('shared')!
    // Menu, toolbar, and shortcut all resolve availability from the same
    // definition — there is no per-surface re-implementation.
    const menuAvailability = resolveCommandAvailability(command, ctx(readyContext()))
    const toolbarAvailability = resolveCommandAvailability(command, ctx(readyContext()))
    const shortcutAvailability = resolveCommandAvailability(command, ctx(readyContext()))
    expect(menuAvailability).toEqual(toolbarAvailability)
    expect(toolbarAvailability).toEqual(shortcutAvailability)
    expect(menuAvailability.enabled).toBe(true)
  })
})

describe('deriveAvailability', () => {
  it('derives ready engine + open project from the projections', () => {
    const status: EngineSessionStatus = { state: 'ready', message: 'ready' }
    const availability = deriveAvailability(status, {} as never)
    expect(availability.engine).toBe('ready')
  })

  it('reports no-project when the project store has no summary', () => {
    const status: EngineSessionStatus = { state: 'ready', message: 'ready' }
    const availability = deriveAvailability(status, {} as never)
    expect(availability.project).toBe('no-project')
  })
})
