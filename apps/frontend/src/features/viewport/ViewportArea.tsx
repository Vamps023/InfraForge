import { Box } from 'lucide-react'
import { useViewportStore, viewportSurfaceActive } from './viewportStore'
import { ProjectHomeScreen } from '../../editor/shell/ProjectHomeScreen'
import type { CommandContext } from '../../editor/commands/useCommands'

// ViewportArea — hosts the native Vulkan child HWND and shows overlay
// states (starting/error) when the renderer surface is not active. The
// viewport host div must always be in the DOM for the native viewport
// process lifecycle; overlays are CSS-only and do not occlude the HWND.
//
// The native viewport is a child HWND reparented onto the BrowserWindow.
// CSS overlays in the webview cannot render above it, so when an overlay
// that must be visible to the user is shown (the project home screen when
// no project is open or the Home workspace is active), the parent reports
// a blocking overlay so the desktop shell hides the native viewport
// (ViewportVisibilityPolicy).
//
// UI precedence inside the viewport area:
//   1. viewport-host div (always mounted — native viewport lifecycle)
//   2. ProjectHomeScreen (when showHomeScreen — independent of renderer
//      state, because hiding the native viewport causes the renderer to
//      report 'suspended', which is NOT surfaceActive; gating Home on
//      surfaceActive would create a circular dependency that makes Home
//      disappear)
//   3. Renderer status overlay (when !showHomeScreen && !surfaceActive —
//      only shown for authoring workspaces when the renderer is not yet
//      ready or has failed)
export interface ViewportAreaProps {
  hostRef: React.RefObject<HTMLDivElement | null>
  showHomeScreen: boolean
  commandContext: CommandContext
}

export function ViewportArea({ hostRef, showHomeScreen, commandContext }: ViewportAreaProps) {
  const rendererStatus = useViewportStore((state) => state.status)
  const surfaceActive = viewportSurfaceActive(rendererStatus.state)

  return (
    <main className="viewport-area" aria-label="Viewport">
      <div className="viewport-host" ref={hostRef} />
      {showHomeScreen ? (
        <ProjectHomeScreen context={commandContext} />
      ) : !surfaceActive ? (
        <div className="viewport-overlay">
          <div className="viewport-grid" aria-hidden="true" />
          <div className="empty-state">
            <div className="empty-state-icon">
              <Box size={22} />
            </div>
            {rendererStatus.state === 'failed' || rendererStatus.state === 'stopped' ? (
              <>
                <h1>Native viewport unavailable</h1>
                <p className="empty-state-error">{rendererStatus.detail}</p>
              </>
            ) : (
              <>
                <h1>Starting native viewport…</h1>
                <p>{rendererStatus.detail}</p>
              </>
            )}
          </div>
        </div>
      ) : null}
    </main>
  )
}
