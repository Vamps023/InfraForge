import { useOperationsStore, activeOperations, recentOperations } from '../operations/operationsStore'
import { useProblemsStore } from '../problems/problemsStore'
import { useSelectionStore } from '../selection/selectionStore'
import { useUiStore } from '../../state/uiStore'

// Bottom panel with Problems and Operations tabs. Both tabs consume real
// backend-projected state; no fake jobs or sample warnings are rendered.
export function BottomPanel() {
  const activeBottomTab = useUiStore((state) => state.activeBottomTab)
  const setActiveBottomTab = useUiStore((state) => state.setActiveBottomTab)
  const operations = useOperationsStore((state) => state.operations)
  const diagnostics = useProblemsStore((state) => state.diagnostics)
  const select = useSelectionStore((state) => state.select)

  const active = activeOperations(operations)
  const recent = recentOperations(operations)

  return (
    <section className="bottom-panel">
      <div className="bottom-tabs">
        <button
          className={activeBottomTab === 'Problems' ? 'bottom-tab active' : 'bottom-tab'}
          type="button"
          onClick={() => setActiveBottomTab('Problems')}
        >
          Problems{diagnostics.length > 0 ? ` (${diagnostics.length})` : ''}
        </button>
        <button
          className={activeBottomTab === 'Operations' ? 'bottom-tab active' : 'bottom-tab'}
          type="button"
          onClick={() => setActiveBottomTab('Operations')}
        >
          Operations{active.length > 0 ? ` (${active.length})` : ''}
        </button>
      </div>
      <div className="bottom-content">
        {activeBottomTab === 'Problems' ? (
          diagnostics.length === 0 ? (
            <span className="bottom-empty">No diagnostics.</span>
          ) : (
            <ul className="problem-list">
              {diagnostics.map((diagnostic) => (
                <li
                  key={diagnostic.id}
                  className={`problem-row problem-${diagnostic.severity}`}
                  onClick={() => diagnostic.targetId && select([diagnostic.targetId], 'replace')}
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
                  <ul className="operation-list">
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
                  <ul className="operation-list operation-recent">
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
