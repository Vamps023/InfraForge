import { useEffect, useRef, useState } from 'react'
import { Box, ChevronDown, CircleDot, FolderOpen, PanelBottom, PanelLeft, PanelRight, Search } from 'lucide-react'
import { connectEngineSession, type EngineSession, type EngineSessionStatus } from './lib/engineSession'
import { GeoreferencePanel } from './features/geo/GeoreferencePanel'
import { NewProjectDialog } from './features/project/NewProjectDialog'
import { closeProject, openProject, refreshProjectSummary, saveProject } from './features/project/projectApi'
import { subscribeProjectEvents } from './features/project/projectEvents'
import { useProjectStore } from './features/project/projectStore'
import { useViewportHost } from './features/viewport/useViewportHost'
import { useViewportStore, viewportSurfaceActive } from './features/viewport/viewportStore'
import { useUiStore } from './state/uiStore'

const bottomTabs = ['Problems', 'Operations'] as const

type BottomTab = (typeof bottomTabs)[number]

function ViewportArea({ hostRef }: { hostRef: React.RefObject<HTMLDivElement | null> }) {
  const rendererStatus = useViewportStore((state) => state.status)
  const surfaceActive = viewportSurfaceActive(rendererStatus.state)

  return (
    <main className="viewport-area" aria-label="Viewport">
      <div className="viewport-host" ref={hostRef} />
      {!surfaceActive ? (
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

function AppHeader({
  projectOpen,
  busy,
  engineReady,
  onSave,
  onGeoreference,
  onClose,
}: {
  projectOpen: boolean
  busy: boolean
  engineReady: boolean
  onSave: () => void
  onGeoreference: () => void
  onClose: () => void
}) {
  const summary = useProjectStore((state) => state.summary)
  return (
    <header className="app-header">
      <div className="brand">
        <span className="brand-mark">IF</span>
        <span>InfraForge</span>
      </div>
      <div className="project-chip" title={summary?.directory}>
        {summary ? summary.displayName : 'No project open'}
      </div>
      <div className="header-spacer" />
      {projectOpen ? (
        <div className="header-actions">
          <button className="button secondary" type="button" disabled={!engineReady || busy} onClick={onSave}>
            Save
          </button>
          <button className="button secondary" type="button" disabled={!engineReady || busy} onClick={onGeoreference}>
            Georeference…
          </button>
          <button className="button secondary" type="button" disabled={!engineReady || busy} onClick={onClose}>
            Close
          </button>
        </div>
      ) : null}
      <div className="build-label">Foundation 0.3.0</div>
    </header>
  )
}

function Toolbar({
  engineReady,
  busy,
  onNewProject,
  onOpenProject,
}: {
  engineReady: boolean
  busy: boolean
  onNewProject: () => void
  onOpenProject: () => void
}) {
  return (
    <div className="toolbar" aria-label="Editor toolbar">
      <button className="tool-button active" type="button" aria-pressed="true">
        Select
      </button>
      <div className="toolbar-separator" />
      <button className="tool-button" type="button" disabled={!engineReady || busy} onClick={onNewProject}>
        New Project…
      </button>
      <button className="tool-button" type="button" disabled={!engineReady || busy} onClick={onOpenProject}>
        <FolderOpen size={13} /> Open Project…
      </button>
      <div className="toolbar-spacer" />
      <span className="toolbar-hint">Authoring tools appear only when their production domain is available.</span>
    </div>
  )
}

function Outliner() {
  return (
    <aside className="panel outliner-panel">
      <div className="panel-title-row">
        <span>Outliner</span>
        <PanelLeft size={14} />
      </div>
      <div className="search-box">
        <Search size={14} />
        <input aria-label="Search outliner" placeholder="Search" disabled />
      </div>
      <div className="panel-empty">Open a project to inspect world entities.</div>
    </aside>
  )
}

function Inspector() {
  return (
    <aside className="panel inspector-panel">
      <div className="panel-title-row">
        <span>Inspector</span>
        <PanelRight size={14} />
      </div>
      <div className="panel-empty">Select an authored entity to inspect its properties.</div>
    </aside>
  )
}

function BottomPanel({ activeTab, setActiveTab }: { activeTab: BottomTab; setActiveTab: (tab: BottomTab) => void }) {
  const lastError = useProjectStore((state) => state.lastError)
  const rendererStatus = useViewportStore((state) => state.status)
  const rendererProblem =
    rendererStatus.state === 'failed' || rendererStatus.state === 'stopped'
      ? `Viewport: ${rendererStatus.detail}`
      : null
  return (
    <section className="bottom-panel">
      <div className="bottom-tabs">
        {bottomTabs.map((tab) => (
          <button
            key={tab}
            className={activeTab === tab ? 'bottom-tab active' : 'bottom-tab'}
            type="button"
            onClick={() => setActiveTab(tab)}
          >
            {tab}
          </button>
        ))}
        <div className="bottom-spacer" />
        <PanelBottom size={14} />
      </div>
      <div className="bottom-content">
        {activeTab === 'Problems' ? (
          lastError ? (
            <span className="problem-row">{lastError.message}</span>
          ) : rendererProblem ? (
            <span className="problem-row">{rendererProblem}</span>
          ) : (
            'No diagnostics.'
          )
        ) : (
          'No operations are running.'
        )}
      </div>
    </section>
  )
}

function StatusBar({ engineStatus }: { engineStatus: EngineSessionStatus }) {
  const summary = useProjectStore((state) => state.summary)
  const rendererStatus = useViewportStore((state) => state.status)
  const rendererTitle = [
    rendererStatus.detail,
    rendererStatus.gpu ? `GPU: ${rendererStatus.gpu}` : null,
    rendererStatus.vulkan ? `Vulkan ${rendererStatus.vulkan}` : null,
    rendererStatus.validation ? 'Validation enabled' : null,
  ]
    .filter(Boolean)
    .join(' — ')
  return (
    <footer className="status-bar">
      <span className={`status-item engine-${engineStatus.state}`} title={engineStatus.message}>
        <CircleDot size={12} /> {engineStatus.message}
      </span>
      <span className="status-divider" />
      {summary ? (
        <>
          <span className="status-item">Project revision {summary.revision}</span>
          {summary.dirty ? <span className="status-item status-dirty">● unsaved</span> : null}
          <span className="status-divider" />
          <span className="status-item" title={summary.georeference?.horizontalCrs ?? undefined}>
            CRS {summary.georeference?.horizontalCrs || '—'}
          </span>
        </>
      ) : (
        <>
          <span className="status-item">Project revision —</span>
          <span className="status-item">CRS —</span>
        </>
      )}
      <div className="status-spacer" />
      <span className={`status-item renderer-${rendererStatus.state}`} title={rendererTitle}>
        Renderer {rendererStatus.state}
        {rendererStatus.gpu ? ` · ${rendererStatus.gpu}` : ''}
      </span>
      <ChevronDown size={12} />
    </footer>
  )
}

export function App() {
  const activeBottomTab = useUiStore((state) => state.activeBottomTab)
  const setActiveBottomTab = useUiStore((state) => state.setActiveBottomTab)
  const engineStatus = useUiStore((state) => state.engineStatus)
  const setEngineStatus = useUiStore((state) => state.setEngineStatus)
  const [engineSession, setEngineSession] = useState<EngineSession | null>(null)
  const [showNewProjectDialog, setShowNewProjectDialog] = useState(false)
  const [showGeoreferencePanel, setShowGeoreferencePanel] = useState(false)
  const disposersRef = useRef<(() => void) | null>(null)
  const viewportHostRef = useRef<HTMLDivElement | null>(null)

  // Blocking application overlays that render over the editor surface. The
  // native child-HWND viewport cannot be occluded by CSS z-index, so the
  // page reports this centrally and the desktop shell hides/restores the
  // native viewport (with a placement refresh) through its visibility
  // policy.
  const blockingOverlayActive = showNewProjectDialog || showGeoreferencePanel
  useViewportHost(viewportHostRef, { blockedByOverlay: blockingOverlayActive })

  const summary = useProjectStore((state) => state.summary)
  const operation = useProjectStore((state) => state.operation)
  const projectOpen = summary !== null
  const engineReady = engineStatus.state === 'ready' && engineSession !== null
  const busy = operation !== null

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
      const disposeSession = () => {
        unsubscribe()
        result.session.dispose()
      }

      if (cancelled) {
        disposeSession()
        return
      }
      disposersRef.current = disposeSession
      setEngineSession(result.session)

      await refreshProjectSummary(result.session.client).catch(() => undefined)
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

  const handleNewProject = () => setShowNewProjectDialog(true)

  const handleOpenProject = async () => {
    if (!engineSession) {
      return
    }
    const desktop = window.infraforgeDesktop
    if (!desktop?.pickDirectory) {
      useProjectStore.getState().setLastError({
        code: '4',
        message: 'The desktop shell did not expose a directory picker.',
      })
      return
    }
    const selected = await desktop.pickDirectory({
      title: 'Open an InfraForge project directory',
      buttonLabel: 'Open Project',
    })
    if (!selected) {
      return
    }
    await openProject(engineSession.client, selected).catch(() => undefined)
  }

  const handleSave = async () => {
    if (!engineSession) {
      return
    }
    await saveProject(engineSession.client).catch(() => undefined)
  }

  const handleClose = async () => {
    if (!engineSession) {
      return
    }
    await closeProject(engineSession.client).catch(() => undefined)
  }

  return (
    <div className="app-shell">
      <AppHeader
        projectOpen={projectOpen}
        busy={busy}
        engineReady={engineReady}
        onSave={() => void handleSave()}
        onGeoreference={() => setShowGeoreferencePanel(true)}
        onClose={() => void handleClose()}
      />
      <Toolbar
        engineReady={engineReady}
        busy={busy}
        onNewProject={handleNewProject}
        onOpenProject={() => void handleOpenProject()}
      />
      <div className="workspace">
        <Outliner />
        <ViewportArea hostRef={viewportHostRef} />
        <Inspector />
      </div>
      <BottomPanel activeTab={activeBottomTab} setActiveTab={setActiveBottomTab} />
      <StatusBar engineStatus={engineStatus} />
      {showNewProjectDialog && engineSession ? (
        <NewProjectDialog
          client={engineSession.client}
          busy={busy}
          onClose={() => setShowNewProjectDialog(false)}
        />
      ) : null}
      {showGeoreferencePanel && engineSession && projectOpen ? (
        <GeoreferencePanel
          client={engineSession.client}
          onClose={() => setShowGeoreferencePanel(false)}
        />
      ) : null}
    </div>
  )
}
