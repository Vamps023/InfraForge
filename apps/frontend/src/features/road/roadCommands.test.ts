import { beforeEach, describe, expect, it, vi } from 'vitest'
import { commandRegistry } from '../../editor/commands/commandRegistry'
import { registerRoadCommands, unregisterRoadCommands } from './roadCommands'
import { useSelectionStore } from '../../editor/selection/selectionStore'
import type { EngineClient } from '../../lib/engineSession'

// Road command registration tests: verify the road commands are properly
// registered with the command registry and have correct availability gating.
// These tests do not execute the commands (which would require an engine
// client); they verify the command definitions are correct.

describe('roadCommands', () => {
  const mockEngineClient = {} as EngineClient

  beforeEach(() => {
    for (const command of commandRegistry.all()) {
      commandRegistry.unregister(command.id)
    }
    useSelectionStore.getState().clear()
  })

  it('registers all road commands', () => {
    registerRoadCommands({ getEngineClient: () => mockEngineClient })

    const expectedIds = [
      'road.create',
      'road.delete',
      'road.rename',
      'road.undo',
      'road.redo',
      'road.fit-source',
    ]
    for (const id of expectedIds) {
      expect(commandRegistry.get(id), `command ${id} should be registered`).toBeDefined()
    }
  })

  it('road.create requires engine and project', () => {
    registerRoadCommands({ getEngineClient: () => mockEngineClient })
    const cmd = commandRegistry.get('road.create')
    expect(cmd).toBeDefined()
    expect(cmd?.requiresEngine).toBe(true)
    expect(cmd?.requiresProject).toBe(true)
    expect(cmd?.requiresNotBusy).toBe(true)
    expect(cmd?.category).toBe('Road')
    expect(cmd?.group).toBe('road')
  })

  it('road.delete requires a selected road', () => {
    registerRoadCommands({ getEngineClient: () => mockEngineClient })
    const cmd = commandRegistry.get('road.delete')
    expect(cmd).toBeDefined()
    expect(cmd?.requiresEngine).toBe(true)
    expect(cmd?.requiresProject).toBe(true)

    // Without a road selected, the command should be disabled.
    expect(cmd?.enabled?.()).toBe(false)

    // With a road selected, it should be enabled.
    useSelectionStore.getState().select(['road:abc123'])
    expect(cmd?.enabled?.()).toBe(true)

    // With a non-road selection, it should be disabled.
    useSelectionStore.getState().select(['terrain:xyz'])
    expect(cmd?.enabled?.()).toBe(false)
  })

  it('road.rename requires a selected road', () => {
    registerRoadCommands({ getEngineClient: () => mockEngineClient })
    const cmd = commandRegistry.get('road.rename')
    expect(cmd).toBeDefined()

    useSelectionStore.getState().select(['road:abc123'])
    expect(cmd?.enabled?.()).toBe(true)
  })

  it('road.fit-source requires a selected road', () => {
    registerRoadCommands({ getEngineClient: () => mockEngineClient })
    const cmd = commandRegistry.get('road.fit-source')
    expect(cmd).toBeDefined()

    useSelectionStore.getState().select(['road:abc123'])
    expect(cmd?.enabled?.()).toBe(true)
  })

  it('unregisters all road commands', () => {
    registerRoadCommands({ getEngineClient: () => mockEngineClient })
    unregisterRoadCommands()

    const expectedIds = [
      'road.create',
      'road.delete',
      'road.rename',
      'road.undo',
      'road.redo',
      'road.fit-source',
    ]
    for (const id of expectedIds) {
      expect(commandRegistry.get(id), `command ${id} should be unregistered`).toBeUndefined()
    }
  })
})
