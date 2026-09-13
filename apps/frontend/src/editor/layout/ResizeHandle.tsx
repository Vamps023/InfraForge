import { useCallback, useEffect, useRef } from 'react'
import { useLayoutStore, type PanelRegion } from './layoutStore'

// Accessible resize handle for a dockable panel. Pointer drag updates the
// panel size through the layout store; the store clamps to minimums and
// persists the result. The handle is keyboard-operable: arrow keys nudge
// the edge by 8px, Home resets to default, and Shift+arrow nudges by 32px.
//
// `edge` describes which side the handle sits on so the drag direction maps
// correctly: a left panel's handle is on its right edge (drag right grows),
// a right panel's handle is on its left edge (drag left grows), and the
// bottom panel's handle is on its top edge (drag up grows).
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
      const grow = (amount: number) => {
        const dir = edge === 'right' ? 1 : edge === 'left' ? 1 : 1
        setPanelSize(region, size + amount * dir)
      }
      const shrink = (amount: number) => {
        const dir = edge === 'right' ? -1 : edge === 'left' ? -1 : -1
        setPanelSize(region, size + amount * dir)
      }
      switch (event.key) {
        case 'ArrowRight':
        case 'ArrowDown':
          event.preventDefault()
          grow(event.shiftKey ? NUDGE_LARGE : NUDGE)
          break
        case 'ArrowLeft':
        case 'ArrowUp':
          event.preventDefault()
          shrink(event.shiftKey ? NUDGE_LARGE : NUDGE)
          break
        case 'Home':
          event.preventDefault()
          setPanelSize(region, 250)
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
      tabIndex={0}
      className={`resize-handle resize-${region}`}
      onPointerDown={onPointerDown}
      onKeyDown={onKeyDown}
    />
  )
}
