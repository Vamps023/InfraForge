import { useRef } from 'react'
import { useOperationsStore, activeOperations, recentOperations } from '../operations/operationsStore'
import { useProblemsStore } from '../problems/problemsStore'
import { useSelectionStore } from '../selection/selectionStore'
import { useUiStore } from '../../state/uiStore'

// Bottom panel with Problems and Operations tabs. Both tabs consume real
// backend-projected state; no fake jobs or sample warnings are rendered.
// Tabs use tablist/tab semantics with keyboard navigation (ArrowLeft/Right
// between tabs). Problem rows are focusable and keyboard-activatable.
export function BottomPanel() {
  const activeBottomTab = useUiStore((state) => state.activeBottomTab)
  const setActiveBottomTab = useUiStore((state) => state.setActiveBottomTab)
  const operations = useOperationsStore((state) => state.operations)
  const diagnostics = useProblemsStore((state) => state.diagnostics)
  const select = useSelectionStore((state) => state.select)
  const tabRefs = useRef<Record<string, HTMLButtonElement | null>>({})

  const active = activeOperations(operations)
  const recent = recentOperations(operations)

  const tabs = ['Problems', 'Operations'] as const
  const onTabKeyDown = (event: React.KeyboardEvent, currentTab: string) => {
    if (event.key === 'ArrowLeft' || event.key === 'ArrowRight') {
      event.preventDefault()
      const currentIndex = tabs.indexOf(currentTab as 'Problems' | 'Operations')
      const direction = event.key === 'ArrowLeft' ? -1 : 1
      const nextIndex = (currentIndex + direction + tabs.length) % tabs.length
      const nextTab = tabs[nextIndex]
      if (nextTab !== undefined) {
        setActiveBottomTab(nextTab)
        tabRefs.current[nextTab]?.focus()
      }
    }
  }

  return (
    <section className="bottom-panel">
      <div className="bottom-tabs" role="tablist" aria-label="Bottom panel tabs">
        <button
          ref={(el) => { tabRefs.current['Problems'] = el }}
          className={activeBottomTab === 'Problems' ? 'bottom-tab active' : 'bottom-tab'}
          type="button"
          role="tab"
          aria-selected={activeBottomTab === 'Problems'}
          tabIndex={activeBottomTab === 'Problems' ? 0 : -1}
          onClick={() => setActiveBottomTab('Problems')}
          onKeyDown={(e) => onTabKeyDown(e, 'Problems')}
        >
          Problems{diagnostics.length > 0 ? ` (${diagnostics.length})` : ''}
        </button>
        <button
          ref={(el) => { tabRefs.current['Operations'] = el }}
          className={activeBottomTab === 'Operations' ? 'bottom-tab active' : 'bottom-tab'}
          type="button"
          role="tab"
          aria-selected={activeBottomTab === 'Operations'}
          tabIndex={activeBottomTab === 'Operations' ? 0 : -1}
          onClick={() => setActiveBottomTab('Operations')}
          onKeyDown={(e) => onTabKeyDown(e, 'Operations')}
        >
          Operations{active.length > 0 ? ` (${active.length})` : ''}
        </button>
      </div>
      <div className="bottom-content">
        {activeBottomTab === 'Problems' ? (
          diagnostics.length === 0 ? (
            <span className="bottom-empty">No diagnostics.</span>
          ) : (
            <ul className="problem-list" role="list">
              {diagnostics.map((diagnostic) => (
                <li
                  key={diagnostic.id}
                  className={`problem-row problem-${diagnostic.severity}`}
                  tabIndex={0}
                  role="button"
                  aria-label={`${diagnostic.severity}: ${diagnostic.message} from ${diagnostic.source}`}
                  onClick={() => diagnostic.targetId && select([diagnostic.targetId], 'replace')}
                  onKeyDown={(event) => {
                    if (event.key === 'Enter' || event.key === ' ') {
                      event.preventDefault()
                      if (diagnostic.targetId) {
                        select([diagnostic.targetId], 'replace')
                      }
                    }
                  }}
                >
                  <span className="problem-severity">{diagnostic.severity}</span>
                  <span className="problem-source">{diagnostic.source}</span>
                  <span className="problem-message">{diagnostic.message}</span>
                </li>
              ))}
            </ul>
          )
        ) : (
          <div className="operations-list">
            {active.length === 0 && recent.length === 0 ? (
              <span className="bottom-empty">No operations are running.</span>
            ) : (
              <>
                {active.length > 0 ? (
                  <ul className="operation-list" role="list">
                    {active.map((op) => (
                      <li key={op.id} className="operation-row operation-active">
                        <span className="operation-name">{op.name}</span>
                        {op.progress !== null ? (
                          <span className="operation-progress">{Math.round(op.progress * 100)}%</span>
                        ) : null}
                        {op.processed !== undefined && op.total !== undefined ? (
                          <span className="operation-count">{op.processed}/{op.total}</span>
                        ) : null}
                        {op.cancellable ? (
                          <button className="operation-cancel" type="button" disabled>
                            Cancel
                          </button>
                        ) : null}
                      </li>
                    ))}
                  </ul>
                ) : null}
                {recent.length > 0 ? (
                  <ul className="operation-list operation-recent" role="list">
                    {recent.map((op) => (
                      <li key={op.id} className={`operation-row operation-${op.state}`}>
                        <span className="operation-name">{op.name}</span>
                        <span className="operation-state">{op.state}</span>
                        {op.message ? <span className="operation-message">{op.message}</span> : null}
                      </li>
                    ))}
                  </ul>
                ) : null}
              </>
            )}
          </div>
        )}
      </div>
    </section>
  )
}
