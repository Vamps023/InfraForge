import { useState } from 'react'
import { useAuthoringDraftStore } from '../tools/authoringDraftStore'
import { AUTHORING_TOOLS } from '../tools/authoringToolTypes'
import { GRID_STEPS, ANGLE_STEPS } from '../tools/snapService'
import { useRoadStore } from '../../features/road/roadStore'
import { Settings2, Magnet, Layers, X, ChevronDown, ChevronUp } from 'lucide-react'

export interface ToolOptionsPanelProps {
  onCommitPolyline?: () => void
}

export function ToolOptionsPanel({ onCommitPolyline }: ToolOptionsPanelProps) {
  const [activeTab, setActiveTab] = useState<'tool' | 'snap' | 'lanes'>('tool')
  const [isCollapsed, setIsCollapsed] = useState(false)

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
  const lastError = useRoadStore((state) => state.lastError)
  const setLastError = useRoadStore((state) => state.setLastError)

  if (activeTool === 'select' && draftPoints.length === 0) {
    return null
  }

  const def = AUTHORING_TOOLS[activeTool]

  return (
    <div
      className={`floating-tool-options-panel${isCollapsed ? ' collapsed' : ''}`}
      role="region"
      aria-label="Tool Options"
    >
      {/* Header */}
      <div className="panel-floating-header">
        <div className="tool-title-group">
          <span className="tool-indicator-dot" />
          <span className="tool-title-text">{def.label}</span>
          {def.shortcut && <kbd className="tool-shortcut-badge">{def.shortcut}</kbd>}
        </div>

        <div className="header-icon-actions">
          <button
            type="button"
            className="icon-btn-subtle"
            onClick={() => setIsCollapsed(!isCollapsed)}
            title={isCollapsed ? 'Expand tool options' : 'Collapse tool options'}
            aria-label={isCollapsed ? 'Expand' : 'Collapse'}
          >
            {isCollapsed ? <ChevronDown size={14} /> : <ChevronUp size={14} />}
          </button>
          <button
            type="button"
            className="icon-btn-subtle"
            onClick={() => {
              clearDraft()
              setTool('select')
            }}
            title="Cancel tool (Esc)"
            aria-label="Cancel tool"
          >
            <X size={14} />
          </button>
        </div>
      </div>

      {!isCollapsed && (
        <>
          {/* Tab Navigation */}
          <div className="floating-tabs-list" role="tablist">
            <button
              type="button"
              role="tab"
              aria-selected={activeTab === 'tool'}
              className={`floating-tab-btn${activeTab === 'tool' ? ' active' : ''}`}
              onClick={() => setActiveTab('tool')}
            >
              <Settings2 size={12} />
              <span>Tool</span>
            </button>
            <button
              type="button"
              role="tab"
              aria-selected={activeTab === 'snap'}
              className={`floating-tab-btn${activeTab === 'snap' ? ' active' : ''}`}
              onClick={() => setActiveTab('snap')}
            >
              <Magnet size={12} />
              <span>Snap</span>
            </button>
            <button
              type="button"
              role="tab"
              aria-selected={activeTab === 'lanes'}
              className={`floating-tab-btn${activeTab === 'lanes' ? ' active' : ''}`}
              onClick={() => setActiveTab('lanes')}
            >
              <Layers size={12} />
              <span>Lanes</span>
            </button>
          </div>

          {/* Tab 1: Tool Parameters */}
          {activeTab === 'tool' && (
            <div className="floating-tab-content">
              <label className="checkbox-row" title="Sample terrain elevation along reference alignment">
                <input
                  type="checkbox"
                  checked={roadParams.stickToTerrain}
                  onChange={(e) => updateRoadParams({ stickToTerrain: e.target.checked })}
                />
                <span>Stick to Terrain</span>
              </label>

              {activeTool === 'road.clothoid' && (
                <div className="tool-fields-group">
                  <div className="form-field-row">
                    <label>Mode</label>
                    <select
                      value={clothoidParams.mode}
                      onChange={(e) => updateClothoidParams({ mode: e.target.value as 'interactive' | 'fixed-length' })}
                    >
                      <option value="interactive">Interactive</option>
                      <option value="fixed-length">Fixed Length</option>
                    </select>
                  </div>
                  <div className="form-field-row">
                    <label>Transition L (m)</label>
                    <input
                      type="number"
                      min={1}
                      step={5}
                      value={clothoidParams.length}
                      onChange={(e) => updateClothoidParams({ length: Math.max(1, Number(e.target.value)) })}
                    />
                  </div>
                  <div className="form-field-row">
                    <label>End Curvature k1</label>
                    <input
                      type="number"
                      step={0.001}
                      value={clothoidParams.endCurvature}
                      onChange={(e) => updateClothoidParams({ endCurvature: Number(e.target.value) })}
                    />
                  </div>
                </div>
              )}

              {/* Metrics preview when points are placed */}
              {metrics.totalLength > 0 && (
                <div className="metrics-summary-box">
                  <div className="metric-row">
                    <span>Length</span>
                    <b>{metrics.totalLength.toFixed(1)} m</b>
                  </div>
                  <div className="metric-row">
                    <span>Heading</span>
                    <b>{metrics.headingDeg.toFixed(1)}°</b>
                  </div>
                </div>
              )}
            </div>
          )}

          {/* Tab 2: Snapping */}
          {activeTab === 'snap' && (
            <div className="floating-tab-content">
              <div className="snap-option-row">
                <label className="checkbox-row">
                  <input
                    type="checkbox"
                    checked={snappingConfig.gridSnap}
                    onChange={(e) => updateSnappingConfig({ gridSnap: e.target.checked })}
                  />
                  <span>Grid Snap</span>
                </label>
                {snappingConfig.gridSnap && (
                  <select
                    className="compact-select"
                    value={snappingConfig.gridStep}
                    onChange={(e) => updateSnappingConfig({ gridStep: Number(e.target.value) })}
                  >
                    {GRID_STEPS.map((s) => (
                      <option key={s} value={s}>{s} m</option>
                    ))}
                  </select>
                )}
              </div>

              <div className="snap-option-row">
                <label className="checkbox-row">
                  <input
                    type="checkbox"
                    checked={snappingConfig.angleSnap}
                    onChange={(e) => updateSnappingConfig({ angleSnap: e.target.checked })}
                  />
                  <span>Angle Snap</span>
                </label>
                {snappingConfig.angleSnap && (
                  <select
                    className="compact-select"
                    value={snappingConfig.angleStepDeg}
                    onChange={(e) => updateSnappingConfig({ angleStepDeg: Number(e.target.value) })}
                  >
                    {ANGLE_STEPS.map((a) => (
                      <option key={a} value={a}>{a}°</option>
                    ))}
                  </select>
                )}
              </div>

              <div className="snap-option-row">
                <label className="checkbox-row">
                  <input
                    type="checkbox"
                    checked={snappingConfig.endpointSnap}
                    onChange={(e) => updateSnappingConfig({ endpointSnap: e.target.checked })}
                  />
                  <span>Endpoint Snap</span>
                </label>
              </div>
            </div>
          )}

          {/* Tab 3: Lanes */}
          {activeTab === 'lanes' && (
            <div className="floating-tab-content">
              <p className="tab-hint-text">
                Authoring creates a standard 2-lane road by default. You can customize cross-section lanes in the Lanes tab of the Inspector after placement.
              </p>
            </div>
          )}

          {/* Error Banner */}
          {lastError && (
            <div className="panel-error-strip" role="alert">
              <span>{lastError}</span>
              <button
                type="button"
                className="icon-btn-subtle"
                onClick={() => setLastError(null)}
              >
                <X size={12} />
              </button>
            </div>
          )}
        </>
      )}
    </div>
  )
}
