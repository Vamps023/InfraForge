// Centralized native-viewport visibility policy (pure, unit-tested).
//
// The React page owns the inputs it can see — whether the viewport host is on
// screen and whether a blocking application overlay (dialog covering the
// editor) is active — and reports their combination over the existing
// viewport:set-visible channel. The desktop shell owns the native viewport
// process/HWND and folds in the window's own displayability. The native
// viewport is shown only when every input allows it:
//
//   nativeViewportVisible = viewportHostVisible
//                         && !blockingOverlayActive
//                         && windowCanDisplayViewport
//
// The policy returns an ordered action list instead of a bare boolean so the
// occluded -> visible transition is deterministic: placement is recomputed
// and applied from the latest cached bounds BEFORE the surface is shown, so
// the child HWND cannot flash over the overlay or return at stale
// coordinates. Hiding is always re-asserted (never skipped as a no-op)
// because a viewport process that starts later creates its window visible.

export interface ViewportVisibilityInputs {
  // The page's combined signal: viewport host on screen and no blocking
  // overlay covers it.
  pageDesiresViewport: boolean
  // The shell's own input: the BrowserWindow can display content right now
  // (not minimized).
  windowDisplayable: boolean
  // Whether the shell last applied visible=true to the native viewport.
  currentlyAppliedVisible: boolean
  // Whether the shell has cached viewport bounds for the sending window.
  hasCachedBounds: boolean
}

export type ViewportShellAction = 'place' | 'show' | 'hide'

export interface ViewportVisibilityPlan {
  visible: boolean
  actions: ViewportShellAction[]
}

export function planViewportVisibility(inputs: ViewportVisibilityInputs): ViewportVisibilityPlan {
  const visible = inputs.pageDesiresViewport && inputs.windowDisplayable

  if (!visible) {
    return { visible, actions: ['hide'] }
  }
  if (inputs.currentlyAppliedVisible) {
    // Already shown; placement while visible is owned by the bounds and
    // window-move paths, not by the visibility policy.
    return { visible, actions: [] }
  }
  return {
    visible,
    actions: inputs.hasCachedBounds ? ['place', 'show'] : ['show'],
  }
}

// The startup visibility decision: the native child window is created
// visible or hidden with exactly this value, so a viewport process started
// while a blocking overlay is open or the host window is minimized can
// never flash before the first runtime visibility command. Same conditions
// as the runtime policy's `visible`, deliberately not a second state
// machine — runtime visibility stays on planViewportVisibility +
// viewport:set-visible.
export function desiredStartupVisibility(
  inputs: Pick<ViewportVisibilityInputs, 'pageDesiresViewport' | 'windowDisplayable'>,
): boolean {
  return planViewportVisibility({ ...inputs, currentlyAppliedVisible: false, hasCachedBounds: false }).visible
}
