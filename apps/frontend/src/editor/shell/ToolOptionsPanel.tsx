import { useAuthoringDraftStore } from '../tools/authoringDraftStore'
import { AUTHORING_TOOLS } from '../tools/authoringToolTypes'
import { GRID_STEPS, ANGLE_STEPS } from '../tools/snapService'
import { Check, X } from 'lucide-react'

export interface ToolOptionsPanelProps {
  onCommitPolyline?: () => void
}

export function ToolOptionsPanel({ onCommitPolyline }: ToolOptionsPanelProps) {
  const activeTool = useAuthoringDraftStore((state) => state.activeTool)
  const draftPoints = useAuthoringDraftStore((state) => state.draftPoints)
  const clearDraft = useAuthoringDraftStore((state) => state.clearDraft)
  const setTool = useAuthoringDraftStore((state) => state.setTool)
  const snappingConfig = useAuthoringDraftStore((state) => state.snappingConfig)
  const updateSnappingConfig = useAuthoringDraftStore((state) => state.updateSnappingConfig)
  const metrics = useAuthoringDraftStore((state) => state.metrics)
  const clothoidParams = useAuthoringDraftStore((state) => state.clothoidParams)
  const updateClothoidParams = useAuthoringDraftStore((state) => state.updateClothoidParams)
  const roadParams = useAuthoringDraftStore((state) => state.roadParams)
  const updateRoadParams = useAuthoringDraftStore((state) => state.updateRoadParams)

  const def = AUTHORING_TOOLS[activeTool]

  const handleCancel = () => {
    clearDraft()
    setTool('select')
  }

  return (
    <div className="authoring-options-bar" role="toolbar" aria-label="Tool options and snapping">
      <div className="authoring-tool-badge">
        <span>{def.label}</span>
        <kbd className="search-shortcut">{def.shortcut}</kbd>
      </div>

      <span className="authoring-status-hint">
        {draftPoints.length > 0 ? (
          <strong>
            Point {draftPoints.length}
            {def.maxPoints ? ` of ${def.maxPoints}` : ''} —{' '}
          </strong>
        ) : null}
        {def.statusHint}
      </span>

      {/* Snapping controls */}
      <div className="authoring-snapping-group">
        <label className="authoring-checkbox-label" title="Snap to coordinate grid">
          <input
            type="checkbox"
            checked={snappingConfig.gridSnap}
            onChange={(e) => updateSnappingConfig({ gridSnap: e.target.checked })}
          />
          Grid
        </label>
        {snappingConfig.gridSnap && (
          <select
            className="authoring-compact-select"
            value={snappingConfig.gridStep}
            onChange={(e) => updateSnappingConfig({ gridStep: Number(e.target.value) })}
          >
            {GRID_STEPS.map((s) => (
              <option key={s} value={s}>
                {s}m
              </option>
            ))}
          </select>
        )}

        <label className="authoring-checkbox-label" title="Snap direction to angular increments">
          <input
            type="checkbox"
            checked={snappingConfig.angleSnap}
            onChange={(e) => updateSnappingConfig({ angleSnap: e.target.checked })}
          />
          Angle
        </label>
        {snappingConfig.angleSnap && (
          <select
            className="authoring-compact-select"
            value={snappingConfig.angleStepDeg}
            onChange={(e) => updateSnappingConfig({ angleStepDeg: Number(e.target.value) })}
          >
            {ANGLE_STEPS.map((a) => (
              <option key={a} value={a}>
                {a}°
              </option>
            ))}
          </select>
        )}

        <label className="authoring-checkbox-label" title="Snap to nearest road start/end point">
          <input
            type="checkbox"
            checked={snappingConfig.endpointSnap}
            onChange={(e) => updateSnappingConfig({ endpointSnap: e.target.checked })}
          />
          Endpoint
        </label>
      </div>

      {/* Live CAD Metrics */}
      {metrics.totalLength > 0 && (
        <div className="authoring-metrics-group">
          <div className="authoring-metric-item">
            Len:<span>{metrics.totalLength.toFixed(1)}m</span>
          </div>
          <div className="authoring-metric-item">
            Heading:<span>{metrics.headingDeg.toFixed(1)}°</span>
          </div>
          <div className="authoring-metric-item">
            dE:<span>{metrics.deltaE > 0 ? `+${metrics.deltaE.toFixed(1)}` : metrics.deltaE.toFixed(1)}m</span>
          </div>
          <div className="authoring-metric-item">
            dN:<span>{metrics.deltaN > 0 ? `+${metrics.deltaN.toFixed(1)}` : metrics.deltaN.toFixed(1)}m</span>
          </div>
        </div>
      )}

      {/* Clothoid parameters */}
      {activeTool === 'road.clothoid' && (
        <div className="authoring-snapping-group">
          <label className="authoring-checkbox-label" title="Clothoid transition length">
            L (m):
            <input
              type="number"
              className="authoring-compact-select"
              style={{ width: '60px' }}
              value={clothoidParams.length}
              min={1}
              step={5}
              onChange={(e) => updateClothoidParams({ length: Math.max(1, Number(e.target.value)) })}
            />
          </label>
          <label className="authoring-checkbox-label" title="End curvature (1/R)">
            k1:
            <input
              type="number"
              className="authoring-compact-select"
              style={{ width: '70px' }}
              value={clothoidParams.endCurvature}
              step={0.001}
              onChange={(e) => updateClothoidParams({ endCurvature: Number(e.target.value) })}
            />
          </label>
        </div>
      )}

      {/* Conformance to terrain */}
      <div className="authoring-snapping-group">
        <label className="authoring-checkbox-label" title="Sample terrain elevation along reference alignment">
          <input
            type="checkbox"
            checked={roadParams.stickToTerrain}
            onChange={(e) => updateRoadParams({ stickToTerrain: e.target.checked })}
          />
          Conform Terrain
        </label>
      </div>

      {/* Action buttons */}
      <div className="authoring-action-buttons">
        {activeTool === 'road.polyline' && draftPoints.length >= 2 && onCommitPolyline && (
          <button
            type="button"
            className="tool-button active"
            onClick={onCommitPolyline}
            title="Finish polyline and fit road (Enter)"
          >
            <Check size={14} /> Finish
          </button>
        )}
        {(draftPoints.length > 0 || activeTool !== 'select') && (
          <button
            type="button"
            className="tool-button"
            onClick={handleCancel}
            title="Cancel current tool and clear draft (Esc)"
          >
            <X size={14} /> Cancel
          </button>
        )}
      </div>
    </div>
  )
}
