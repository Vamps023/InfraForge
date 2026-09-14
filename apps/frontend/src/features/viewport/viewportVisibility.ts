import type { ShellDialog } from '../../editor/shell/shellUiStore'
import type { WorkspaceId } from '../../editor/shell/workspaceStore'

// App-level viewport visibility computation. These pure functions are
// the single production implementation of the rules that determine:
//   1. Whether the project home screen should be shown (which hides the
//      native viewport via blockedByOverlay).
//   2. Whether a blocking overlay is active (which tells the desktop shell
//      to hide the native child HWND).
//
// Extracted from App.tsx so that regression tests exercise the real
// production logic, not a test-only copy.

// The home screen is shown when no project is open OR when the user
// explicitly navigates to the Home workspace. In both cases the native
// viewport must be hidden so the CSS overlay is visible to the user.
export function computeShowHomeScreen(
  projectOpen: boolean,
  activeWorkspace: WorkspaceId,
): boolean {
  return !projectOpen || activeWorkspace === 'home'
}

// A blocking overlay is active when a modal dialog is open OR when the
// home screen is shown. The native child-HWND viewport cannot be occluded
// by CSS z-index, so the page reports this centrally and the desktop shell
// hides/restores the native viewport (with a placement refresh) through
// its visibility policy.
export function computeBlockedByOverlay(
  openDialog: ShellDialog,
  showHomeScreen: boolean,
): boolean {
  return openDialog !== null || showHomeScreen
}
