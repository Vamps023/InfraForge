import type { ReactNode } from 'react'
import type { CanonicalId } from '../selection/selectionStore'

// Inspector section registry. Inspector sections are supplied based on
// selected canonical IDs/object projections. The shell does not hard-code
// every future domain into one giant inspector component; domains register
// sections and the inspector composes the ones that apply to the current
// selection.
//
// The registry is observable: the Inspector subscribes via
// useSyncExternalStore and re-renders when sections are registered or
// unregistered after mount — no stale caches.

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
  all() {
    return Array.from(sections.values()).sort(
      (a, b) => (a.order ?? 100) - (b.order ?? 100),
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
        (a, b) => (a.order ?? 100) - (b.order ?? 100),
      )
    }
    return snapshot
  },
}
