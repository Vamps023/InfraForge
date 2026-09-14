import { useSyncExternalStore } from 'react'
import { inspectorSectionRegistry } from './inspectorRegistry'
import { useSelectionStore } from '../selection/selectionStore'

// Inspector. Resolves registered sections against the current canonical
// selection and composes the applicable ones. Empty selection renders the
// project-level sections (e.g. project overview); unsupported selection
// renders an honest "no inspector available" state rather than fake
// properties.
//
// The inspector reactively subscribes to the section registry via
// useSyncExternalStore so it updates when sections are registered or
// unregistered after mount, and to the selection store so it updates on
// selection changes.
export function Inspector() {
  const selectedIds = useSelectionStore((state) => state.selectedIds)
  const primaryId = useSelectionStore((state) => state.primaryId)
  const sections = useSyncExternalStore(
    inspectorSectionRegistry.subscribe,
    inspectorSectionRegistry.getSnapshot,
  )
  const context = { selectedIds, primaryId }
  const applicable = sections.filter((section) => section.applies(context))

  return (
    <aside className="panel inspector-panel" aria-label="Inspector">
      <div className="panel-title-row">
        <span>Inspector</span>
      </div>
      <div className="inspector-body">
        {applicable.length === 0 ? (
          <div className="panel-empty">
            {selectedIds.length === 0
              ? 'Select an authored entity to inspect its properties.'
              : 'No inspector section is available for the current selection.'}
          </div>
        ) : (
          applicable.map((section) => (
            <section className="inspector-section" key={section.id}>
              <div className="inspector-section-title">{section.label}</div>
              <div className="inspector-section-body">{section.render(context)}</div>
            </section>
          ))
        )}
      </div>
    </aside>
  )
}
