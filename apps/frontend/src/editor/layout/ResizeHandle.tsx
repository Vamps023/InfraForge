import { useCallback, useEffect, useRef } from 'react'
import { useLayoutStore, PANEL_DEFAULT_SIZE, type PanelRegion } from './layoutStore'

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
  const draggingRef = useRef<{ startX: number; startY: number; startSize: number } | null>(null)

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

  const onPointerUp = useCallback(() => {
    draggingRef.current = null
    window.removeEventListener('pointermove', onPointerMove)
    window.removeEventListener('pointerup', onPointerUp)
    document.body.style.userSelect = ''
  }, [onPointerMove])

  const onPointerDown = useCallback(
    (event: React.PointerEvent<HTMLDivElement>) => {
      event.preventDefault()
      draggingRef.current = { startX: event.clientX, startY: event.clientY, startSize: size }
      window.addEventListener('pointermove', onPointerMove)
      window.addEventListener('pointerup', onPointerUp)
      document.body.style.userSelect = 'none'
    },
    [onPointerMove, onPointerUp, size],
  )

  useEffect(() => {
    return () => {
      window.removeEventListener('pointermove', onPointerMove)
      window.removeEventListener('pointerup', onPointerUp)
    }
  }, [onPointerMove, onPointerUp])

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
      aria-valuemin={0}
      aria-valuenow={Math.round(size)}
      tabIndex={0}
      className={`resize-handle resize-${region}`}
      onPointerDown={onPointerDown}
      onKeyDown={onKeyDown}
    />
  )
}
