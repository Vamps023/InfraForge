import { Download, FolderOpen, MapPin } from 'lucide-react'
import { useWorkspaceStore } from './workspaceStore'
import { useShellUiStore } from './shellUiStore'
import { useProjectStore } from '../../features/project/projectStore'
import { Tooltip } from '../../ui/Tooltip'

// ContextToolbar — context-aware toolbar that shows workspace-specific
// actions. Only renders actions for the active workspace that have real
// functionality. No fake buttons.
//
// Terrain workspace: Import (local file), Download Area, Georeference.
// Other workspaces: no actions yet (future modules).
export function ContextToolbar() {
  const activeWorkspace = useWorkspaceStore((state) => state.activeWorkspace)

  if (activeWorkspace === 'terrain') {
    return <TerrainContextToolbar />
  }

  // Home and future workspaces have no context toolbar actions yet.
  return null
}

function TerrainContextToolbar() {
  const openTerrainImport = useShellUiStore((state) => state.openTerrainImport)
  const openDialogCommand = useShellUiStore((state) => state.openDialogCommand)
  const summary = useProjectStore((state) => state.summary)
  const projectOpen = summary !== null

  return (
    <div className="context-toolbar" aria-label="Terrain workspace actions">
      <span className="context-toolbar-label">Terrain</span>
      <div className="context-toolbar-divider" />
      <Tooltip label="Import a local GeoTIFF DEM file">
        <button
          type="button"
          className="tool-button"
          disabled={!projectOpen}
          onClick={() => openTerrainImport('local-file')}
        >
          <FolderOpen size={14} /> Import
        </button>
      </Tooltip>
      <Tooltip label="Download terrain DEM for a selected area">
        <button
          type="button"
          className="tool-button"
          disabled={!projectOpen}
          onClick={() => openTerrainImport('download-area')}
        >
          <Download size={14} /> Download Area
        </button>
      </Tooltip>
      <Tooltip label="Open canonical georeference settings">
        <button
          type="button"
          className="tool-button"
          disabled={!projectOpen}
          onClick={() => openDialogCommand('georeference')}
        >
          <MapPin size={14} /> Georeference
        </button>
      </Tooltip>
    </div>
  )
}
