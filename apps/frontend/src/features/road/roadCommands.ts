import { commandRegistry, type CommandDefinition } from '../../editor/commands/commandRegistry'
import { useShellUiStore } from '../../editor/shell/shellUiStore'
import { useSelectionStore } from '../../editor/selection/selectionStore'
import type { EngineClient } from '../../lib/engineSession'
import { deleteRoad, fitRoadSource, undoRoadEdit, redoRoadEdit } from './roadApi'
import { useRoadStore } from './roadStore'
import { useRoadToolStore } from './roadToolStore'
import { useAuthoringDraftStore } from '../../editor/tools/authoringDraftStore'
import { createRoad } from './roadApi'

// Road commands registered through the Issue #5 command registry. Each
// command issues real engine commands through the canonical roadApi; no
// direct protocol construction, no duplicated execution, no redundant
// listRoads refresh after commands that already update the store (Blocker 18).

export interface RoadCommandDeps {
  getEngineClient: () => EngineClient | null
}

export function registerRoadCommands(deps: RoadCommandDeps): void {
  const defs: CommandDefinition[] = [
    {
      id: 'road.tool.select',
      label: 'Select',
      description: 'Select alignment or control points in the viewport.',
      category: 'Road',
      group: 'road-tools',
      surfaces: ['palette', 'toolbar'],
      requiresProject: true,
      execute: () => {
        useAuthoringDraftStore.getState().clearDraft()
        useAuthoringDraftStore.getState().setTool('select')
      },
    },
    {
      id: 'road.tool.straight',
      label: 'Straight',
      description: 'Insert direct straight segment between 2 points.',
      category: 'Road',
      group: 'road-tools',
      surfaces: ['palette', 'toolbar'],
      requiresEngine: true,
      requiresProject: true,
      execute: () => {
        useAuthoringDraftStore.getState().clearDraft()
        useAuthoringDraftStore.getState().setTool('road.straight')
      },
    },
    {
      id: 'road.tool.arc',
      label: 'Circle Arc',
      description: 'Insert circular arc passing through 3 points.',
      category: 'Road',
      group: 'road-tools',
      surfaces: ['palette', 'toolbar'],
      requiresEngine: true,
      requiresProject: true,
      execute: () => {
        useAuthoringDraftStore.getState().clearDraft()
        useAuthoringDraftStore.getState().setTool('road.arc')
      },
    },
    {
      id: 'road.tool.clothoid',
      label: 'Clothoid Arc',
      description: 'Insert transition spiral with continuous curvature.',
      category: 'Road',
      group: 'road-tools',
      surfaces: ['palette', 'toolbar'],
      requiresEngine: true,
      requiresProject: true,
      execute: () => {
        useAuthoringDraftStore.getState().clearDraft()
        useAuthoringDraftStore.getState().setTool('road.clothoid')
      },
    },
    {
      id: 'road.tool.polyline',
      label: 'Polyline',
      description: 'Draw multi-point alignment polyline fitted by native engine.',
      category: 'Road',
      group: 'road-tools',
      surfaces: ['palette', 'toolbar'],
      requiresEngine: true,
      requiresProject: true,
      execute: () => {
        useAuthoringDraftStore.getState().clearDraft()
        useAuthoringDraftStore.getState().setTool('road.polyline')
      },
    },
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
      id: 'road.finish-drawing',
      label: 'Finish Road',
      category: 'Road', group: 'road', surfaces: ['toolbar', 'palette'],
      requiresEngine: true, requiresProject: true,
      enabled: () => useRoadToolStore.getState().mode === 'drawing' &&
        useRoadToolStore.getState().points.length >= 2,
      execute: async () => {
        const client = deps.getEngineClient(); if (!client) return
        const draft = useRoadToolStore.getState()
        if (draft.mode !== 'drawing' || draft.points.length < 2 || draft.positionTolerance === null) return
        await createRoad(client, draft.name, draft.points.map((p) => p.easting),
          draft.points.map((p) => p.northing), draft.positionTolerance, [], [],
          draft.maxCurvature ?? undefined)
        draft.cancel()
      },
    },
    {
      id: 'road.cancel-drawing', label: 'Cancel Road Drawing', category: 'Road', group: 'road',
      surfaces: ['toolbar', 'palette'], requiresProject: true,
      enabled: () => useRoadToolStore.getState().mode === 'drawing',
      execute: () => useRoadToolStore.getState().cancel(),
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
        // Blocker 18: use canonical roadApi; the command result/event
        // already updates the store, so no redundant listRoads call.
        await deleteRoad(client, roadId)
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
        // Blocker 18: use canonical roadApi for undo. The result/event
        // updates the store; no redundant listRoads call.
        await undoRoadEdit(client, roadId)
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
        // Blocker 18: use canonical roadApi for redo.
        await redoRoadEdit(client, roadId)
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
        // Blocker 18: use canonical roadApi; no redundant listRoads call.
        await fitRoadSource(client, roadId)
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
  commandRegistry.unregister('road.finish-drawing')
  commandRegistry.unregister('road.cancel-drawing')
}
