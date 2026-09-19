import { useEffect, useRef, useState } from 'react'
import { connectEngineSession, type EngineSession } from './lib/engineSession'
import { GeoreferencePanel } from './features/geo/GeoreferencePanel'
import { NewProjectDialog } from './features/project/NewProjectDialog'
import { subscribeProjectEvents } from './features/project/projectEvents'
import { subscribeShellEvents } from './editor/shell/shellEventProjector'
import { useProjectStore } from './features/project/projectStore'
import { useViewportHost } from './features/viewport/useViewportHost'
import { ViewportArea } from './features/viewport/ViewportArea'
import { computeShowHomeScreen, computeBlockedByOverlay } from './features/viewport/viewportVisibility'
import { useUiStore } from './state/uiStore'

import { AppHeader } from './editor/shell/AppHeader'
import { WorkspaceRail } from './editor/shell/WorkspaceRail'
import { ContextToolShelf } from './editor/shell/ContextToolShelf'
import { StatusBar } from './editor/shell/StatusBar'
import { BottomPanel } from './editor/shell/BottomPanel'
import { useWorkspaceStore } from './editor/shell/workspaceStore'
import { EditorLayout } from './editor/layout/EditorLayout'
import { Navigator } from './editor/navigator/Navigator'
import { Inspector } from './editor/inspector/Inspector'
import { registerBuiltinCommands, unregisterBuiltinCommands } from './editor/commands/builtinCommands'
import { CommandPalette } from './editor/commands/CommandPalette'
import { registerProjectOverviewSection, unregisterProjectOverviewSection } from './editor/inspector/projectOverviewSection'
import { registerWorldInspectorSection, unregisterWorldInspectorSection } from './features/world/worldInspectorSection'
import { registerProjectRootProjection, unregisterProjectRootProjection } from './editor/outliner/projectRootProjection'
import { useCommandContext, useCommandShortcuts } from './editor/commands/useCommands'
import { useShellUiStore } from './editor/shell/shellUiStore'
import { useProblemDiagnostics } from './editor/problems/useProblemDiagnostics'
import { ContextEditorHost, useActiveContextEditor } from './editor/contextEditor/ContextEditorHost'

import { ImportTerrainDialog } from './features/terrain/ImportTerrainDialog'
import { ExportTerrainDialog } from './features/terrain/ExportTerrainDialog'
import { DiagnosticsDialog } from './editor/shell/DiagnosticsDialog'
import { registerTerrainCommands, unregisterTerrainCommands } from './features/terrain/terrainCommands'
import { registerTerrainOutlinerProjection, unregisterTerrainOutlinerProjection } from './features/terrain/terrainOutlinerProjection'
import { registerTerrainInspectorSection, unregisterTerrainInspectorSection } from './features/terrain/terrainInspectorSection'
import { subscribeTerrainEvents, setTerrainScenePublisher } from './features/terrain/terrainEvents'
import { fetchTerrainScene } from './features/terrain/terrainApi'

import { registerRoadCommands, unregisterRoadCommands } from './features/road/roadCommands'
import { registerRoadOutlinerProjection, unregisterRoadOutlinerProjection } from './features/road/roadOutlinerProjection'
import { registerRoadInspectorSection, unregisterRoadInspectorSection } from './features/road/roadInspectorSection'
import { registerJunctionInspectorSection, unregisterJunctionInspectorSection } from './features/road/JunctionInspectorSection'
import { registerRoadProfileContextEditor, unregisterRoadProfileContextEditor } from './features/road/RoadProfileEditor'
import { subscribeRoadEvents, setRoadScenePublisher } from './features/road/roadEvents'
import { listRoads, fetchRoadScene, moveRoadControl, insertRoadControl } from './features/road/roadApi'
import { CreateRoadDialog } from './features/road/CreateRoadDialog'
import { RenameRoadDialog } from './features/road/RenameRoadDialog'
import { useRoadToolStore } from './features/road/roadToolStore'
import { useToolStore } from './editor/tools/toolStore'
import { useSelectionStore } from './editor/selection/selectionStore'
import { useRoadStore } from './features/road/roadStore'
import { getRoad } from './features/road/roadApi'
import { useAuthoringDraftStore } from './editor/tools/authoringDraftStore'
import { useAuthoringInteraction } from './editor/tools/useAuthoringInteraction'
import { useAuthoringShortcuts } from './editor/commands/useAuthoringShortcuts'
import { ToolRail } from './editor/shell/ToolRail'
import { ToolOptionsPanel } from './editor/shell/ToolOptionsPanel'

export function App() {
  const engineStatus = useUiStore((state) => state.engineStatus)
  const setEngineStatus = useUiStore((state) => state.setEngineStatus)
  const [engineSession, setEngineSession] = useState<EngineSession | null>(null)
  const disposersRef = useRef<(() => void) | null>(null)
  const viewportHostRef = useRef<HTMLDivElement | null>(null)
  const roadToolMode = useRoadToolStore((state) => state.mode)
  const roadDraftPoints = useRoadToolStore((state) => state.points)

  useEffect(() => {
    window.infraforgeDesktop?.setRoadPreview?.(
      roadToolMode === 'drawing' ? roadDraftPoints : [],
    )
  }, [roadToolMode, roadDraftPoints])

  const openDialog = useShellUiStore((state) => state.openDialog)
  const menuOpen = useShellUiStore((state) => state.menuOpen)
  const closeDialog = useShellUiStore((state) => state.closeDialog)

  // Register builtin commands and the project-overview inspector section
  // once. Commands close over the live engine client via the getter so they
  // always see the current session without re-registration.
  useEffect(() => {
    registerBuiltinCommands({ getEngineClient: () => engineSessionRef.current?.client ?? null })
    registerProjectOverviewSection()
    registerWorldInspectorSection()
    registerProjectRootProjection()
    registerTerrainCommands({ getEngineClient: () => engineSessionRef.current?.client ?? null })
    registerTerrainOutlinerProjection()
    registerTerrainInspectorSection({ getEngineClient: () => engineSessionRef.current?.client ?? null })
    registerRoadCommands({ getEngineClient: () => engineSessionRef.current?.client ?? null })
    registerRoadOutlinerProjection()
    registerRoadInspectorSection({ getEngineClient: () => engineSessionRef.current?.client ?? null })
    registerJunctionInspectorSection({ getEngineClient: () => engineSessionRef.current?.client ?? null })
    registerRoadProfileContextEditor({ getEngineClient: () => engineSessionRef.current?.client ?? null })
    return () => {
      unregisterBuiltinCommands()
      unregisterProjectOverviewSection()
      unregisterWorldInspectorSection()
      unregisterProjectRootProjection()
      unregisterTerrainCommands()
      unregisterTerrainOutlinerProjection()
      unregisterTerrainInspectorSection()
      unregisterRoadCommands()
      unregisterRoadOutlinerProjection()
      unregisterRoadInspectorSection()
      unregisterJunctionInspectorSection()
      unregisterRoadProfileContextEditor()
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

  // Centralized selection synchronization: when canonical selection changes,
  // ensure roadStore reflects the selected road and hydrates its details
  // consistently regardless of whether selection originated from Viewport,
  // Outliner, Sources, or commands.
  useEffect(() => {
    return useSelectionStore.subscribe((state, prevState) => {
      if (state.primaryId === prevState.primaryId) {
        return
      }
      const primaryId = state.primaryId
      const client = engineSessionRef.current?.client
      if (primaryId && primaryId.startsWith('road:')) {
        const roadId = primaryId.slice('road:'.length)
        useRoadStore.getState().selectRoad(roadId)
        useRoadStore.getState().selectJunction(null)
        if (client) {
          void getRoad(client, roadId).catch(() => undefined)
        }
      } else if (primaryId && primaryId.startsWith('junction:')) {
        const junctionId = primaryId.slice('junction:'.length)
        useRoadStore.getState().selectJunction(junctionId)
        useRoadStore.getState().selectRoad(null)
        useRoadStore.getState().setDetails(null)
      } else {
        useRoadStore.getState().selectRoad(null)
        useRoadStore.getState().setDetails(null)
        useRoadStore.getState().selectJunction(null)
      }
    })
  }, [])

  const { handleViewportInteraction, commitCurrentDraft } = useAuthoringInteraction({
    getClient: () => engineSessionRef.current?.client ?? null,
  })
  useAuthoringShortcuts(commitCurrentDraft)

  // The shared tool store is the sole owner of viewport input. The authoring
  // draft store keeps only transient construction state and mirrors its
  // selected authoring mode into that central owner.
  useEffect(() => {
    const synchronizeAuthoringTool = (activeTool: ReturnType<typeof useAuthoringDraftStore.getState>['activeTool']) => {
      const toolStore = useToolStore.getState()
      if (activeTool === 'select') {
        if (toolStore.activeToolId?.startsWith('road.authoring.')) {
          toolStore.clearTool()
        }
        return
      }

      useRoadToolStore.getState().cancel()
      toolStore.activateTool({
        id: `road.authoring.${activeTool.slice('road.'.length)}`,
        workspaceId: 'roads',
        statusHint: 'Click in the viewport to place canonical project-coordinate construction points.',
        cancel: () => useAuthoringDraftStore.getState().setTool('select'),
        onViewportInteraction: (interaction) => {
          void handleViewportInteraction(interaction)
        },
      })
    }

    synchronizeAuthoringTool(useAuthoringDraftStore.getState().activeTool)
    return useAuthoringDraftStore.subscribe((state, previous) => {
      if (state.activeTool !== previous.activeTool) {
        synchronizeAuthoringTool(state.activeTool)
      }
    })
  }, [handleViewportInteraction])

  useEffect(() => window.infraforgeDesktop?.onViewportInteraction?.((interaction) => {
    const activeTool = useToolStore.getState()
    if (activeTool.viewportHandler) {
      activeTool.viewportHandler(interaction)
      return
    }

    const tool = useRoadToolStore.getState()
    if (interaction.kind === 'primary-click' && tool.mode === 'drawing') {
      useRoadToolStore.getState().append({
        easting: interaction.easting, northing: interaction.northing,
      })
    } else if (interaction.kind === 'primary-click' &&
      (tool.mode === 'move-control' || tool.mode === 'insert-control')) {
      const client = engineSessionRef.current?.client
      if (!client || !tool.editRoadId || tool.controlIndex === null) return
      const operation = tool.mode === 'move-control'
        ? moveRoadControl(client, tool.editRoadId, tool.controlIndex,
            interaction.easting, interaction.northing)
        : insertRoadControl(client, tool.editRoadId, tool.controlIndex,
            interaction.easting, interaction.northing)
      void operation.then(() => getRoad(client, tool.editRoadId!))
        .finally(() => useRoadToolStore.getState().cancel())
    } else if (interaction.kind === 'primary-click') {
      const selectionId = interaction.roadId ? `road:${interaction.roadId}` : null
      useSelectionStore.getState().select(selectionId ? [selectionId] : [])
    }
  }), [])

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
  const showHomeScreen = computeShowHomeScreen(projectOpen, activeWorkspace)

  // Blocking application overlays that render over the editor surface. The
  // native child-HWND viewport cannot be occluded by CSS z-index, so the
  // page reports this centrally and the desktop shell hides/restores the
  // native viewport (with a placement refresh) through its visibility
  // policy.
  //
  // The blocking signal includes `!projectOpen` (not `surfaceActive &&
  // !projectOpen`) so that `blockedByOverlay` is true from initial mount —
  // before the viewport process starts — rather than transitioning to true
  // only when the renderer reports ready. This avoids a race where the
  // desktop shell's post-readiness `applyViewportVisibilityPlan` runs
  // before the renderer's `setViewportVisible(false)` IPC round-trip
  // lands, which would briefly show the native surface over the home
  // screen.
  const blockingOverlayActive = computeBlockedByOverlay(openDialog, showHomeScreen, menuOpen)
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
      const unsubscribeRoad = subscribeRoadEvents(result.session.client)

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

      // Wire the road scene publisher: when road work settles, fetch the
      // updated road scene projection and forward it to the native viewport.
      setRoadScenePublisher(() => {
        void (async () => {
          const scene = await fetchRoadScene(result.session.client).catch(() => null)
          if (scene) {
            window.infraforgeDesktop?.setViewportScene?.(scene as Record<string, unknown>)
          }
        })()
      })

      const disposeSession = () => {
        unsubscribe()
        unsubscribeShell()
        unsubscribeTerrain()
        unsubscribeRoad()
        setTerrainScenePublisher(null)
        setRoadScenePublisher(null)
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

  const activeContextEditor = useActiveContextEditor()

  return (
    <div className="app-shell">
      <AppHeader context={commandContext} />
      <div className="app-main">
        <WorkspaceRail />
        <div className="app-editor-area">
          {projectOpen && activeWorkspace !== 'home' ? (
            <ContextToolShelf context={commandContext} />
          ) : null}
          {projectOpen && activeWorkspace === 'roads' ? (
            <ToolOptionsPanel onCommitPolyline={commitCurrentDraft} />
          ) : null}
          <div className="authoring-container">
            {projectOpen && activeWorkspace === 'roads' ? (
              <ToolRail />
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
            leftPanel={<Navigator />}
            contextEditor={activeContextEditor ? <ContextEditorHost /> : undefined}
            rightPanel={<Inspector />}
            bottomPanel={<BottomPanel />}
          />
          </div>
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
      {openDialog === 'export-terrain' && engineSession && projectOpen ? (
        <ExportTerrainDialog
          client={engineSession.client}
          busy={busy}
          onClose={() => closeDialog()}
        />
      ) : null}
      {openDialog === 'diagnostics' ? (
        <DiagnosticsDialog onClose={() => closeDialog()} />
      ) : null}
      {openDialog === 'create-road' && engineSession && projectOpen ? (
        <CreateRoadDialog
          client={engineSession.client}
          onClose={() => closeDialog()}
        />
      ) : null}
      {openDialog === 'rename-road' && engineSession && projectOpen ? (
        <RenameRoadDialog
          client={engineSession.client}
          onClose={() => closeDialog()}
        />
      ) : null}
    </div>
  )
}
