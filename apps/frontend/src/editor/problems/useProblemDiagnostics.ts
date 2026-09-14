import { useEffect } from 'react'
import { useProjectStore } from '../../features/project/projectStore'
import { useViewportStore } from '../../features/viewport/viewportStore'
import { useProblemsStore } from '../problems/problemsStore'

// Bridges real frontend-owned projections (project command failures, viewport
// failures) into the problems store as diagnostics. This does not fabricate
// warnings; it only surfaces the real errors those stores already report.
// Backend-projected diagnostics arrive through event subscriptions via
// shellEventProjector; this bridge covers the shell-owned surfaces today.
//
// Source identity contract:
//   - `project` source: project command failures from useProjectStore.lastError.
//     These are application-level command errors (e.g., save failed, open
//     failed), NOT engine transport connection failures.
//   - `viewport` source: viewport lifecycle failures from useViewportStore.
//   - Engine transport connection status (useUiStore.engineStatus) is a
//     separate surface and is NOT bridged here to avoid conflating transport
//     connection state with project command failures.
//
// Stable diagnostic identity: each shell-owned source uses a single stable
// id per source so a new failure replaces (not accumulates with) the
// previous one, and recovery clears the source entirely. This avoids stale
// Problems entries after the viewport recovers.

const VIEWPORT_STATUS_ID = 'viewport:status'
const PROJECT_COMMAND_ID = 'project-command:error'

export function useProblemDiagnostics(): void {
  const lastError = useProjectStore((state) => state.lastError)
  const viewportStatus = useViewportStore((state) => state.status)
  const upsert = useProblemsStore((state) => state.upsert)
  const remove = useProblemsStore((state) => state.remove)

  // Project command errors: one current diagnostic at most. Recovery (no
  // lastError) clears the project source entirely. This is distinct from
  // engine transport connection status.
  useEffect(() => {
    if (lastError) {
      upsert({
        id: PROJECT_COMMAND_ID,
        severity: 'error',
        message: lastError.message,
        source: 'project',
      })
    } else {
      remove(PROJECT_COMMAND_ID)
    }
  }, [lastError, upsert, remove])

  // Viewport failures: one current viewport status diagnostic at most. A
  // new failure replaces the previous one (same stable id), and recovery
  // (not failed/stopped) removes it entirely. No accumulation when the
  // failure detail changes repeatedly.
  useEffect(() => {
    if (viewportStatus.state === 'failed' || viewportStatus.state === 'stopped') {
      upsert({
        id: VIEWPORT_STATUS_ID,
        severity: 'error',
        message: `Viewport: ${viewportStatus.detail}`,
        source: 'viewport',
      })
    } else {
      remove(VIEWPORT_STATUS_ID)
    }
  }, [viewportStatus, upsert, remove])
}

// Exported for tests so they can assert the stable identity contract.
export const PROBLEM_DIAGNOSTIC_IDS = {
  viewportStatus: VIEWPORT_STATUS_ID,
  projectCommand: PROJECT_COMMAND_ID,
} as const
