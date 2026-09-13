import { describe, expect, it } from 'vitest'
import { planViewportVisibility, type ViewportVisibilityInputs } from '../src/ViewportVisibilityPolicy.js'

function inputs(overrides: Partial<ViewportVisibilityInputs> = {}): ViewportVisibilityInputs {
  return {
    pageDesiresViewport: true,
    windowDisplayable: true,
    currentlyAppliedVisible: false,
    hasCachedBounds: true,
    ...overrides,
  }
}

describe('viewport visibility policy', () => {
  it('hides the native viewport while a blocking overlay is active', () => {
    // The page reports desired=false when a modal covers the editor.
    const plan = planViewportVisibility(inputs({ pageDesiresViewport: false }))
    expect(plan.visible).toBe(false)
    expect(plan.actions).toEqual(['hide'])
  })

  it('re-asserts hidden on every occluded signal instead of skipping as a no-op', () => {
    // A viewport process that starts later creates its window visible, so
    // hiding must never be treated as already-applied.
    const first = planViewportVisibility(inputs({ pageDesiresViewport: false, currentlyAppliedVisible: false }))
    const again = planViewportVisibility(inputs({ pageDesiresViewport: false, currentlyAppliedVisible: false }))
    expect(first.actions).toEqual(['hide'])
    expect(again.actions).toEqual(['hide'])
  })

  it('restores by applying fresh placement before showing', () => {
    const plan = planViewportVisibility(inputs({ currentlyAppliedVisible: false }))
    expect(plan.visible).toBe(true)
    expect(plan.actions).toEqual(['place', 'show'])
  })

  it('shows without a placement step when no cached bounds exist', () => {
    const plan = planViewportVisibility(inputs({ currentlyAppliedVisible: false, hasCachedBounds: false }))
    expect(plan.actions).toEqual(['show'])
  })

  it('is a no-op while the viewport is already visible and desired', () => {
    const plan = planViewportVisibility(inputs({ currentlyAppliedVisible: true }))
    expect(plan.visible).toBe(true)
    expect(plan.actions).toEqual([])
  })

  it('keeps the viewport hidden while the window is minimized and restores deterministically', () => {
    const minimized = planViewportVisibility(inputs({ windowDisplayable: false, currentlyAppliedVisible: true }))
    expect(minimized.visible).toBe(false)
    expect(minimized.actions).toEqual(['hide'])

    const restored = planViewportVisibility(inputs({ currentlyAppliedVisible: false }))
    expect(restored.visible).toBe(true)
    expect(restored.actions).toEqual(['place', 'show'])
  })

  it('survives repeated open -> close -> open -> close cycles', () => {
    const actions: string[] = []
    let applied = false
    let pageDesires = true

    for (let cycle = 0; cycle < 3; cycle += 1) {
      pageDesires = false
      for (const action of planViewportVisibility(inputs({ pageDesiresViewport: pageDesires, currentlyAppliedVisible: applied })).actions) {
        actions.push(action)
        if (action === 'show') {
          applied = true
        }
        if (action === 'hide') {
          applied = false
        }
      }
      expect(applied).toBe(false)

      pageDesires = true
      for (const action of planViewportVisibility(inputs({ pageDesiresViewport: pageDesires, currentlyAppliedVisible: applied })).actions) {
        actions.push(action)
        if (action === 'show') {
          applied = true
        }
        if (action === 'hide') {
          applied = false
        }
      }
      expect(applied).toBe(true)
    }

    expect(actions).toEqual([
      'hide', 'place', 'show',
      'hide', 'place', 'show',
      'hide', 'place', 'show',
    ])
  })
})


describe('first show after native startup', () => {
  // The native surface is always created hidden; the shell's first-show
  // happens only through this policy application after readiness.
  it('a fresh (never-shown) surface with desired visibility shows place-before-show', () => {
    expect(
      planViewportVisibility(inputs({ pageDesiresViewport: true, windowDisplayable: true, currentlyAppliedVisible: false })),
    ).toEqual({ visible: true, actions: ['place', 'show'] })
  })

  it('a fresh surface behind an overlay or minimized stays hidden and re-asserts hide', () => {
    expect(
      planViewportVisibility(inputs({ pageDesiresViewport: false, currentlyAppliedVisible: false })),
    ).toEqual({ visible: false, actions: ['hide'] })
    expect(
      planViewportVisibility(inputs({ windowDisplayable: false, currentlyAppliedVisible: false })),
    ).toEqual({ visible: false, actions: ['hide'] })
  })
})
