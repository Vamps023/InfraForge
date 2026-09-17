import { Download, FolderOpen, MapPin, Plus, Trash2, Undo2, Redo2, RefreshCw, Check, X, Upload } from 'lucide-react'
import { useWorkspaceStore } from './workspaceStore'
import { Tooltip } from '../../ui/Tooltip'
import {
  useCommandExecutor,
  type CommandContext,
} from '../commands/useCommands'
import { useRoadToolStore } from '../../features/road/roadToolStore'

// ContextToolbar — context-aware toolbar that shows workspace-specific
// actions. Only renders actions for the active workspace that have real
// functionality. No fake buttons.
//
// Terrain workspace: Import (local file), Download Area, Georeference.
// Roads workspace: Create, Delete, Rename, Refit, Undo, Redo.
// Other workspaces: no actions yet (future modules).
//
// All buttons route through the central command registry via
// executeCommand() so availability gating (engine, project, busy state)
// is consistent with the menu, shortcuts, and command palette. The
// disabled state is derived from resolveCommandAvailability() — the same
// path every other surface uses — so the toolbar cannot drift apart from
// the command architecture's safety gating.
export function ContextToolbar({ context }: { context: CommandContext }) {
  const activeWorkspace = useWorkspaceStore((state) => state.activeWorkspace)

  if (activeWorkspace === 'terrain') {
    return <TerrainContextToolbar context={context} />
  }

  if (activeWorkspace === 'roads') {
    return <RoadsContextToolbar context={context} />
  }

  // Home and future workspaces have no context toolbar actions yet.
  return null
}

function TerrainContextToolbar({ context }: { context: CommandContext }) {
  const importLocal = useCommandExecutor('terrain.import-local', context)
  const downloadArea = useCommandExecutor('terrain.download-area', context)
  const exportTerrainCmd = useCommandExecutor('terrain.export', context)
  const georeference = useCommandExecutor('project.georeference', context)

  return (
    <div className="context-toolbar" aria-label="Terrain workspace actions">
      <span className="context-toolbar-label">Terrain</span>
      <div className="context-toolbar-divider" />
      <Tooltip label={importLocal.availability.disabledReason ?? 'Import a local GeoTIFF DEM file'}>
        <button
          type="button"
          className="tool-button"
          disabled={!importLocal.availability.enabled}
          onClick={() => void importLocal.run()}
        >
          <FolderOpen size={14} /> Import
        </button>
      </Tooltip>
      <Tooltip label={downloadArea.availability.disabledReason ?? 'Download terrain DEM for a selected area'}>
        <button
          type="button"
          className="tool-button"
          disabled={!downloadArea.availability.enabled}
          onClick={() => void downloadArea.run()}
        >
          <Download size={14} /> Download Area
        </button>
      </Tooltip>
      <Tooltip label={exportTerrainCmd.availability.disabledReason ?? 'Export terrain heightmaps and albedo textures'}>
        <button
          type="button"
          className="tool-button"
          disabled={!exportTerrainCmd.availability.enabled}
          onClick={() => void exportTerrainCmd.run()}
        >
          <Upload size={14} /> Export
        </button>
      </Tooltip>
      <Tooltip label={georeference.availability.disabledReason ?? 'Open canonical georeference settings'}>
        <button
          type="button"
          className="tool-button"
          disabled={!georeference.availability.enabled}
          onClick={() => void georeference.run()}
        >
          <MapPin size={14} /> Georeference
        </button>
      </Tooltip>
    </div>
  )
}

function RoadsContextToolbar({ context }: { context: CommandContext }) {
  const createRoad = useCommandExecutor('road.create', context)
  const deleteRoad = useCommandExecutor('road.delete', context)
  const renameRoad = useCommandExecutor('road.rename', context)
  const refitRoad = useCommandExecutor('road.fit-source', context)
  const undoRoad = useCommandExecutor('road.undo', context)
  const redoRoad = useCommandExecutor('road.redo', context)
  const finishDrawing = useCommandExecutor('road.finish-drawing', context)
  const cancelDrawing = useCommandExecutor('road.cancel-drawing', context)
  const drawing = useRoadToolStore((state) => state.mode === 'drawing')
  const pointCount = useRoadToolStore((state) => state.points.length)

  return (
    <div className="context-toolbar" aria-label="Roads workspace actions">
      <span className="context-toolbar-label">Roads</span>
      <div className="context-toolbar-divider" />
      {drawing ? <>
        <span className="context-toolbar-label">{pointCount} control points</span>
        <button type="button" className="tool-button" disabled={!finishDrawing.availability.enabled}
          onClick={() => void finishDrawing.run()}><Check size={14} /> Finish</button>
        <button type="button" className="tool-button" disabled={!cancelDrawing.availability.enabled}
          onClick={() => void cancelDrawing.run()}><X size={14} /> Cancel</button>
        <div className="context-toolbar-divider" />
      </> : null}
      <Tooltip label={createRoad.availability.disabledReason ?? 'Create a new road from a source polyline'}>
        <button
          type="button"
          className="tool-button"
          disabled={!createRoad.availability.enabled}
          onClick={() => void createRoad.run()}
        >
          <Plus size={14} /> Create Road
        </button>
      </Tooltip>
      <Tooltip label={deleteRoad.availability.disabledReason ?? 'Delete the selected road'}>
        <button
          type="button"
          className="tool-button"
          disabled={!deleteRoad.availability.enabled}
          onClick={() => void deleteRoad.run()}
        >
          <Trash2 size={14} /> Delete
        </button>
      </Tooltip>
      <Tooltip label={renameRoad.availability.disabledReason ?? 'Rename the selected road'}>
        <button
          type="button"
          className="tool-button"
          disabled={!renameRoad.availability.enabled}
          onClick={() => void renameRoad.run()}
        >
          Rename
        </button>
      </Tooltip>
      <Tooltip label={refitRoad.availability.disabledReason ?? 'Refit road geometry from source polyline'}>
        <button
          type="button"
          className="tool-button"
          disabled={!refitRoad.availability.enabled}
          onClick={() => void refitRoad.run()}
        >
          <RefreshCw size={14} /> Refit
        </button>
      </Tooltip>
      <div className="context-toolbar-divider" />
      <Tooltip label={undoRoad.availability.disabledReason ?? 'Undo the last road edit'}>
        <button
          type="button"
          className="tool-button"
          disabled={!undoRoad.availability.enabled}
          onClick={() => void undoRoad.run()}
        >
          <Undo2 size={14} /> Undo
        </button>
      </Tooltip>
      <Tooltip label={redoRoad.availability.disabledReason ?? 'Redo the last undone road edit'}>
        <button
          type="button"
          className="tool-button"
          disabled={!redoRoad.availability.enabled}
          onClick={() => void redoRoad.run()}
        >
          <Redo2 size={14} /> Redo
        </button>
      </Tooltip>
    </div>
  )
}
