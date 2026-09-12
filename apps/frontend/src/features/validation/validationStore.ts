import { create } from 'zustand'
import type { Diagnostic, DiagnosticEntityRef } from '@infraforge/protocol'

// UI-only projection of validation diagnostics from the native engine
// (ADR-0009). The frontend never computes diagnostics itself; it receives
// structured results from `world.check` and projects them for display.
//
// Diagnostic identity: a diagnostic is identified by (code, entities), not
// by code alone. Multiple entities may produce the same code (e.g.
// `road.geometry.self_intersection` on two roads). The store keys
// diagnostics by their identity string to prevent duplicates.
interface ValidationUiState {
  // Diagnostics from the last completed `world.check` run, ordered as the
  // engine produced them. Empty when no run has completed or the run found
  // no diagnostics.
  diagnostics: Diagnostic[]
  // Project revision the diagnostics were computed against.
  revision: number | null
  // True when the last run was cancelled before all validators completed.
  cancelled: boolean
  // True when the diagnostics are stale because the project revision has
  // changed since the last run. The user should re-run Check World.
  stale: boolean
  // True while a `world.check` command is in flight.
  checking: boolean
  setDiagnostics: (diagnostics: Diagnostic[], revision: number, cancelled: boolean, stale: boolean) => void
  setChecking: (checking: boolean) => void
  addDiagnostic: (diagnostic: Diagnostic) => void
  removeDiagnostic: (code: string, entities: DiagnosticEntityRef[]) => void
  clear: () => void
}

// Computes a deterministic identity string from a diagnostic's code and
// entities. This mirrors the native `Diagnostic::diagnosticIdentity()` in
// the engine. The message is never part of identity.
export function diagnosticIdentity(code: string, entities: DiagnosticEntityRef[]): string {
  if (entities.length === 0) {
    return code
  }
  const sorted = [...entities].sort((a, b) => {
    if (a.kind !== b.kind) return a.kind < b.kind ? -1 : 1
    return a.id < b.id ? -1 : 1
  })
  const entityPart = sorted.map((e) => `${e.kind}:${e.id}`).join(',')
  return `${code}|${entityPart}`
}

export const useValidationStore = create<ValidationUiState>((set) => ({
  diagnostics: [],
  revision: null,
  cancelled: false,
  stale: false,
  checking: false,
  setDiagnostics: (diagnostics, revision, cancelled, stale) =>
    set({ diagnostics, revision, cancelled, stale, checking: false }),
  setChecking: (checking) => set({ checking }),
  addDiagnostic: (diagnostic) =>
    set((state) => {
      const id = diagnosticIdentity(diagnostic.code, diagnostic.entities)
      // Prevent duplicates: if a diagnostic with the same identity already
      // exists, replace it (the engine may re-emit with updated fields).
      const filtered = state.diagnostics.filter(
        (d) => diagnosticIdentity(d.code, d.entities) !== id,
      )
      return { diagnostics: [...filtered, diagnostic] }
    }),
  removeDiagnostic: (code, entities) =>
    set((state) => {
      const id = diagnosticIdentity(code, entities)
      return {
        diagnostics: state.diagnostics.filter(
          (d) => diagnosticIdentity(d.code, d.entities) !== id,
        ),
      }
    }),
  clear: () => set({ diagnostics: [], revision: null, cancelled: false, stale: false, checking: false }),
}))
