import { useEffect, useRef, useState } from 'react'
import { Box } from 'lucide-react'
import { connectEngineSession, type EngineSession, type EngineSessionStatus } from './lib/engineSession'
import { GeoreferencePanel } from './features/geo/GeoreferencePanel'
import { NewProjectDialog } from './features/project/NewProjectDialog'
import { subscribeProjectEvents } from './features/project/projectEvents'
import { subscribeShellEvents } from './editor/shell/shellEventProjector'
import { useProjectStore } from './features/project/projectStore'
import { useViewportHost } from './features/viewport/useViewportHost'
import { useViewportStore, viewportSurfaceActive } from './features/viewport/viewportStore'
import { useUiStore } from './state/uiStore'

import { AppHeader } from './editor/shell/AppHeader'
import { WorkspaceRail } from './editor/shell/WorkspaceRail'
import { ContextToolbar } from './editor/shell/ContextToolbar'
import { StatusBar } from './editor/shell/StatusBar'
import { ProjectHomeScreen } from './editor/shell/ProjectHomeScreen'
import { BottomPanel } from './editor/shell/BottomPanel'
import { useWorkspaceStore } from './editor/shell/workspaceStore'
import { EditorLayout } from './editor/layout/EditorLayout'
import { Outliner } from './editor/outliner/Outliner'
import { Inspector } from './editor/inspector/Inspector'
import { registerBuiltinCommands, unregisterBuiltinCommands } from './editor/commands/builtinCommands'
import { CommandPalette } from './editor/commands/CommandPalette'
import { registerProjectOverviewSection, unregisterProjectOverviewSection } from './editor/inspector/projectOverviewSection'
import { registerProjectRootProjection, unregisterProjectRootProjection } from './editor/outliner/projectRootProjection'
import { useCommandContext, useCommandShortcuts } from './editor/commands/useCommands'
import { useShellUiStore } from './editor/shell/shellUiStore'
import { useProblemDiagnostics } from './editor/problems/useProblemDiagnostics'

import { ImportTerrainDialog } from './features/terrain/ImportTerrainDialog'
import { DiagnosticsDialog } from './editor/shell/DiagnosticsDialog'
import { registerTerrainCommands, unregisterTerrainCommands } from './features/terrain/terrainCommands'
import { registerTerrainOutlinerProjection, unregisterTerrainOutlinerProjection } from './features/terrain/terrainOutlinerProjection'
import { registerTerrainInspectorSection, unregisterTerrainInspectorSection } from './features/terrain/terrainInspectorSection'
import { subscribeTerrainEvents, setTerrainScenePublisher } from './features/terrain/terrainEvents'
import { fetchTerrainScene } from './features/terrain/terrainApi'

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
function ViewportArea({
  hostRef,
  showHomeScreen,
  commandContext,
}: {
  hostRef: React.RefObject<HTMLDivElement | null>
  showHomeScreen: boolean
  commandContext: ReturnType<typeof useCommandContext>
}) {
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

export function App() {
  const engineStatus = useUiStore((state) => state.engineStatus)
  const setEngineStatus = useUiStore((state) => state.setEngineStatus)
  const [engineSession, setEngineSession] = useState<EngineSession | null>(null)
  const disposersRef = useRef<(() => void) | null>(null)
  const viewportHostRef = useRef<HTMLDivElement | null>(null)

  const openDialog = useShellUiStore((state) => state.openDialog)
  const closeDialog = useShellUiStore((state) => state.closeDialog)

  // Register builtin commands and the project-overview inspector section
  // once. Commands close over the live engine client via the getter so they
  // always see the current session without re-registration.
  useEffect(() => {
    registerBuiltinCommands({ getEngineClient: () => engineSessionRef.current?.client ?? null })
    registerProjectOverviewSection()
    registerProjectRootProjection()
    registerTerrainCommands({ getEngineClient: () => engineSessionRef.current?.client ?? null })
    registerTerrainOutlinerProjection()
    registerTerrainInspectorSection({ getEngineClient: () => engineSessionRef.current?.client ?? null })
    return () => {
      unregisterBuiltinCommands()
      unregisterProjectOverviewSection()
      unregisterProjectRootProjection()
      unregisterTerrainCommands()
      unregisterTerrainOutlinerProjection()
      unregisterTerrainInspectorSection()
    }
  }, [])

  // Keep a ref of the session so command handlers (registered once) read the
  // live client without depending on the session in their closure.
  const engineSessionRef = useRef<EngineSession | null>(null)
  useEffect(() => {
    engineSessionRef.current = engineSession
  }, [engineSession])

  const commandContext = useCommandContext(engineStatus, engineSession)
  useCommandShortcuts(commandContext)
  useProblemDiagnostics()

  // Help menu → Diagnostics: the desktop shell sends menu:diagnostics when
  // the user clicks Help → Diagnostics or Help → About. Open the same dialog
  // the command palette uses.
  useEffect(() => {
    const desktop = window.infraforgeDesktop
    if (!desktop?.onDiagnosticsRequest) {
      return
    }
    return desktop.onDiagnosticsRequest(() => {
      useShellUiStore.getState().openDialogCommand('diagnostics')
    })
  }, [])

  const summary = useProjectStore((state) => state.summary)
  const operation = useProjectStore((state) => state.operation)
  const projectOpen = summary !== null
  const busy = operation !== null
  const activeWorkspace = useWorkspaceStore((state) => state.activeWorkspace)

  // The home screen is shown when no project is open OR when the user
  // explicitly navigates to the Home workspace. In both cases the native
  // viewport must be hidden so the CSS overlay is visible to the user.
  const showHomeScreen = !projectOpen || activeWorkspace === 'home'

  // Blocking application overlays that render over the editor surface. The
  // native child-HWND viewport cannot be occluded by CSS z-index, so the
  // page reports this centrally and the desktop shell hides/restores the
  // native viewport (with a placement refresh) through its visibility
  // policy.
  //
  // The project home screen (shown when no project is open or when the user
  // navigates to the Home workspace) is a CSS overlay that must be visible
  // to the user. Because the native viewport HWND sits on top of the
  // webview, the home screen would be hidden behind it unless we report a
  // blocking overlay so the desktop shell hides the native surface.
  //
  // The blocking signal includes `!projectOpen` (not `surfaceActive &&
  // !projectOpen`) so that `blockedByOverlay` is true from initial mount —
  // before the viewport process starts — rather than transitioning to true
  // only when the renderer reports ready. This avoids a race where the
  // desktop shell's post-readiness `applyViewportVisibilityPlan` runs
  // before the renderer's `setViewportVisible(false)` IPC round-trip
  // lands, which would briefly show the native surface over the home
  // screen.
  const blockingOverlayActive = openDialog !== null || showHomeScreen
  useViewportHost(viewportHostRef, { blockedByOverlay: blockingOverlayActive })

  useEffect(() => {
    let cancelled = false

    void (async () => {
      const result = await connectEngineSession((status) => {
        if (!cancelled) {
          setEngineStatus(status)
        }
      })

      if (result.state !== 'ready') {
        return
      }

      const unsubscribe = subscribeProjectEvents(result.session.client)
      const unsubscribeShell = subscribeShellEvents(result.session.client)
      const unsubscribeTerrain = subscribeTerrainEvents(result.session.client)

      // Wire the terrain scene publisher: when terrain work settles, fetch
      // the updated scene projection and forward it to the native viewport
      // through the desktop shell IPC bridge.
      setTerrainScenePublisher(() => {
        void (async () => {
          const scene = await fetchTerrainScene(result.session.client).catch(() => null)
          if (scene) {
            window.infraforgeDesktop?.setViewportScene?.(scene as Record<string, unknown>)
          }
        })()
      })

      const disposeSession = () => {
        unsubscribe()
        unsubscribeShell()
        unsubscribeTerrain()
        setTerrainScenePublisher(null)
        result.session.dispose()
      }

      if (cancelled) {
        disposeSession()
        return
      }
      disposersRef.current = disposeSession
      setEngineSession(result.session)

      // A supervised engine always starts without a project. Project-scoped
      // hydration is intentionally deferred to the canonical projectOpened
      // event (projectEvents.ts). Sending speculative project commands here
      // creates real PROJECT_NOT_OPEN operation failures during every cold
      // start and can race the user's subsequent open command.
    })().catch((error: unknown) => {
      if (!cancelled) {
        setEngineStatus({
          state: 'failed',
          message: error instanceof Error ? error.message : String(error),
        })
      }
    })

    return () => {
      cancelled = true
      disposersRef.current?.()
      disposersRef.current = null
      setEngineSession(null)
    }
  }, [setEngineStatus])

  return (
    <div className="app-shell">
      <AppHeader context={commandContext} />
      <div className="app-main">
        <WorkspaceRail />
        <div className="app-editor-area">
          {projectOpen && activeWorkspace !== 'home' ? (
            <ContextToolbar context={commandContext} />
          ) : null}
          <EditorLayout
            viewportHostRef={viewportHostRef}
            viewport={
              <ViewportArea
                hostRef={viewportHostRef}
                showHomeScreen={showHomeScreen}
                commandContext={commandContext}
              />
            }
            leftPanel={<Outliner />}
            rightPanel={<Inspector />}
            bottomPanel={<BottomPanel />}
          />
        </div>
      </div>
      <StatusBar engineStatus={engineStatus} />
      {openDialog === 'new-project' && engineSession ? (
        <NewProjectDialog
          client={engineSession.client}
          busy={busy}
          onClose={() => closeDialog()}
        />
      ) : null}
      {openDialog === 'georeference' && engineSession && projectOpen ? (
        <GeoreferencePanel
          client={engineSession.client}
          onClose={() => closeDialog()}
        />
      ) : null}
      {openDialog === 'command-palette' ? (
        <CommandPalette
          context={commandContext}
          onClose={() => closeDialog()}
        />
      ) : null}
      {openDialog === 'import-terrain' && engineSession && projectOpen ? (
        <ImportTerrainDialog
          client={engineSession.client}
          busy={busy}
          onClose={() => closeDialog()}
        />
      ) : null}
      {openDialog === 'diagnostics' ? (
        <DiagnosticsDialog onClose={() => closeDialog()} />
      ) : null}
    </div>
  )
}
