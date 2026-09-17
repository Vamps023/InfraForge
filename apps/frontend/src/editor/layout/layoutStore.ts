import { create } from 'zustand'

// Dockable editor layout store. Panel visibility and sizes are USER editor
// preferences, persisted separately from canonical project state (ADR-0009).
// They never enter project files. Persistence uses localStorage under a
// dedicated key so preferences survive reloads without becoming project data.
//
// Minimum sizes are enforced so panels cannot collapse to unusable widths.

export type PanelRegion = 'left' | 'right' | 'bottom' | 'contextEditor'

export interface PanelState {
  visible: boolean
  // For left/right: width in CSS px. For bottom and contextEditor: height in CSS px.
  size: number
}

export interface LayoutPreferences {
  panels: Record<PanelRegion, PanelState>
}

export const PANEL_MIN_SIZE: Record<PanelRegion, number> = {
  left: 180,
  right: 200,
  bottom: 80,
  contextEditor: 120,
}

export const PANEL_DEFAULT_SIZE: Record<PanelRegion, number> = {
  left: 250,
  right: 290,
  bottom: 150,
  contextEditor: 200,
}

const PREFERENCES_STORAGE_KEY = 'infraforge.editor.layout.v1'

export function defaultPreferences(): LayoutPreferences {
  return {
    panels: {
      left: { visible: true, size: PANEL_DEFAULT_SIZE.left },
      right: { visible: true, size: PANEL_DEFAULT_SIZE.right },
      bottom: { visible: true, size: PANEL_DEFAULT_SIZE.bottom },
      contextEditor: { visible: true, size: PANEL_DEFAULT_SIZE.contextEditor },
    },
  }
}

// Pure: clamps a size to the minimum for the region and rejects invalid
// numeric values (NaN, Infinity, -Infinity, absurdly large numbers) so
// corrupted localStorage cannot make the editor unusable. A practical
// maximum is enforced so a corrupt preference cannot consume the whole
// window; the viewport re-reports its bounds on every layout change anyway,
// so clamping at persistence time is safe.
export const PANEL_MAX_SIZE: Record<PanelRegion, number> = {
  left: 2400,
  right: 2400,
  bottom: 1600,
  contextEditor: 1200,
}

export function clampPanelSize(region: PanelRegion, size: number): number {
  if (!Number.isFinite(size)) {
    return PANEL_DEFAULT_SIZE[region]
  }
  const min = PANEL_MIN_SIZE[region]
  const max = PANEL_MAX_SIZE[region]
  if (size < min) {
    return min
  }
  if (size > max) {
    return max
  }
  return size
}

function loadPreferences(): LayoutPreferences {
  if (typeof window === 'undefined') {
    return defaultPreferences()
  }
  try {
    const raw = window.localStorage.getItem(PREFERENCES_STORAGE_KEY)
    if (!raw) {
      return defaultPreferences()
    }
    const parsed = JSON.parse(raw) as Partial<LayoutPreferences>
    return normalizePreferences(parsed)
  } catch {
    return defaultPreferences()
  }
}

function normalizePreferences(parsed: Partial<LayoutPreferences> | undefined): LayoutPreferences {
  const fallback = defaultPreferences()
  if (!parsed || typeof parsed !== 'object') {
    return fallback
  }
  const panels: Partial<Record<PanelRegion, PanelState>> = parsed.panels ?? {}
  const region = (r: PanelRegion): PanelState => {
    const p = panels[r]
    if (!p || typeof p !== 'object') {
      return fallback.panels[r]
    }
    return {
      visible: typeof p.visible === 'boolean' ? p.visible : fallback.panels[r].visible,
      size: clampPanelSize(r, typeof p.size === 'number' && Number.isFinite(p.size) ? p.size : fallback.panels[r].size),
    }
  }
  return {
    panels: {
      left: region('left'),
      right: region('right'),
      bottom: region('bottom'),
      contextEditor: region('contextEditor'),
    },
  }
}

function persistPreferences(prefs: LayoutPreferences): void {
  if (typeof window === 'undefined') {
    return
  }
  try {
    window.localStorage.setItem(PREFERENCES_STORAGE_KEY, JSON.stringify(prefs))
  } catch {
    // Persistence is best-effort; a full quota or disabled storage must not
    // break the editor. Preferences reset to defaults on next load.
  }
}

interface LayoutStoreState {
  panels: Record<PanelRegion, PanelState>
  setPanelVisible: (region: PanelRegion, visible: boolean) => void
  setPanelSize: (region: PanelRegion, size: number) => void
  resetLayout: () => void
  hydrate: () => void
}

function initialPanels(): Record<PanelRegion, PanelState> {
  const prefs = loadPreferences()
  return prefs.panels
}

export const useLayoutStore = create<LayoutStoreState>((set, get) => ({
  panels: initialPanels(),
  setPanelVisible: (region, visible) => {
    const next = { ...get().panels, [region]: { ...get().panels[region], visible } }
    set({ panels: next })
    persistPreferences({ panels: next })
  },
  setPanelSize: (region, size) => {
    const clamped = clampPanelSize(region, size)
    const next = { ...get().panels, [region]: { ...get().panels[region], size: clamped } }
    set({ panels: next })
    persistPreferences({ panels: next })
  },
  resetLayout: () => {
    const prefs = defaultPreferences()
    set({ panels: prefs.panels })
    persistPreferences(prefs)
  },
  hydrate: () => {
    set({ panels: loadPreferences().panels })
  },
}))

// Exported for tests that need to reset localStorage state between cases.
export function clearLayoutPreferences(): void {
  if (typeof window === 'undefined') {
    return
  }
  try {
    window.localStorage.removeItem(PREFERENCES_STORAGE_KEY)
  } catch {
    // ignore
  }
}

export { PREFERENCES_STORAGE_KEY }
