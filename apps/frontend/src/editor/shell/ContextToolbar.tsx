import { Download, FolderOpen, MapPin } from 'lucide-react'
import { useWorkspaceStore } from './workspaceStore'
import { Tooltip } from '../../ui/Tooltip'
import {
  useCommandExecutor,
  type CommandContext,
} from '../commands/useCommands'

// ContextToolbar — context-aware toolbar that shows workspace-specific
// actions. Only renders actions for the active workspace that have real
// functionality. No fake buttons.
//
// Terrain workspace: Import (local file), Download Area, Georeference.
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

  // Home and future workspaces have no context toolbar actions yet.
  return null
}

function TerrainContextToolbar({ context }: { context: CommandContext }) {
  const importLocal = useCommandExecutor('terrain.import-local', context)
  const downloadArea = useCommandExecutor('terrain.download-area', context)
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
