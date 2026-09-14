import { useSyncExternalStore, useState } from 'react'
import { ChevronDown, ChevronRight } from 'lucide-react'
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
//
// Sections are collapsible — each header toggles its body visibility.
// Sections start expanded. The collapse state is local UI state (ADR-0009);
// it is not project/domain state.
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
            <CollapsibleSection key={section.id} label={section.label}>
              {section.render(context)}
            </CollapsibleSection>
          ))
        )}
      </div>
    </aside>
  )
}

function CollapsibleSection({
  label,
  children,
}: {
  label: string
  children: React.ReactNode
}) {
  const [expanded, setExpanded] = useState(true)
  const sectionId = `inspector-section-${label.replace(/\s+/g, '-').toLowerCase()}`

  return (
    <section className="inspector-section">
      <button
        type="button"
        className="inspector-section-title"
        aria-expanded={expanded}
        aria-controls={sectionId}
        onClick={() => setExpanded(!expanded)}
      >
        {expanded ? <ChevronDown size={12} /> : <ChevronRight size={12} />}
        <span>{label}</span>
      </button>
      {expanded ? (
        <div className="inspector-section-body" id={sectionId}>
          {children}
        </div>
      ) : null}
    </section>
  )
}
