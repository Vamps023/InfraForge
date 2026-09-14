import { create } from 'zustand'

// Canonical selection identity for the editor shell. Selection is expressed
// as stable canonical IDs owned by the native engine (ADR-0009). The frontend
// never substitutes object identity for canonical identity: a selected entity
// is referenced by the same ID the engine uses in its projections/events, so
// 2D/3D views and the outliner/inspector all resolve to the same world object.
//
// No domain objects exist yet, so production selection is empty. The store is
// the single selection surface for the shell, outliner, inspector, and any
// future command that acts on the current selection.

export type CanonicalId = string

export type SelectionMode = 'replace' | 'add' | 'toggle'

interface SelectionState {
  selectedIds: CanonicalId[]
  primaryId: CanonicalId | null
  select: (ids: CanonicalId[], mode?: SelectionMode) => void
  toggle: (id: CanonicalId) => void
  clear: () => void
  setPrimary: (id: CanonicalId | null) => void
}

export const useSelectionStore = create<SelectionState>((set) => ({
  selectedIds: [],
  primaryId: null,
  select: (ids, mode = 'replace') =>
    set((state) => {
      if (mode === 'replace') {
        const next = Array.from(new Set(ids))
        return {
          selectedIds: next,
          primaryId: next.length > 0 ? (next[next.length - 1] ?? null) : null,
        }
      }
      if (mode === 'add') {
        const merged = new Set(state.selectedIds)
        for (const id of ids) {
          merged.add(id)
        }
        const next = Array.from(merged)
        return { selectedIds: next, primaryId: next.length > 0 ? (next[next.length - 1] ?? null) : state.primaryId }
      }
      // toggle
      const merged = new Set(state.selectedIds)
      let primary = state.primaryId
      for (const id of ids) {
        if (merged.has(id)) {
          merged.delete(id)
          if (primary === id) {
            primary = state.selectedIds.find((other) => other !== id && merged.has(other)) ?? null
          }
        } else {
          merged.add(id)
          primary = id
        }
      }
      const next = Array.from(merged)
      return { selectedIds: next, primaryId: primary }
    }),
  toggle: (id) =>
    set((state) => {
      const merged = new Set(state.selectedIds)
      let primary = state.primaryId
      if (merged.has(id)) {
        merged.delete(id)
        if (primary === id) {
          primary = merged.size > 0 ? (merged.values().next().value ?? null) : null
        }
      } else {
        merged.add(id)
        primary = id
      }
      return { selectedIds: Array.from(merged), primaryId: primary }
    }),
  clear: () => set({ selectedIds: [], primaryId: null }),
  setPrimary: (id) =>
    set((state) => (id === null || state.selectedIds.includes(id) ? { primaryId: id } : state)),
}))
