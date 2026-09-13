import { inspectorSectionRegistry } from './inspectorRegistry'
import { useSelectionStore } from '../selection/selectionStore'

// Inspector. Resolves registered sections against the current canonical
// selection and composes the applicable ones. Empty selection renders the
// project-level sections (e.g. project overview); unsupported selection
// renders an honest "no inspector available" state rather than fake
// properties.
export function Inspector() {
  const selectedIds = useSelectionStore((state) => state.selectedIds)
  const primaryId = useSelectionStore((state) => state.primaryId)
  const context = { selectedIds, primaryId }
  const sections = inspectorSectionRegistry.resolve(context)

  return (
    <aside className="panel inspector-panel" aria-label="Inspector">
      <div className="panel-title-row">
        <span>Inspector</span>
      </div>
      <div className="inspector-body">
        {sections.length === 0 ? (
          <div className="panel-empty">
            {selectedIds.length === 0
              ? 'Select an authored entity to inspect its properties.'
              : 'No inspector section is available for the current selection.'}
          </div>
        ) : (
          sections.map((section) => (
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
