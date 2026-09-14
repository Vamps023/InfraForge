import { WORKSPACES, useWorkspaceStore, type WorkspaceDefinition } from './workspaceStore'
import { Tooltip } from '../../ui/Tooltip'

// WorkspaceRail — vertical navigation rail on the far left of the editor.
// Shows workspace icons; only functional workspaces are clickable.
// Future workspaces are visually disabled with a "Coming later" tooltip.
export function WorkspaceRail() {
  const activeWorkspace = useWorkspaceStore((state) => state.activeWorkspace)
  const setWorkspace = useWorkspaceStore((state) => state.setWorkspace)

  return (
    <nav className="workspace-rail" aria-label="Workspace navigation">
      {WORKSPACES.map((workspace) => (
        <WorkspaceRailItem
          key={workspace.id}
          workspace={workspace}
          active={activeWorkspace === workspace.id}
          onSelect={() => {
            if (workspace.enabled) {
              setWorkspace(workspace.id)
            }
          }}
        />
      ))}
    </nav>
  )
}

function WorkspaceRailItem({
  workspace,
  active,
  onSelect,
}: {
  workspace: WorkspaceDefinition
  active: boolean
  onSelect: () => void
}) {
  const Icon = workspace.icon
  const tooltip = workspace.enabled
    ? workspace.label
    : workspace.futureLabel ?? `${workspace.label} — coming later`

  return (
    <Tooltip label={tooltip} side="right">
      <button
        type="button"
        className={`workspace-rail-item${active ? ' active' : ''}${!workspace.enabled ? ' disabled' : ''}`}
        aria-label={workspace.label}
        aria-current={active ? 'page' : undefined}
        aria-disabled={!workspace.enabled}
        disabled={!workspace.enabled}
        onClick={onSelect}
      >
        <Icon size={18} />
        <span className="workspace-rail-label">{workspace.label}</span>
      </button>
    </Tooltip>
  )
}
