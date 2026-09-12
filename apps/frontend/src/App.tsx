import { useEffect, useRef, useState } from 'react'
import { Box, ChevronDown, CircleDot, PanelBottom, PanelLeft, PanelRight, Search } from 'lucide-react'
import { connectEngineSession, type EngineSession, type EngineSessionStatus } from './lib/engineSession'
import { GeoreferencePanel } from './features/geo/GeoreferencePanel'
import { NewProjectDialog } from './features/project/NewProjectDialog'
import { closeProject, openProject, refreshProjectSummary, saveProject } from './features/project/projectApi'
import { subscribeProjectEvents } from './features/project/projectEvents'
import { useProjectStore } from './features/project/projectStore'
import { useUiStore } from './state/uiStore'

const bottomTabs = ['Problems', 'Operations'] as const

type BottomTab = (typeof bottomTabs)[number]

function EmptyViewport({
  engineReady,
  projectOpen,
  busy,
  onNewProject,
  onOpenProject,
}: {
  engineReady: boolean
  projectOpen: boolean
  busy: boolean
  onNewProject: () => void
  onOpenProject: () => void
}) {
  return (
    <main className="viewport-empty" aria-label="Viewport">
      <div className="viewport-grid" aria-hidden="true" />
      <div className="empty-state">
        <div className="empty-state-icon">
          <Box size={22} />
        </div>
        {projectOpen ? (
          <>
            <h1>Viewport unavailable</h1>
            <p>The project is open, but the native Vulkan viewport is not implemented yet (issue #2).</p>
          </>
        ) : (
          <>
            <h1>No project open</h1>
            <p>Create an InfraForge project or open an existing .iforge directory.</p>
            <div className="empty-state-actions">
              <button className="button primary" type="button" disabled={!engineReady || busy} onClick={onNewProject}>
                New Project…
              </button>
              <button className="button secondary" type="button" disabled={!engineReady || busy} onClick={onOpenProject}>
                Open Project…
              </button>
            </div>
            {!engineReady ? (
              <p className="empty-state-note">Project actions require a connected native engine session.</p>
            ) : null}
          </>
        )}
      </div>
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
      <div className="build-label">Foundation 0.2.0</div>
    </header>
  )
}

function Toolbar() {
  return (
    <div className="toolbar" aria-label="Editor toolbar">
      <button className="tool-button active" type="button" aria-pressed="true">
        Select
      </button>
      <div className="toolbar-separator" />
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
      <span className="status-item">Renderer —</span>
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
      <Toolbar />
      <div className="workspace">
        <Outliner />
        <EmptyViewport
          engineReady={engineReady}
          projectOpen={projectOpen}
          busy={busy}
          onNewProject={handleNewProject}
          onOpenProject={() => void handleOpenProject()}
        />
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
