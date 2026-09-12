import { create } from 'zustand'
import type { Diagnostic } from '@infraforge/protocol'

// UI-only projection of validation diagnostics from the native engine
// (ADR-0009). The frontend never computes diagnostics itself; it receives
// structured results from `world.check` and projects them for display.
interface ValidationUiState {
  // Diagnostics from the last completed `world.check` run, ordered as the
  // engine produced them. Empty when no run has completed or the run found
  // no diagnostics.
  diagnostics: Diagnostic[]
  // Project revision the diagnostics were computed against.
  revision: number | null
  // True when the last run was cancelled before all validators completed.
  cancelled: boolean
  // True while a `world.check` command is in flight.
  checking: boolean
  setDiagnostics: (diagnostics: Diagnostic[], revision: number, cancelled: boolean) => void
  setChecking: (checking: boolean) => void
  clear: () => void
}

export const useValidationStore = create<ValidationUiState>((set) => ({
  diagnostics: [],
  revision: null,
  cancelled: false,
  checking: false,
  setDiagnostics: (diagnostics, revision, cancelled) => set({ diagnostics, revision, cancelled, checking: false }),
  setChecking: (checking) => set({ checking }),
  clear: () => set({ diagnostics: [], revision: null, cancelled: false, checking: false }),
}))
