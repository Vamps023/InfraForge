import { beforeEach, describe, expect, it } from 'vitest'
import { commandRegistry, type CommandContext } from '../../editor/commands/commandRegistry'
import { registerRoadCommands, unregisterRoadCommands } from './roadCommands'
import { useSelectionStore } from '../../editor/selection/selectionStore'
import { useRoadToolStore } from './roadToolStore'
import type { EngineClient } from '../../lib/engineSession'

// Road command registration tests: verify the road commands are properly
// registered with the command registry and have correct availability gating,
// shortcuts, execution side-effects, and tool state synchronization.

describe('roadCommands', () => {
  const mockEngineClient = {} as EngineClient
  const context: CommandContext = {
    availability: {
      engine: 'ready',
      engineMessage: 'ready',
      project: 'project-open',
      viewportActive: true,
    },
  }

  beforeEach(() => {
    for (const command of commandRegistry.all()) {
      commandRegistry.unregister(command.id)
    }
    useSelectionStore.getState().clear()
    useRoadToolStore.getState().cancel()
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
      'road.finish-drawing',
      'road.cancel-drawing',
      'junction.delete',
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
    expect(cmd?.enabled?.(context)).toBe(false)

    // With a road selected, it should be enabled.
    useSelectionStore.getState().select(['road:abc123'])
    expect(cmd?.enabled?.(context)).toBe(true)

    // With a non-road selection, it should be disabled.
    useSelectionStore.getState().select(['terrain:xyz'])
    expect(cmd?.enabled?.(context)).toBe(false)
  })

  it('road.rename requires a selected road', () => {
    registerRoadCommands({ getEngineClient: () => mockEngineClient })
    const cmd = commandRegistry.get('road.rename')
    expect(cmd).toBeDefined()

    useSelectionStore.getState().select(['road:abc123'])
    expect(cmd?.enabled?.(context)).toBe(true)
  })

  it('road.fit-source requires a selected road', () => {
    registerRoadCommands({ getEngineClient: () => mockEngineClient })
    const cmd = commandRegistry.get('road.fit-source')
    expect(cmd).toBeDefined()

    useSelectionStore.getState().select(['road:abc123'])
    expect(cmd?.enabled?.(context)).toBe(true)
  })

  it('road.finish-drawing has Enter shortcut and gates on points', async () => {
    registerRoadCommands({ getEngineClient: () => mockEngineClient })
    const cmd = commandRegistry.get('road.finish-drawing')
    expect(cmd).toBeDefined()
    expect(cmd?.shortcut).toEqual({ key: 'Enter', display: 'Enter' })
    expect(cmd?.surfaces).toContain('shortcut')
    expect(cmd?.surfaces).toContain('toolbar')

    useRoadToolStore.getState().cancel()
    expect(cmd?.enabled?.(context)).toBe(false)

    useRoadToolStore.getState().begin('Test Road', 1.0, null)
    expect(cmd?.enabled?.(context)).toBe(false)

    useRoadToolStore.getState().append({ easting: 100, northing: 200 })
    expect(cmd?.enabled?.(context)).toBe(false)

    // Append second point with delay to not trigger double-click
    useRoadToolStore.setState({ lastClickTime: Date.now() - 1000 })
    useRoadToolStore.getState().append({ easting: 150, northing: 250 })
    expect(cmd?.enabled?.(context)).toBe(true)
  })

  it('road.finish-drawing creates road, selects road:<id>, and cancels tool', async () => {
    const mockClient = {
      sendCommand: async () => ({
        case: 'createRoadResult',
        value: {
          road: {
            roadId: 'road-new-42',
            name: 'New Road',
            length: 100,
            alignmentSegmentCount: 1,
            protectedAnchorCount: 0,
            revision: 1n,
          },
        },
      }),
    } as unknown as EngineClient

    registerRoadCommands({ getEngineClient: () => mockClient })
    const cmd = commandRegistry.get('road.finish-drawing')

    useRoadToolStore.getState().begin('New Road', 1.0, null)
    useRoadToolStore.setState({ lastClickTime: Date.now() - 1000 })
    useRoadToolStore.getState().append({ easting: 10, northing: 20 })
    useRoadToolStore.setState({ lastClickTime: Date.now() - 1000 })
    useRoadToolStore.getState().append({ easting: 30, northing: 40 })

    await cmd?.execute?.(context)

    expect(useRoadToolStore.getState().mode).toBe('idle')
    expect(useSelectionStore.getState().primaryId).toBe('road:road-new-42')
  })

  it('rapid double-click on roadToolStore finishes drawing when points >= 2', async () => {
    let finished = false
    useRoadToolStore.getState().setFinishCallback(() => {
      finished = true
    })

    useRoadToolStore.getState().begin('Double Click Road', 1.0, null)
    useRoadToolStore.setState({ lastClickTime: Date.now() - 1000 })
    useRoadToolStore.getState().append({ easting: 10, northing: 20 })
    useRoadToolStore.setState({ lastClickTime: Date.now() - 1000 })
    useRoadToolStore.getState().append({ easting: 30, northing: 40 })
    expect(finished).toBe(false)

    // Simulate rapid click at same or next point (within 400ms)
    useRoadToolStore.setState({ lastClickTime: Date.now() - 50 })
    useRoadToolStore.getState().append({ easting: 30, northing: 40 })

    expect(finished).toBe(true)
  })

  it('junction.delete requires a selected junction', () => {
    registerRoadCommands({ getEngineClient: () => mockEngineClient })
    const cmd = commandRegistry.get('junction.delete')
    expect(cmd).toBeDefined()
    expect(cmd?.requiresEngine).toBe(true)
    expect(cmd?.requiresProject).toBe(true)

    // Disabled without selection or non-junction
    expect(cmd?.enabled?.(context)).toBe(false)
    useSelectionStore.getState().select(['road:abc123'])
    expect(cmd?.enabled?.(context)).toBe(false)

    // Enabled with junction
    useSelectionStore.getState().select(['junction:junc-1'])
    expect(cmd?.enabled?.(context)).toBe(true)
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
      'road.finish-drawing',
      'road.cancel-drawing',
      'junction.delete',
    ]
    for (const id of expectedIds) {
      expect(commandRegistry.get(id), `command ${id} should be unregistered`).toBeUndefined()
    }
  })
})
