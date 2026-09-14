import { useViewportStore, viewportSurfaceActive } from '../../features/viewport/viewportStore'

// ViewportOverlay — HUD elements rendered over the viewport area. Only
// shows real data from the viewport status projection. Does not fake
// FPS, coordinates, or shading mode controls — those will be wired when
// the renderer exposes them.
export function ViewportOverlay() {
  const rendererStatus = useViewportStore((state) => state.status)
  const surfaceActive = viewportSurfaceActive(rendererStatus.state)

  // Only show HUD when the viewport surface is active (renderer is ready).
  if (!surfaceActive) {
    return null
  }

  return (
    <div className="viewport-hud" aria-hidden="true">
      <div className="viewport-hud-br">
        <span className="viewport-hud-item">
          <span className="viewport-hud-label">Renderer</span>
          <span className="viewport-hud-value">{rendererStatus.state}</span>
        </span>
        {rendererStatus.gpu ? (
          <>
            <span className="viewport-hud-divider" />
            <span className="viewport-hud-item">
              <span className="viewport-hud-label">GPU</span>
              <span className="viewport-hud-value">{rendererStatus.gpu}</span>
            </span>
          </>
        ) : null}
        {rendererStatus.vulkan ? (
          <>
            <span className="viewport-hud-divider" />
            <span className="viewport-hud-item">
              <span className="viewport-hud-label">Vulkan</span>
              <span className="viewport-hud-value">{rendererStatus.vulkan}</span>
            </span>
          </>
        ) : null}
      </div>
    </div>
  )
}
