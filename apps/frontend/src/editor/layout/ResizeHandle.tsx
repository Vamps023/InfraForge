import { useCallback, useEffect, useRef } from 'react'
import { useLayoutStore, PANEL_DEFAULT_SIZE, PANEL_MIN_SIZE, PANEL_MAX_SIZE, type PanelRegion } from './layoutStore'

// Accessible resize handle for a dockable panel. Pointer drag updates the
// panel size through the layout store; the store clamps to minimums and
// persists the result. The handle is keyboard-operable: arrow keys nudge
// the edge by 8px, Home resets to the region default, and Shift+arrow
// nudges by 32px.
//
// `edge` describes which side the handle sits on so the drag direction maps
// correctly: a left panel's handle is on its right edge (drag right grows),
// a right panel's handle is on its left edge (drag left grows), and the
// bottom panel's handle is on its top edge (drag up grows).
//
// Keyboard semantics make spatial sense for each panel:
//   - left panel: ArrowRight grows, ArrowLeft shrinks
//   - right panel: ArrowLeft grows, ArrowRight shrinks
//   - bottom panel: ArrowUp grows, ArrowDown shrinks
export type PanelEdge = 'right' | 'left' | 'top'

interface ResizeHandleProps {
  region: PanelRegion
  edge: PanelEdge
  ariaLabel: string
}

const NUDGE = 8
const NUDGE_LARGE = 32

export function ResizeHandle({ region, edge, ariaLabel }: ResizeHandleProps) {
  const setPanelSize = useLayoutStore((state) => state.setPanelSize)
  const size = useLayoutStore((state) => state.panels[region].size)
  // The drag state captures the pointer origin, the panel size at drag start,
  // and the previous body userSelect value so cleanup can restore it exactly
  // (rather than unconditionally resetting to '') — both on normal pointer-up
  // and on unmount mid-drag.
  const draggingRef = useRef<{ startX: number; startY: number; startSize: number; previousUserSelect: string } | null>(null)

  const onPointerMove = useCallback(
    (event: PointerEvent) => {
      const drag = draggingRef.current
      if (!drag) {
        return
      }
      const delta =
        edge === 'right'
          ? event.clientX - drag.startX
          : edge === 'left'
            ? drag.startX - event.clientX
            : drag.startY - event.clientY
      setPanelSize(region, drag.startSize + delta)
    },
    [edge, region, setPanelSize],
  )

  const endDrag = useCallback(() => {
    const drag = draggingRef.current
    draggingRef.current = null
    window.removeEventListener('pointermove', onPointerMove)
    window.removeEventListener('pointerup', endDrag)
    if (drag) {
      document.body.style.userSelect = drag.previousUserSelect
    }
  }, [onPointerMove])

  const onPointerDown = useCallback(
    (event: React.PointerEvent<HTMLDivElement>) => {
      event.preventDefault()
      const previousUserSelect = document.body.style.userSelect
      draggingRef.current = { startX: event.clientX, startY: event.clientY, startSize: size, previousUserSelect }
      window.addEventListener('pointermove', onPointerMove)
      window.addEventListener('pointerup', endDrag)
      document.body.style.userSelect = 'none'
    },
    [onPointerMove, endDrag, size],
  )

  useEffect(() => {
    return () => {
      window.removeEventListener('pointermove', onPointerMove)
      window.removeEventListener('pointerup', endDrag)
      // Restore body userSelect if the component unmounts during an active
      // drag. Without this, the application can be left with text selection
      // permanently disabled. Restore the previous value, not ''.
      if (draggingRef.current !== null) {
        const drag = draggingRef.current
        draggingRef.current = null
        document.body.style.userSelect = drag.previousUserSelect
      }
    }
  }, [onPointerMove, endDrag])

  const onKeyDown = useCallback(
    (event: React.KeyboardEvent<HTMLDivElement>) => {
      // Grow/shrink directions are spatial and depend on the edge.
      const growKey =
        edge === 'right'
          ? 'ArrowRight'
          : edge === 'left'
            ? 'ArrowLeft'
            : 'ArrowUp'
      const shrinkKey =
        edge === 'right'
          ? 'ArrowLeft'
          : edge === 'left'
            ? 'ArrowRight'
            : 'ArrowDown'
      const amount = event.shiftKey ? NUDGE_LARGE : NUDGE
      switch (event.key) {
        case growKey:
          event.preventDefault()
          setPanelSize(region, size + amount)
          break
        case shrinkKey:
          event.preventDefault()
          setPanelSize(region, size - amount)
          break
        case 'Home':
          event.preventDefault()
          // Reset to the region-specific default, not a hardcoded constant.
          setPanelSize(region, PANEL_DEFAULT_SIZE[region])
          break
      }
    },
    [edge, region, setPanelSize, size],
  )

  const orientation = region === 'bottom' ? 'horizontal' : 'vertical'
  return (
    <div
      role="separator"
      aria-orientation={orientation}
      aria-label={ariaLabel}
      aria-valuemin={PANEL_MIN_SIZE[region]}
      aria-valuemax={PANEL_MAX_SIZE[region]}
      aria-valuenow={Math.round(size)}
      tabIndex={0}
      className={`resize-handle resize-${region}`}
      onPointerDown={onPointerDown}
      onKeyDown={onKeyDown}
    />
  )
}
