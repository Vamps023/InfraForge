import { useState } from 'react'
import { useRoadStore } from './roadStore'
import { useSelectionStore } from '../../editor/selection/selectionStore'
import { updateRoadElevation, getRoad } from './roadApi'
import { contextEditorRegistry } from '../../editor/contextEditor/contextEditorRegistry'
import type { EngineClient } from '../../lib/engineSession'

export interface RoadProfileEditorProps {
  getEngineClient: () => EngineClient | null
}

export function registerRoadProfileContextEditor(deps: RoadProfileEditorProps): void {
  contextEditorRegistry.register({
    id: 'road-profile',
    label: 'Road Vertical Profile',
    applies: (ctx) =>
      ctx.activeWorkspace === 'roads' && ctx.selectedIds.some((id) => id.startsWith('road:')),
    render: () => <RoadProfileEditor getEngineClient={deps.getEngineClient} />,
  })
}

export function unregisterRoadProfileContextEditor(): void {
  contextEditorRegistry.unregister('road-profile')
}

// RoadProfileEditor — context editor for vertical road profile inspection
// and elevation editing. Docked in ContextEditorHost according to
// docs/06_UI_UX/WORKSPACE_MODEL.md and UX_SPEC.md.
// Uses real backend data and canonical updateRoadElevation command.
export function RoadProfileEditor({ getEngineClient }: RoadProfileEditorProps) {
  const primaryId = useSelectionStore((state) => state.primaryId)
  const roadId = primaryId && primaryId.startsWith('road:') ? primaryId.slice('road:'.length) : null
  const roads = useRoadStore((state) => state.roads)
  const details = useRoadStore((state) => state.details)
  const road = roads.find((r) => r.roadId === roadId)

  const [uniformElevation, setUniformElevation] = useState<string>('0')
  const [submitting, setSubmitting] = useState(false)
  const [feedback, setFeedback] = useState<string | null>(null)

  if (!road || !roadId) {
    return (
      <div className="context-editor-empty">
        Select a road in the viewport or Navigator to view its vertical profile.
      </div>
    )
  }

  const handleApplyUniformElevation = async () => {
    const client = getEngineClient()
    if (!client || !details) {
      return
    }

    const elev = parseFloat(uniformElevation)
    if (!Number.isFinite(elev)) {
      setFeedback('Elevation must be a valid finite number.')
      return
    }

    try {
      setSubmitting(true)
      setFeedback(null)
      // Build stations from start station to length
      const count = Math.max(2, details.controlPoints.length)
      const stations: number[] = []
      const elevations: number[] = []
      for (let i = 0; i < count; ++i) {
        const s = (i / (count - 1)) * road.length
        stations.push(s)
        elevations.push(elev)
      }
      await updateRoadElevation(client, roadId, stations, elevations)
      await getRoad(client, roadId)
      setFeedback(`Updated profile elevation to ${elev.toFixed(2)} units.`)
    } catch (err) {
      setFeedback(err instanceof Error ? err.message : String(err))
    } finally {
      setSubmitting(false)
    }
  }

  return (
    <div className="road-profile-editor" aria-label="Road profile editor">
      <div className="profile-metrics-bar">
        <span className="profile-metric">
          <strong>Road:</strong> {road.name}
        </span>
        <span className="profile-metric">
          <strong>Length:</strong> {road.length.toFixed(2)} m
        </span>
        <span className="profile-metric">
          <strong>Profile:</strong>{' '}
          {details?.hasElevationProfile ? 'Authored' : 'Default flat'}
        </span>
        <span className="profile-metric">
          <strong>Breakpoints:</strong>{' '}
          {details?.elevationBreakpointCount ?? 0}
        </span>
      </div>

      <div className="profile-controls-row">
        <div className="profile-input-group">
          <label htmlFor="uniform-elev-input">Set Uniform Elevation:</label>
          <input
            id="uniform-elev-input"
            type="number"
            step="0.5"
            value={uniformElevation}
            onChange={(e) => setUniformElevation(e.target.value)}
            disabled={submitting}
          />
          <button
            type="button"
            className="button secondary"
            disabled={submitting}
            onClick={() => void handleApplyUniformElevation()}
          >
            {submitting ? 'Applying…' : 'Apply to Alignment'}
          </button>
        </div>
        {feedback ? <span className="profile-feedback">{feedback}</span> : null}
      </div>

      <div className="profile-notice mono small">
        Note: Full graphical station/elevation curve dragging requires breakpoint streaming in RoadDetails. Current vertical profile adjustments apply through canonical updateRoadElevation.
      </div>
    </div>
  )
}
