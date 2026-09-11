import { useEffect } from 'react'
import { Box, ChevronDown, CircleDot, PanelBottom, PanelLeft, PanelRight, Search } from 'lucide-react'
import { connectEngineSession } from './lib/engineSession'
import { useUiStore } from './state/uiStore'

const bottomTabs = ['Problems', 'Operations'] as const

type BottomTab = (typeof bottomTabs)[number]

function EmptyViewport() {
  return (
    <main className="viewport-empty" aria-label="Viewport">
      <div className="viewport-grid" aria-hidden="true" />
      <div className="empty-state">
        <div className="empty-state-icon"><Box size={22} /></div>
        <h1>No project open</h1>
        <p>Project lifecycle and native Vulkan viewport are intentionally unavailable until their real production paths are implemented.</p>
      </div>
    </main>
  )
}

function AppHeader() {
  return (
    <header className="app-header">
      <div className="brand"><span className="brand-mark">IF</span><span>InfraForge</span></div>
      <div className="project-chip">No project open</div>
      <div className="header-spacer" />
      <div className="build-label">Foundation 0.1.0</div>
    </header>
  )
}

function Toolbar() {
  return (
    <div className="toolbar" aria-label="Editor toolbar">
      <button className="tool-button active" type="button" aria-pressed="true">Select</button>
      <div className="toolbar-separator" />
      <span className="toolbar-hint">Authoring tools appear only when their production domain is available.</span>
    </div>
  )
}

function Outliner() {
  return (
    <aside className="panel outliner-panel">
      <div className="panel-title-row"><span>Outliner</span><PanelLeft size={14} /></div>
      <div className="search-box"><Search size={14} /><input aria-label="Search outliner" placeholder="Search" disabled /></div>
      <div className="panel-empty">Open a project to inspect world entities.</div>
    </aside>
  )
}

function Inspector() {
  return (
    <aside className="panel inspector-panel">
      <div className="panel-title-row"><span>Inspector</span><PanelRight size={14} /></div>
      <div className="panel-empty">Select an authored entity to inspect its properties.</div>
    </aside>
  )
}

function BottomPanel({ activeTab, setActiveTab }: { activeTab: BottomTab; setActiveTab: (tab: BottomTab) => void }) {
  return (
    <section className="bottom-panel">
      <div className="bottom-tabs">
        {bottomTabs.map((tab) => (
          <button key={tab} className={activeTab === tab ? 'bottom-tab active' : 'bottom-tab'} type="button" onClick={() => setActiveTab(tab)}>
            {tab}
          </button>
        ))}
        <div className="bottom-spacer" />
        <PanelBottom size={14} />
      </div>
      <div className="bottom-content">
        {activeTab === 'Problems' ? 'No diagnostics.' : 'No operations are running.'}
      </div>
    </section>
  )
}

function StatusBar() {
  const engineStatus = useUiStore((state) => state.engineStatus)
  return (
    <footer className="status-bar">
      <span className={`status-item engine-${engineStatus.state}`} title={engineStatus.message}>
        <CircleDot size={12} /> {engineStatus.message}
      </span>
      <span className="status-divider" />
      <span className="status-item">Project revision —</span>
      <div className="status-spacer" />
      <span className="status-item">CRS —</span>
      <span className="status-item">Renderer —</span>
      <ChevronDown size={12} />
    </footer>
  )
}

export function App() {
  const activeBottomTab = useUiStore((state) => state.activeBottomTab)
  const setActiveBottomTab = useUiStore((state) => state.setActiveBottomTab)
  const setEngineStatus = useUiStore((state) => state.setEngineStatus)

  useEffect(() => {
    let dispose: (() => void) | undefined
    let cancelled = false

    void connectEngineSession((status) => {
      if (!cancelled) {
        setEngineStatus(status)
      }
    }).then((cleanup) => {
      if (cancelled) {
        cleanup()
      } else {
        dispose = cleanup
      }
    }).catch((error: unknown) => {
      if (!cancelled) {
        setEngineStatus({ state: 'failed', message: error instanceof Error ? error.message : String(error) })
      }
    })

    return () => {
      cancelled = true
      dispose?.()
    }
  }, [setEngineStatus])

  return (
    <div className="app-shell">
      <AppHeader />
      <Toolbar />
      <div className="workspace">
        <Outliner />
        <EmptyViewport />
        <Inspector />
      </div>
      <BottomPanel activeTab={activeBottomTab} setActiveTab={setActiveBottomTab} />
      <StatusBar />
    </div>
  )
}
