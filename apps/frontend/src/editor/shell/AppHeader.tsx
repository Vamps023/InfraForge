import { Search } from 'lucide-react'
import type { EngineSessionStatus } from '../../lib/engineSession'
import { useProjectStore } from '../../features/project/projectStore'
import { useViewportStore } from '../../features/viewport/viewportStore'
import { useWorkspaceStore, getWorkspace } from './workspaceStore'
import { useShellUiStore } from './shellUiStore'
import { StatusDot, type StatusDotTone } from '../../ui/StatusDot'
import type { CommandContext } from '../commands/useCommands'
import { AppMenu } from './AppMenu'

// AppHeader — compact modern application header. Combines brand, workspace
// title, inline application menu, project chip, global search trigger, and
// status indicators into a single row. The traditional File/Edit/View menu
// is kept inline but styled subtly so it does not dominate the hierarchy.
export function AppHeader({ context }: { context: CommandContext }) {
  const summary = useProjectStore((state) => state.summary)
  const activeWorkspace = useWorkspaceStore((state) => state.activeWorkspace)
  const workspace = getWorkspace(activeWorkspace)
  const openDialogCommand = useShellUiStore((state) => state.openDialogCommand)

  return (
    <header className="app-header">
      <div className="brand">
        <span className="brand-mark">IF</span>
        <span>InfraForge</span>
      </div>
      <div className="header-divider" />
      <AppMenu context={context} />
      <div className="header-divider" />
      <span className="workspace-label">{workspace?.label ?? 'Terrain'}</span>
      <div className="header-divider" />
      <div className="project-chip" title={summary?.directory ?? undefined}>
        {summary ? summary.displayName : 'No project open'}
      </div>
      <div className="header-spacer" />
      <button
        type="button"
        className="header-search-trigger"
        aria-label="Open command palette"
        onClick={() => openDialogCommand('command-palette')}
      >
        <Search size={14} />
        <span>Search commands…</span>
        <span className="search-shortcut">Ctrl+Shift+P</span>
      </button>
      <HeaderStatusGroup />
      <div className="build-label">v0.1.0</div>
    </header>
  )
}

// Engine + renderer status indicators in the header right side.
function HeaderStatusGroup() {
  // We don't have direct engine status here; the StatusBar handles detail.
  // The header shows compact dots only. We read viewport status for the
  // renderer dot; engine status is passed via the StatusBar component.
  const rendererStatus = useViewportStore((state) => state.status)
  const rendererTone: StatusDotTone =
    rendererStatus.state === 'ready' ? 'success' :
    rendererStatus.state === 'failed' || rendererStatus.state === 'stopped' || rendererStatus.state === 'device_lost' ? 'danger' :
    rendererStatus.state === 'suspended' || rendererStatus.state === 'unavailable' ? 'warning' :
    'muted'

  return (
    <div className="header-status-group">
      <span className="status-item" title={`Renderer: ${rendererStatus.detail}`}>
        <StatusDot tone={rendererTone} />
        <span className="viewport-hud-label">Renderer</span>
      </span>
    </div>
  )
}
