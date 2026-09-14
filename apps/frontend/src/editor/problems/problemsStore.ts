import { create } from 'zustand'
import type { CanonicalId } from '../selection/selectionStore'

// Problems/Diagnostics store, driven by backend events/projections. No static
// sample warnings are created. The store starts empty and gains entries only
// when the backend projects diagnostics, or when a real frontend-owned
// surface (e.g. the engine session failing) reports a diagnostic.
//
// Diagnostics carry stable identity when the backend supplies it, plus a
// canonical object ID for navigation/selection hooks.

export type DiagnosticSeverity = 'error' | 'warning' | 'info'

export interface DiagnosticProjection {
  // Stable diagnostic identity when the backend supplies it; otherwise a
  // frontend-assigned stable id.
  id: string
  severity: DiagnosticSeverity
  message: string
  // Source/domain that produced the diagnostic, e.g. 'engine', 'viewport',
  // 'terrain-validation'.
  source: string
  // Canonical object ID the diagnostic targets, when available. Used for
  // navigation/selection hooks.
  targetId?: CanonicalId
}

interface ProblemsState {
  diagnostics: DiagnosticProjection[]
  upsert: (diagnostic: DiagnosticProjection) => void
  remove: (id: string) => void
  clear: () => void
  clearSource: (source: string) => void
  // Replaces all diagnostics from a source with the given list. Used when
  // the backend sends a full replacement for a source rather than a delta.
  replaceSource: (source: string, diagnostics: DiagnosticProjection[]) => void
}

function upsertDiagnostic(list: DiagnosticProjection[], diagnostic: DiagnosticProjection): DiagnosticProjection[] {
  const index = list.findIndex((existing) => existing.id === diagnostic.id)
  if (index === -1) {
    return [...list, diagnostic]
  }
  const next = list.slice()
  next[index] = { ...next[index]!, ...diagnostic }
  return next
}

export const useProblemsStore = create<ProblemsState>((set, get) => ({
  diagnostics: [],
  upsert: (diagnostic) => set({ diagnostics: upsertDiagnostic(get().diagnostics, diagnostic) }),
  remove: (id) => set({ diagnostics: get().diagnostics.filter((d) => d.id !== id) }),
  clear: () => set({ diagnostics: [] }),
  clearSource: (source) => set({ diagnostics: get().diagnostics.filter((d) => d.source !== source) }),
  replaceSource: (source, diagnostics) =>
    set({
      diagnostics: [
        ...get().diagnostics.filter((d) => d.source !== source),
        ...diagnostics,
      ],
    }),
}))

// Helper to bridge the existing project lastError into the diagnostics store
// as a real engine-session diagnostic. This does not fabricate warnings; it
// only surfaces the real error the engine session already reported.
export function engineSessionDiagnostic(
  severity: DiagnosticSeverity,
  message: string,
): DiagnosticProjection {
  return {
    id: `engine-session:${message}`,
    severity,
    message,
    source: 'engine',
  }
}

export function viewportDiagnostic(
  severity: DiagnosticSeverity,
  message: string,
): DiagnosticProjection {
  return {
    id: `viewport:${message}`,
    severity,
    message,
    source: 'viewport',
  }
}
