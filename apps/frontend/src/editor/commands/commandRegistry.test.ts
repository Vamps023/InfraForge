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
    const availability = deriveAvailability(status, {} as never, { rev: 1 } as never, null, 'ready')
    expect(availability.engine).toBe('ready')
    expect(availability.project).toBe('project-open')
  })

  it('reports no-project when the project store has no summary', () => {
    const status: EngineSessionStatus = { state: 'ready', message: 'ready' }
    const availability = deriveAvailability(status, {} as never, null, null, 'ready')
    expect(availability.project).toBe('no-project')
  })

  it('reports busy when an operation is in progress', () => {
    const status: EngineSessionStatus = { state: 'ready', message: 'ready' }
    const availability = deriveAvailability(status, {} as never, { rev: 1 } as never, 'saving', 'ready')
    expect(availability.project).toBe('busy')
  })

  it('reports viewport inactive when the viewport is not ready', () => {
    const status: EngineSessionStatus = { state: 'ready', message: 'ready' }
    const availability = deriveAvailability(status, {} as never, null, null, 'failed')
    expect(availability.viewportActive).toBe(false)
  })
})

describe('commandRegistry shortcut conflicts', () => {
  it('rejects two commands claiming the same key+modifier combination', () => {
    commandRegistry.register(
      makeCommand({ id: 'a', shortcut: { key: 's', ctrlOrCmd: true, display: 'Ctrl+S' } }),
    )
    expect(() =>
      commandRegistry.register(
        makeCommand({ id: 'b', shortcut: { key: 's', ctrlOrCmd: true, display: 'Ctrl+S' } }),
      ),
    ).toThrowError(/Shortcut conflict/)
  })

  it('allows different keys with the same modifiers', () => {
    commandRegistry.register(
      makeCommand({ id: 'a', shortcut: { key: 's', ctrlOrCmd: true, display: 'Ctrl+S' } }),
    )
    expect(() =>
      commandRegistry.register(
        makeCommand({ id: 'b', shortcut: { key: 'o', ctrlOrCmd: true, display: 'Ctrl+O' } }),
      ),
    ).not.toThrow()
  })

  it('allows the same key with different modifiers', () => {
    commandRegistry.register(
      makeCommand({ id: 'a', shortcut: { key: 's', ctrlOrCmd: true, display: 'Ctrl+S' } }),
    )
    expect(() =>
      commandRegistry.register(
        makeCommand({ id: 'b', shortcut: { key: 's', ctrlOrCmd: true, shift: true, display: 'Ctrl+Shift+S' } }),
      ),
    ).not.toThrow()
  })
})

describe('commandRegistry surfaces', () => {
  it('bySurface filters commands by their declared surfaces', () => {
    commandRegistry.register(makeCommand({ id: 'menu-only', category: 'X' }))
    commandRegistry.register(
      makeCommand({ id: 'toolbar-cmd', category: 'X', surfaces: ['menu', 'toolbar'] }),
    )
    commandRegistry.register(
      makeCommand({ id: 'shortcut-only', category: 'X', surfaces: ['shortcut'] }),
    )
    expect(commandRegistry.bySurface('toolbar').map((c) => c.id)).toEqual(['toolbar-cmd'])
    expect(commandRegistry.bySurface('shortcut').map((c) => c.id)).toEqual(['shortcut-only'])
    // Default surface is 'menu' when not declared.
    expect(commandRegistry.bySurface('menu').map((c) => c.id)).toEqual(['menu-only', 'toolbar-cmd'])
  })
})

describe('commandRegistry observable subscription', () => {
  it('subscribe is notified on register', () => {
    const listener = vi.fn()
    const unsub = commandRegistry.subscribe(listener)
    commandRegistry.register(makeCommand({ id: 'obs-1' }))
    expect(listener).toHaveBeenCalledTimes(1)
    unsub()
  })

  it('subscribe is notified on unregister', () => {
    commandRegistry.register(makeCommand({ id: 'obs-2' }))
    const listener = vi.fn()
    const unsub = commandRegistry.subscribe(listener)
    commandRegistry.unregister('obs-2')
    expect(listener).toHaveBeenCalledTimes(1)
    unsub()
  })

  it('getSnapshot is stable when nothing changes', () => {
    commandRegistry.register(makeCommand({ id: 'snap-1' }))
    const a = commandRegistry.getSnapshot()
    const b = commandRegistry.getSnapshot()
    expect(a).toBe(b)
  })

  it('getSnapshot changes after a mutation', () => {
    commandRegistry.register(makeCommand({ id: 'snap-2' }))
    const before = commandRegistry.getSnapshot()
    commandRegistry.register(makeCommand({ id: 'snap-3' }))
    const after = commandRegistry.getSnapshot()
    expect(after).not.toBe(before)
    expect(after.map((c) => c.id)).toContain('snap-3')
  })

  it('unsubscribe stops notifications', () => {
    const listener = vi.fn()
    const unsub = commandRegistry.subscribe(listener)
    unsub()
    commandRegistry.register(makeCommand({ id: 'obs-3' }))
    expect(listener).not.toHaveBeenCalled()
  })
})
