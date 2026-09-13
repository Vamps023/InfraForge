import { beforeEach, describe, expect, it, vi } from 'vitest'
import {
  commandRegistry,
  executeCommand,
  isCommandVisible,
  commandSurfaces,
  resolveCommandAvailability,
  type CommandContext,
  type CommandDefinition,
} from './commandRegistry'
import type { AvailabilityContext } from '../availability'

function readyContext(): AvailabilityContext {
  return {
    engine: 'ready',
    engineMessage: 'ready',
    project: 'project-open',
    viewportActive: true,
  }
}

function ctx(availability: AvailabilityContext): CommandContext {
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

describe('commandSurfaces', () => {
  it('defaults to ["menu"] when surfaces is not declared', () => {
    expect(commandSurfaces(makeCommand())).toEqual(['menu'])
  })

  it('returns the declared surfaces', () => {
    expect(
      commandSurfaces(makeCommand({ surfaces: ['menu', 'toolbar', 'shortcut', 'palette'] })),
    ).toEqual(['menu', 'toolbar', 'shortcut', 'palette'])
  })
})

describe('isCommandVisible', () => {
  it('returns true when no visible predicate is declared', () => {
    expect(isCommandVisible(makeCommand(), ctx(readyContext()))).toBe(true)
  })

  it('returns false when visible predicate returns false', () => {
    expect(
      isCommandVisible(makeCommand({ visible: () => false }), ctx(readyContext())),
    ).toBe(false)
  })

  it('returns true when visible predicate returns true', () => {
    expect(
      isCommandVisible(makeCommand({ visible: () => true }), ctx(readyContext())),
    ).toBe(true)
  })
})

describe('shortcut surface filtering', () => {
  it('a command with shortcut metadata but no shortcut surface is not a shortcut', () => {
    const command = makeCommand({
      id: 'no-surface',
      shortcut: { key: 'k', ctrlOrCmd: true, display: 'Ctrl+K' },
      surfaces: ['menu'],
    })
    expect(commandSurfaces(command).includes('shortcut')).toBe(false)
  })

  it('a command with shortcut metadata and shortcut surface is a shortcut', () => {
    const command = makeCommand({
      id: 'has-surface',
      shortcut: { key: 'k', ctrlOrCmd: true, display: 'Ctrl+K' },
      surfaces: ['menu', 'shortcut'],
    })
    expect(commandSurfaces(command).includes('shortcut')).toBe(true)
  })
})

describe('visible predicate in availability resolution', () => {
  it('visible=false does not affect resolveCommandAvailability (visibility is separate from enabled)', () => {
    const command = makeCommand({ visible: () => false })
    // resolveCommandAvailability checks enabled, not visible. Visibility is
    // a separate concern handled by isCommandVisible at the UI layer.
    expect(resolveCommandAvailability(command, ctx(readyContext())).enabled).toBe(true)
  })
})

describe('requiresEngine default', () => {
  it('a command without requiresEngine does not require the engine', () => {
    const command = makeCommand()
    const noEngine = ctx({ ...readyContext(), engine: 'starting', engineMessage: 'starting' })
    expect(resolveCommandAvailability(command, noEngine).enabled).toBe(true)
  })
})
