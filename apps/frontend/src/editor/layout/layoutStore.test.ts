import { beforeEach, describe, expect, it } from 'vitest'
import {
  useLayoutStore,
  clearLayoutPreferences,
  defaultPreferences,
  clampPanelSize,
  PANEL_MIN_SIZE,
  PREFERENCES_STORAGE_KEY,
  type PanelRegion,
} from './layoutStore'

beforeEach(() => {
  clearLayoutPreferences()
  useLayoutStore.getState().hydrate()
})

describe('layoutStore panel visibility', () => {
  it('defaults all panels to visible', () => {
    const panels = useLayoutStore.getState().panels
    expect(panels.left.visible).toBe(true)
    expect(panels.right.visible).toBe(true)
    expect(panels.bottom.visible).toBe(true)
  })

  it('toggles panel visibility', () => {
    useLayoutStore.getState().setPanelVisible('left', false)
    expect(useLayoutStore.getState().panels.left.visible).toBe(false)
    useLayoutStore.getState().setPanelVisible('left', true)
    expect(useLayoutStore.getState().panels.left.visible).toBe(true)
  })
})

describe('layoutStore resize', () => {
  it('sets a panel size', () => {
    useLayoutStore.getState().setPanelSize('left', 320)
    expect(useLayoutStore.getState().panels.left.size).toBe(320)
  })

  it('clamps to the minimum size', () => {
    useLayoutStore.getState().setPanelSize('left', 10)
    expect(useLayoutStore.getState().panels.left.size).toBe(PANEL_MIN_SIZE.left)
  })

  it('clampPanelSize enforces minimums per region', () => {
    for (const region of ['left', 'right', 'bottom'] as PanelRegion[]) {
      expect(clampPanelSize(region, 0)).toBe(PANEL_MIN_SIZE[region])
      expect(clampPanelSize(region, 9999)).toBe(9999)
    }
  })
})

describe('layoutStore preference persistence', () => {
  it('persists visibility and size to localStorage separately from project state', () => {
    useLayoutStore.getState().setPanelVisible('right', false)
    useLayoutStore.getState().setPanelSize('right', 420)

    const raw = window.localStorage.getItem(PREFERENCES_STORAGE_KEY)
    expect(raw).not.toBeNull()
    const parsed = JSON.parse(raw!)
    expect(parsed.panels.right.visible).toBe(false)
    expect(parsed.panels.right.size).toBe(420)
    // Other panels are untouched.
    expect(parsed.panels.left.visible).toBe(true)
  })

  it('restores preferences on hydrate', () => {
    useLayoutStore.getState().setPanelVisible('bottom', false)
    useLayoutStore.getState().setPanelSize('left', 333)

    // Simulate a reload: a fresh store instance hydrates from storage.
    useLayoutStore.getState().hydrate()
    const panels = useLayoutStore.getState().panels
    expect(panels.bottom.visible).toBe(false)
    expect(panels.left.size).toBe(333)
  })

  it('resets to defaults', () => {
    useLayoutStore.getState().setPanelVisible('left', false)
    useLayoutStore.getState().setPanelSize('left', 999)
    useLayoutStore.getState().resetLayout()
    const defaults = defaultPreferences().panels
    const panels = useLayoutStore.getState().panels
    expect(panels.left.visible).toBe(defaults.left.visible)
    expect(panels.left.size).toBe(defaults.left.size)
  })

  it('falls back to defaults when storage is empty', () => {
    clearLayoutPreferences()
    useLayoutStore.getState().hydrate()
    const defaults = defaultPreferences().panels
    const panels = useLayoutStore.getState().panels
    expect(panels).toEqual(defaults)
  })

  it('falls back to defaults when storage is corrupt', () => {
    window.localStorage.setItem(PREFERENCES_STORAGE_KEY, '{not json')
    useLayoutStore.getState().hydrate()
    const panels = useLayoutStore.getState().panels
    expect(panels).toEqual(defaultPreferences().panels)
  })

  it('normalizes a partial/malformed stored value', () => {
    window.localStorage.setItem(
      PREFERENCES_STORAGE_KEY,
      JSON.stringify({ panels: { left: { visible: false } } }),
    )
    useLayoutStore.getState().hydrate()
    const panels = useLayoutStore.getState().panels
    expect(panels.left.visible).toBe(false)
    expect(panels.left.size).toBe(defaultPreferences().panels.left.size)
    expect(panels.right).toEqual(defaultPreferences().panels.right)
  })
})

describe('layoutStore viewport policy compatibility', () => {
  it('hiding the left panel does not affect the right panel state', () => {
    useLayoutStore.getState().setPanelVisible('left', false)
    expect(useLayoutStore.getState().panels.right.visible).toBe(true)
    expect(useLayoutStore.getState().panels.bottom.visible).toBe(true)
  })

  it('resizing one panel does not change another panel size', () => {
    useLayoutStore.getState().setPanelSize('left', 300)
    useLayoutStore.getState().setPanelSize('right', 400)
    expect(useLayoutStore.getState().panels.left.size).toBe(300)
    expect(useLayoutStore.getState().panels.right.size).toBe(400)
    useLayoutStore.getState().setPanelSize('left', 250)
    expect(useLayoutStore.getState().panels.right.size).toBe(400)
  })
})
