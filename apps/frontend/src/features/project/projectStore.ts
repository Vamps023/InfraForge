import { create } from 'zustand'
import type { ProjectSummary } from '@infraforge/protocol'

// UI-only projection of the native engine's canonical project session
// (ADR-0009). The summary is refreshed from command results and engine
// events; it is never mutated locally as canonical state.
export type ProjectOperation = 'creating' | 'opening' | 'saving' | 'savingAs' | 'closing'

interface ProjectUiState {
  summary: ProjectSummary | null
  operation: ProjectOperation | null
  lastError: { code: string; message: string } | null
  setSummary: (summary: ProjectSummary | null) => void
  patchSummary: (patch: Partial<ProjectSummary>) => void
  setOperation: (operation: ProjectOperation | null) => void
  setLastError: (error: { code: string; message: string } | null) => void
  clearProject: () => void
}

export const useProjectStore = create<ProjectUiState>((set) => ({
  summary: null,
  operation: null,
  lastError: null,
  setSummary: (summary) => set({ summary }),
  patchSummary: (patch) =>
    set((state) => (state.summary ? { summary: { ...state.summary, ...patch } } : state)),
  setOperation: (operation) => set({ operation }),
  setLastError: (lastError) => set({ lastError }),
  clearProject: () => set({ summary: null, operation: null }),
}))
