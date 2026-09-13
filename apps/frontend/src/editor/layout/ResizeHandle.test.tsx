import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { render, screen, fireEvent } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { ResizeHandle } from './ResizeHandle'
import {
  useLayoutStore,
  clearLayoutPreferences,
  PANEL_DEFAULT_SIZE,
  PANEL_MIN_SIZE,
  PANEL_MAX_SIZE,
  type PanelRegion,
} from './layoutStore'

beforeEach(() => {
  clearLayoutPreferences()
  useLayoutStore.getState().hydrate()
})

afterEach(() => {
  clearLayoutPreferences()
  useLayoutStore.getState().hydrate()
})

function getHandle(region: PanelRegion) {
  return screen.getByRole('separator')
}

describe('ResizeHandle keyboard semantics', () => {
  describe('left panel (edge=right)', () => {
    it('ArrowRight grows the panel', () => {
      render(<ResizeHandle region="left" edge="right" ariaLabel="Resize left" />)
      const before = useLayoutStore.getState().panels.left.size
      fireEvent.keyDown(getHandle('left'), { key: 'ArrowRight' })
      expect(useLayoutStore.getState().panels.left.size).toBe(before + 8)
    })

    it('ArrowLeft shrinks the panel', () => {
      render(<ResizeHandle region="left" edge="right" ariaLabel="Resize left" />)
      const before = useLayoutStore.getState().panels.left.size
      fireEvent.keyDown(getHandle('left'), { key: 'ArrowLeft' })
      expect(useLayoutStore.getState().panels.left.size).toBe(before - 8)
    })

    it('Home resets to the left default (250), not a hardcoded constant', () => {
      render(<ResizeHandle region="left" edge="right" ariaLabel="Resize left" />)
      useLayoutStore.getState().setPanelSize('left', 999)
      fireEvent.keyDown(getHandle('left'), { key: 'Home' })
      expect(useLayoutStore.getState().panels.left.size).toBe(PANEL_DEFAULT_SIZE.left)
      expect(PANEL_DEFAULT_SIZE.left).toBe(250)
    })
  })

  describe('right panel (edge=left)', () => {
    it('ArrowLeft grows the panel', () => {
      render(<ResizeHandle region="right" edge="left" ariaLabel="Resize right" />)
      const before = useLayoutStore.getState().panels.right.size
      fireEvent.keyDown(getHandle('right'), { key: 'ArrowLeft' })
      expect(useLayoutStore.getState().panels.right.size).toBe(before + 8)
    })

    it('ArrowRight shrinks the panel', () => {
      render(<ResizeHandle region="right" edge="left" ariaLabel="Resize right" />)
      const before = useLayoutStore.getState().panels.right.size
      fireEvent.keyDown(getHandle('right'), { key: 'ArrowRight' })
      expect(useLayoutStore.getState().panels.right.size).toBe(before - 8)
    })

    it('Home resets to the right default (290), not 250', () => {
      render(<ResizeHandle region="right" edge="left" ariaLabel="Resize right" />)
      useLayoutStore.getState().setPanelSize('right', 999)
      fireEvent.keyDown(getHandle('right'), { key: 'Home' })
      expect(useLayoutStore.getState().panels.right.size).toBe(PANEL_DEFAULT_SIZE.right)
      expect(PANEL_DEFAULT_SIZE.right).toBe(290)
    })
  })

  describe('bottom panel (edge=top)', () => {
    it('ArrowUp grows the panel', () => {
      render(<ResizeHandle region="bottom" edge="top" ariaLabel="Resize bottom" />)
      const before = useLayoutStore.getState().panels.bottom.size
      fireEvent.keyDown(getHandle('bottom'), { key: 'ArrowUp' })
      expect(useLayoutStore.getState().panels.bottom.size).toBe(before + 8)
    })

    it('ArrowDown shrinks the panel', () => {
      render(<ResizeHandle region="bottom" edge="top" ariaLabel="Resize bottom" />)
      const before = useLayoutStore.getState().panels.bottom.size
      fireEvent.keyDown(getHandle('bottom'), { key: 'ArrowDown' })
      expect(useLayoutStore.getState().panels.bottom.size).toBe(before - 8)
    })

    it('Home resets to the bottom default (150), not 250', () => {
      render(<ResizeHandle region="bottom" edge="top" ariaLabel="Resize bottom" />)
      useLayoutStore.getState().setPanelSize('bottom', 999)
      fireEvent.keyDown(getHandle('bottom'), { key: 'Home' })
      expect(useLayoutStore.getState().panels.bottom.size).toBe(PANEL_DEFAULT_SIZE.bottom)
      expect(PANEL_DEFAULT_SIZE.bottom).toBe(150)
    })
  })

  it('Shift+arrow nudges by 32px', () => {
    render(<ResizeHandle region="left" edge="right" ariaLabel="Resize left" />)
    const before = useLayoutStore.getState().panels.left.size
    fireEvent.keyDown(getHandle('left'), { key: 'ArrowRight', shiftKey: true })
    expect(useLayoutStore.getState().panels.left.size).toBe(before + 32)
  })

  it('shrinking does not go below the minimum', () => {
    render(<ResizeHandle region="left" edge="right" ariaLabel="Resize left" />)
    // Shrink aggressively.
    for (let i = 0; i < 100; i += 1) {
      fireEvent.keyDown(getHandle('left'), { key: 'ArrowLeft', shiftKey: true })
    }
    expect(useLayoutStore.getState().panels.left.size).toBe(PANEL_MIN_SIZE.left)
  })

  it('exposes aria-valuenow and aria-orientation', () => {
    const { container } = render(<ResizeHandle region="left" edge="right" ariaLabel="Resize left" />)
    const handle = screen.getByRole('separator')
    expect(handle.getAttribute('aria-orientation')).toBe('vertical')
    expect(handle.getAttribute('aria-valuenow')).toBe(String(useLayoutStore.getState().panels.left.size))
  })

  it('exposes aria-valuemin equal to PANEL_MIN_SIZE for the region', () => {
    render(<ResizeHandle region="left" edge="right" ariaLabel="Resize left" />)
    const handle = screen.getByRole('separator')
    expect(handle.getAttribute('aria-valuemin')).toBe(String(PANEL_MIN_SIZE.left))
  })

  it('exposes aria-valuemax equal to PANEL_MAX_SIZE for the region', () => {
    render(<ResizeHandle region="left" edge="right" ariaLabel="Resize left" />)
    const handle = screen.getByRole('separator')
    expect(handle.getAttribute('aria-valuemax')).toBe(String(PANEL_MAX_SIZE.left))
  })
})
