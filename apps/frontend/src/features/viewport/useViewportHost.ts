import { useEffect, type RefObject } from 'react'
import { useViewportStore } from './viewportStore'

// Reports the viewport-host rectangle to the desktop shell and mirrors
// renderer status into the projection store. Bounds are re-sent on every
// layout change of the host element and on device-pixel-ratio changes; the
// shell additionally re-places the surface on pure window moves.
//
// Visibility: the page reports its combined desired visibility — host on
// screen AND no blocking application overlay — over the existing
// viewport:set-visible channel. The desktop shell owns the native viewport
// process/HWND and folds in window displayability plus the deterministic
// restore ordering (ViewportVisibilityPolicy).
export function useViewportHost(
  hostRef: RefObject<HTMLDivElement | null>,
  occlusion: { blockedByOverlay?: boolean } = {},
): void {
  const setStatus = useViewportStore((state) => state.setStatus)
  const blockedByOverlay = occlusion.blockedByOverlay ?? false

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

    return () => {
      observer?.disconnect()
      dprQuery?.removeEventListener('change', listenDpr)
      unsubscribeStatus?.()
    }
  }, [hostRef, setStatus])

  useEffect(() => {
    const desktop = window.infraforgeDesktop
    if (!desktop?.setViewportVisible) {
      return
    }
    const sendDesiredVisibility = () => {
      desktop.setViewportVisible(!document.hidden && !blockedByOverlay)
    }
    const visibilityListener = () => sendDesiredVisibility()
    document.addEventListener('visibilitychange', visibilityListener)
    // Assert on every overlay/state change (and once on mount) so the shell
    // converges even when an earlier signal raced viewport startup.
    sendDesiredVisibility()
    return () => {
      document.removeEventListener('visibilitychange', visibilityListener)
    }
  }, [blockedByOverlay])
}
