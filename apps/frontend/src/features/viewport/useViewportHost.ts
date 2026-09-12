import { useEffect, type RefObject } from 'react'
import { useViewportStore } from './viewportStore'

// Reports the viewport-host rectangle to the desktop shell and mirrors
// renderer status into the projection store. Bounds are re-sent on every
// layout change of the host element and on device-pixel-ratio changes; the
// shell additionally re-places the surface on pure window moves.
export function useViewportHost(hostRef: RefObject<HTMLDivElement | null>): void {
  const setStatus = useViewportStore((state) => state.setStatus)

  useEffect(() => {
    const desktop = window.infraforgeDesktop
    if (!desktop?.setViewportBounds) {
      return
    }

    const sendBounds = () => {
      const element = hostRef.current
      if (!element) {
        return
      }
      const rect = element.getBoundingClientRect()
      desktop.setViewportBounds(
        {
          x: Math.round(rect.x * 100) / 100,
          y: Math.round(rect.y * 100) / 100,
          width: Math.round(rect.width * 100) / 100,
          height: Math.round(rect.height * 100) / 100,
        },
        window.devicePixelRatio,
      )
    }

    const unsubscribeStatus = desktop.onViewportStatus?.((status) => setStatus(status))

    // ResizeObserver delivers an initial entry after layout, so bounds are
    // only ever sent with settled geometry (an immediate send here would
    // race the first paint with a stale rect).
    const element = hostRef.current
    let observer: ResizeObserver | null = null
    if (element) {
      observer = new ResizeObserver(() => sendBounds())
      observer.observe(element)
    }

    let dprQuery: MediaQueryList | null = null
    const listenDpr = () => {
      dprQuery = window.matchMedia(`(resolution: ${window.devicePixelRatio}dppx)`)
      dprQuery.addEventListener('change', listenDpr)
      sendBounds()
    }
    listenDpr()

    const visibilityListener = () => {
      desktop.setViewportVisible?.(!document.hidden)
    }
    document.addEventListener('visibilitychange', visibilityListener)

    return () => {
      observer?.disconnect()
      dprQuery?.removeEventListener('change', listenDpr)
      document.removeEventListener('visibilitychange', visibilityListener)
      unsubscribeStatus?.()
    }
  }, [hostRef, setStatus])
}
