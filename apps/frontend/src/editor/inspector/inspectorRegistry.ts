import type { ReactNode } from 'react'
import type { CanonicalId } from '../selection/selectionStore'

// Inspector section registry. Inspector sections are supplied based on
// selected canonical IDs/object projections. The shell does not hard-code
// every future domain into one giant inspector component; domains register
// sections and the inspector composes the ones that apply to the current
// selection.
//
// A section declares which canonical IDs/object types it can inspect. The
// inspector resolves sections by asking each registered section whether it
// applies to the current selection, then renders the applicable sections in
// registration order.

export interface InspectorSectionContext {
  // All currently selected canonical IDs.
  selectedIds: CanonicalId[]
  // Primary selection (the last-clicked or anchor ID), or null for empty.
  primaryId: CanonicalId | null
}

export interface InspectorSection {
  // Unique section id, e.g. 'project-overview', 'road-properties'.
  id: string
  // Display label for the section header.
  label: string
  // Optional ordering hint; lower renders first. Defaults to 100.
  order?: number
  // Returns true when this section should render for the current selection.
  // Sections that apply to no selection (e.g. project-level overview) return
  // true when selectedIds is empty.
  applies: (context: InspectorSectionContext) => boolean
  // Renders the section body. Receives the selection context so the body
  // can read projections for the selected canonical IDs.
  render: (context: InspectorSectionContext) => ReactNode
}

interface InspectorSectionRegistry {
  sections: Map<string, InspectorSection>
  register: (section: InspectorSection) => void
  unregister: (id: string) => void
  all: () => InspectorSection[]
  resolve: (context: InspectorSectionContext) => InspectorSection[]
}

const inspectorRegistry: InspectorSectionRegistry = {
  sections: new Map<string, InspectorSection>(),
  register(section) {
    if (inspectorRegistry.sections.has(section.id)) {
      throw new Error(`Duplicate inspector section id: ${section.id}`)
    }
    inspectorRegistry.sections.set(section.id, section)
  },
  unregister(id) {
    inspectorRegistry.sections.delete(id)
  },
  all() {
    return Array.from(inspectorRegistry.sections.values()).sort(
      (a, b) => (a.order ?? 100) - (b.order ?? 100),
    )
  },
  resolve(context) {
    return inspectorRegistry.all().filter((section) => section.applies(context))
  },
}

export const inspectorSectionRegistry = inspectorRegistry
