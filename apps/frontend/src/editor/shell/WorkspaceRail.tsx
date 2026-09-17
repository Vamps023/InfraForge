import { WorkspaceSwitcher, type WorkspaceSwitcherProps } from './WorkspaceSwitcher'

// Backwards-compatible WorkspaceRail export forwarding to WorkspaceSwitcher
export function WorkspaceRail(props: WorkspaceSwitcherProps) {
  return <WorkspaceSwitcher {...props} />
}

export { WorkspaceSwitcher }
