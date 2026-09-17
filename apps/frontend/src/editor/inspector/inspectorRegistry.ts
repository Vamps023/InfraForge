import type { ReactNode } from 'react'
import type { CanonicalId } from '../selection/selectionStore'

// Inspector section registry. Inspector sections are supplied based on
// selected canonical IDs/object projections.
// Standard order conforming to docs/06_UI_UX/DESIGN_SYSTEM.md and UX_SPEC.md:
// Identity -> Geometry -> Semantics -> Appearance -> Connections -> Source/Provenance -> Export -> Diagnostics

export type InspectorSectionCategory =
  | 'identity'
  | 'geometry'
  | 'semantics'
  | 'appearance'
  | 'connections'
  | 'source'
  | 'export'
  | 'diagnostics'

export const SECTION_CATEGORY_ORDER: Record<InspectorSectionCategory, number> = {
  identity: 10,
  geometry: 20,
  semantics: 30,
  appearance: 40,
  connections: 50,
  source: 60,
  export: 70,
  diagnostics: 80,
}

export interface InspectorSectionContext {
  // All currently selected canonical IDs.
  selectedIds: CanonicalId[]
  // Primary selection (the last-clicked or anchor ID), or null for empty.
  primaryId: CanonicalId | null
}

export interface InspectorSection {
  // Unique section id, e.g. 'project-overview', 'road-geometry'.
  id: string
  // Display label for the section header.
  label: string
  // Category mapping to the standard 8-tier inspector order.
  category?: InspectorSectionCategory
  // Optional ordering hint within the category; lower renders first. Defaults to 0.
  order?: number
  // Returns true when this section should render for the current selection.
  applies: (context: InspectorSectionContext) => boolean
  // Renders the section body. Receives the selection context.
  render: (context: InspectorSectionContext) => ReactNode
}

function resolveOrder(section: InspectorSection): number {
  const baseOrder = section.category ? SECTION_CATEGORY_ORDER[section.category] : 100
  return baseOrder + (section.order ?? 0)
}

const sections = new Map<string, InspectorSection>()
const listeners = new Set<() => void>()
let snapshot: InspectorSection[] | null = null

function notify(): void {
  snapshot = null
  for (const listener of listeners) {
    listener()
  }
}

export interface InspectorSectionRegistry {
  register: (section: InspectorSection) => void
  unregister: (id: string) => void
  get: (id: string) => InspectorSection | undefined
  all: () => InspectorSection[]
  resolve: (context: InspectorSectionContext) => InspectorSection[]
  subscribe: (listener: () => void) => () => void
  getSnapshot: () => InspectorSection[]
}

export const inspectorSectionRegistry: InspectorSectionRegistry = {
  register(section) {
    if (sections.has(section.id)) {
      throw new Error(`Duplicate inspector section id: ${section.id}`)
    }
    sections.set(section.id, section)
    notify()
  },
  unregister(id) {
    if (sections.delete(id)) {
      notify()
    }
  },
  get(id) {
    return sections.get(id)
  },
  all() {
    return Array.from(sections.values()).sort(
      (a, b) => resolveOrder(a) - resolveOrder(b),
    )
  },
  resolve(context) {
    return inspectorSectionRegistry.all().filter((section) => section.applies(context))
  },
  subscribe(listener) {
    listeners.add(listener)
    return () => {
      listeners.delete(listener)
    }
  },
  getSnapshot() {
    if (snapshot === null) {
      snapshot = Array.from(sections.values()).sort(
        (a, b) => resolveOrder(a) - resolveOrder(b),
      )
    }
    return snapshot
  },
}
