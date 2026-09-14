import type { EngineSessionStatus } from '../../lib/engineSession'
import { useProjectStore } from '../../features/project/projectStore'
import { useViewportStore } from '../../features/viewport/viewportStore'
import { StatusDot, type StatusDotTone } from '../../ui/StatusDot'

// StatusBar — simplified status bar with concise indicators. Detailed
// information appears in tooltips. Camera instructions are NOT permanently
// shown (available through help overlay/tooltips instead).
//
// Layout:
//   Left:  Engine dot + state | Renderer dot + state | Project state
//   Center: CRS
//   Right: GPU | Version
export function StatusBar({ engineStatus }: { engineStatus: EngineSessionStatus }) {
  const summary = useProjectStore((state) => state.summary)
  const rendererStatus = useViewportStore((state) => state.status)

  const engineTone: StatusDotTone =
    engineStatus.state === 'ready' ? 'success' :
    engineStatus.state === 'failed' || engineStatus.state === 'disconnected' ? 'danger' :
    engineStatus.state === 'unavailable' ? 'warning' :
    'muted'

  const rendererTone: StatusDotTone =
    rendererStatus.state === 'ready' ? 'success' :
    rendererStatus.state === 'failed' || rendererStatus.state === 'stopped' || rendererStatus.state === 'device_lost' ? 'danger' :
    rendererStatus.state === 'suspended' || rendererStatus.state === 'unavailable' ? 'warning' :
    'muted'

  const rendererTitle = [
    rendererStatus.detail,
    rendererStatus.gpu ? `GPU: ${rendererStatus.gpu}` : null,
    rendererStatus.vulkan ? `Vulkan ${rendererStatus.vulkan}` : null,
    rendererStatus.validation ? 'Validation enabled' : null,
  ].filter(Boolean).join(' — ')

  return (
    <footer className="status-bar">
      {/* Left: engine + renderer + project state */}
      <span className="status-item" title={engineStatus.message}>
        <StatusDot tone={engineTone} />
        Engine {engineStatus.state}
      </span>
      <span className="status-divider" />
      <span className={`status-item renderer-${rendererStatus.state}`} title={rendererTitle}>
        <StatusDot tone={rendererTone} />
        Renderer {rendererStatus.state}
        {rendererStatus.gpu ? ` · ${rendererStatus.gpu}` : ''}
      </span>
      <span className="status-divider" />
      {summary ? (
        <>
          <span className="status-item">
            Rev {summary.revision}
            {summary.dirty ? ' · unsaved' : ''}
          </span>
        </>
      ) : (
        <span className="status-item">No project</span>
      )}

      <div className="status-spacer" />

      {/* Center/right: CRS */}
      {summary ? (
        <span className="status-item" title={summary.georeference?.horizontalCrs ?? undefined}>
          CRS {summary.georeference?.horizontalCrs || '—'}
        </span>
      ) : (
        <span className="status-item">CRS —</span>
      )}
      <span className="status-divider" />
      <span className="status-item" title="InfraForge version">v0.1.0</span>
    </footer>
  )
}
