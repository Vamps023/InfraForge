import { useEffect } from 'react'
import { useProjectStore } from '../../features/project/projectStore'
import { useViewportStore } from '../../features/viewport/viewportStore'
import { useProblemsStore, engineSessionDiagnostic, viewportDiagnostic } from '../problems/problemsStore'

// Bridges real frontend-owned projections (engine session errors, viewport
// failures) into the problems store as diagnostics. This does not fabricate
// warnings; it only surfaces the real errors those stores already report.
// Backend-projected diagnostics will arrive through event subscriptions added
// by future domains; this bridge covers the shell-owned surfaces today.
export function useProblemDiagnostics(): void {
  const lastError = useProjectStore((state) => state.lastError)
  const viewportStatus = useViewportStore((state) => state.status)
  const upsert = useProblemsStore((state) => state.upsert)
  const remove = useProblemsStore((state) => state.remove)
  const clearSource = useProblemsStore((state) => state.clearSource)

  useEffect(() => {
    if (lastError) {
      upsert(engineSessionDiagnostic('error', lastError.message))
    } else {
      clearSource('engine')
    }
  }, [lastError, upsert, clearSource])

  useEffect(() => {
    if (viewportStatus.state === 'failed' || viewportStatus.state === 'stopped') {
      upsert(viewportDiagnostic('error', `Viewport: ${viewportStatus.detail}`))
    } else {
      remove(`viewport:Viewport: ${viewportStatus.detail}`)
    }
  }, [viewportStatus, upsert, remove])
}
