import { commandRegistry, type CommandDefinition } from '../../editor/commands/commandRegistry'
import { useShellUiStore } from '../../editor/shell/shellUiStore'
import { useSelectionStore } from '../../editor/selection/selectionStore'
import type { EngineClient } from '../../lib/engineSession'
import { listRoads } from './roadApi'
import { useRoadStore } from './roadStore'

// Road commands registered through the Issue #5 command registry. Each
// command issues real engine commands or opens real dialogs; no fake
// handlers, no duplicated execution in App.tsx.

export interface RoadCommandDeps {
  getEngineClient: () => EngineClient | null
}

export function registerRoadCommands(deps: RoadCommandDeps): void {
  const defs: CommandDefinition[] = [
    {
      id: 'road.create',
      label: 'Create Road…',
      description: 'Create a new road from a source polyline.',
      category: 'Road',
      group: 'road',
      surfaces: ['menu', 'palette', 'toolbar'],
      requiresEngine: true,
      requiresProject: true,
      requiresNotBusy: true,
      execute: () => {
        useShellUiStore.getState().openDialogCommand('create-road')
      },
    },
    {
      id: 'road.delete',
      label: 'Delete Road',
      description: 'Delete the selected road.',
      category: 'Road',
      group: 'road',
      surfaces: ['menu', 'palette'],
      requiresEngine: true,
      requiresProject: true,
      enabled: () => {
        const id = useSelectionStore.getState().primaryId
        return id !== null && id.startsWith('road:')
      },
      execute: async () => {
        const client = deps.getEngineClient()
        if (!client) return
        const id = useSelectionStore.getState().primaryId
        if (!id || !id.startsWith('road:')) return
        const roadId = id.slice('road:'.length)
        const { deleteRoad } = await import('./roadApi')
        await deleteRoad(client, roadId)
        await listRoads(client)
      },
    },
    {
      id: 'road.rename',
      label: 'Rename Road…',
      description: 'Rename the selected road.',
      category: 'Road',
      group: 'road',
      surfaces: ['menu', 'palette'],
      requiresEngine: true,
      requiresProject: true,
      enabled: () => {
        const id = useSelectionStore.getState().primaryId
        return id !== null && id.startsWith('road:')
      },
      execute: () => {
        useShellUiStore.getState().openDialogCommand('rename-road')
      },
    },
    {
      id: 'road.undo',
      label: 'Undo Road Edit',
      description: 'Undo the last road edit.',
      category: 'Road',
      group: 'road',
      surfaces: ['palette'],
      requiresEngine: true,
      requiresProject: true,
      execute: async () => {
        const client = deps.getEngineClient()
        if (!client) return
        const id = useSelectionStore.getState().primaryId
        const roadId = id && id.startsWith('road:') ? id.slice('road:'.length) : ''
        const { UndoRoadCommandSchema } = await import('@infraforge/protocol')
        const { create } = await import('@bufbuild/protobuf')
        await client.sendCommand({
          case: 'undoRoad',
          value: create(UndoRoadCommandSchema, { roadId }),
        })
        await listRoads(client)
      },
    },
    {
      id: 'road.redo',
      label: 'Redo Road Edit',
      description: 'Redo the last undone road edit.',
      category: 'Road',
      group: 'road',
      surfaces: ['palette'],
      requiresEngine: true,
      requiresProject: true,
      execute: async () => {
        const client = deps.getEngineClient()
        if (!client) return
        const id = useSelectionStore.getState().primaryId
        const roadId = id && id.startsWith('road:') ? id.slice('road:'.length) : ''
        const { RedoRoadCommandSchema } = await import('@infraforge/protocol')
        const { create } = await import('@bufbuild/protobuf')
        await client.sendCommand({
          case: 'redoRoad',
          value: create(RedoRoadCommandSchema, { roadId }),
        })
        await listRoads(client)
      },
    },
    {
      id: 'road.fit-source',
      label: 'Refit Road Geometry',
      description: 'Refit the selected road from its source polyline with current parameters.',
      category: 'Road',
      group: 'road',
      surfaces: ['palette'],
      requiresEngine: true,
      requiresProject: true,
      enabled: () => {
        const id = useSelectionStore.getState().primaryId
        return id !== null && id.startsWith('road:')
      },
      execute: async () => {
        const client = deps.getEngineClient()
        if (!client) return
        const id = useSelectionStore.getState().primaryId
        if (!id || !id.startsWith('road:')) return
        const roadId = id.slice('road:'.length)
        const { fitRoadSource } = await import('./roadApi')
        await fitRoadSource(client, roadId, 1.0)
        await listRoads(client)
      },
    },
  ]

  for (const def of defs) {
    commandRegistry.register(def)
  }
}

export function unregisterRoadCommands(): void {
  commandRegistry.unregister('road.create')
  commandRegistry.unregister('road.delete')
  commandRegistry.unregister('road.rename')
  commandRegistry.unregister('road.undo')
  commandRegistry.unregister('road.redo')
  commandRegistry.unregister('road.fit-source')
}
