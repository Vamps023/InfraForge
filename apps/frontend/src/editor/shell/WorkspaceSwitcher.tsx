import { useWorkspaceStore } from './workspaceStore'
import {
  workspaceRegistry,
  type WorkspaceDefinition,
} from '../workspaces/workspaceRegistry'
import { Tooltip } from '../../ui/Tooltip'

export interface WorkspaceSwitcherProps {
  includeFeatureGated?: boolean
  className?: string
}

// WorkspaceSwitcher — persistent, registry-driven workspace navigation.
// Renders only enabled production workspaces in release builds (no fake buttons).
// Complies with docs/06_UI_UX/WORKSPACE_MODEL.md and APP_SHELL.md.
export function WorkspaceSwitcher({
  includeFeatureGated = false,
  className = '',
}: WorkspaceSwitcherProps) {
  const activeWorkspace = useWorkspaceStore((state) => state.activeWorkspace)
  const setWorkspace = useWorkspaceStore((state) => state.setWorkspace)
  const workspaces = workspaceRegistry.getVisible({ includeFeatureGated })

  return (
    <nav
      className={`workspace-switcher workspace-rail ${className}`.trim()}
      aria-label="Workspace navigation"
    >
      {workspaces.map((workspace) => (
        <WorkspaceSwitcherItem
          key={workspace.id}
          workspace={workspace}
          active={activeWorkspace === workspace.id}
          onSelect={() => {
            if (workspace.availability.enabled) {
              setWorkspace(workspace.id)
            }
          }}
        />
      ))}
    </nav>
  )
}

function WorkspaceSwitcherItem({
  workspace,
  active,
  onSelect,
}: {
  workspace: WorkspaceDefinition
  active: boolean
  onSelect: () => void
}) {
  const Icon = workspace.icon
  const enabled = workspace.availability.enabled
  const tooltip = enabled
    ? workspace.label
    : workspace.availability.disabledReason ?? `${workspace.label} — coming later`

  return (
    <Tooltip label={tooltip} side="right">
      <button
        type="button"
        className={`workspace-switcher-item workspace-rail-item${active ? ' active' : ''}${!enabled ? ' disabled' : ''}`}
        aria-label={workspace.label}
        aria-current={active ? 'page' : undefined}
        aria-disabled={!enabled ? true : undefined}
        disabled={!enabled}
        onClick={onSelect}
      >
        <Icon size={18} />
        <span className="workspace-rail-label">{workspace.label}</span>
      </button>
    </Tooltip>
  )
}
