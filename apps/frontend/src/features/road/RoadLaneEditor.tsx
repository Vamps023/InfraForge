import { useEffect, useState } from 'react'
import { create } from '@bufbuild/protobuf'
import {
  RoadLaneInfoSchema,
  RoadLaneSectionInfoSchema,
  type RoadLaneInfo,
  type RoadLaneSectionInfo,
  type RoadSummary,
  type RoadDetails,
} from '@infraforge/protocol'
import type { EngineClient } from '../../lib/engineSession'
import { updateRoadLanes, getRoad } from './roadApi'

export interface RoadLaneEditorProps {
  road: RoadSummary
  details: RoadDetails
  getEngineClient: () => EngineClient | null
}

export interface DraftLane {
  laneId: string
  side: 'left' | 'right'
  laneIndex: number
  type: string
  direction: string
  width: string
}

export interface LanePreset {
  id: string
  name: string
  description: string
  createLanes: () => DraftLane[]
}

export const LANE_PRESETS: LanePreset[] = [
  {
    id: 'standard-2-lane',
    name: 'Standard 2-Lane (Bidirectional)',
    description: '1 driving lane each direction (3.5m each)',
    createLanes: () => [
      { laneId: 'lane-l1', side: 'left', laneIndex: 0, type: 'driving', direction: 'backward', width: '3.5' },
      { laneId: 'lane-r1', side: 'right', laneIndex: 0, type: 'driving', direction: 'forward', width: '3.5' },
    ],
  },
  {
    id: 'one-way-single',
    name: 'One-Way Single Lane',
    description: '1 forward driving lane (3.5m)',
    createLanes: () => [
      { laneId: 'lane-r1', side: 'right', laneIndex: 0, type: 'driving', direction: 'forward', width: '3.5' },
    ],
  },
  {
    id: 'four-lane-divided',
    name: '4-Lane Divided',
    description: '2 driving lanes each direction (3.5m each)',
    createLanes: () => [
      { laneId: 'lane-l1', side: 'left', laneIndex: 0, type: 'driving', direction: 'backward', width: '3.5' },
      { laneId: 'lane-l2', side: 'left', laneIndex: 1, type: 'driving', direction: 'backward', width: '3.5' },
      { laneId: 'lane-r1', side: 'right', laneIndex: 0, type: 'driving', direction: 'forward', width: '3.5' },
      { laneId: 'lane-r2', side: 'right', laneIndex: 1, type: 'driving', direction: 'forward', width: '3.5' },
    ],
  },
  {
    id: 'urban-complete-street',
    name: 'Urban Complete Street',
    description: 'Sidewalk (2m) + Bike (1.5m) + Driving (3.5m) both sides',
    createLanes: () => [
      { laneId: 'lane-l1', side: 'left', laneIndex: 0, type: 'driving', direction: 'backward', width: '3.5' },
      { laneId: 'lane-l2', side: 'left', laneIndex: 1, type: 'bikelane', direction: 'backward', width: '1.5' },
      { laneId: 'lane-l3', side: 'left', laneIndex: 2, type: 'sidewalk', direction: 'none', width: '2.0' },
      { laneId: 'lane-r1', side: 'right', laneIndex: 0, type: 'driving', direction: 'forward', width: '3.5' },
      { laneId: 'lane-r2', side: 'right', laneIndex: 1, type: 'bikelane', direction: 'forward', width: '1.5' },
      { laneId: 'lane-r3', side: 'right', laneIndex: 2, type: 'sidewalk', direction: 'none', width: '2.0' },
    ],
  },
  {
    id: 'rural-highway-shoulders',
    name: 'Rural Highway with Shoulders',
    description: 'Driving (3.5m) + Paved shoulder (2m) each side',
    createLanes: () => [
      { laneId: 'lane-l1', side: 'left', laneIndex: 0, type: 'driving', direction: 'backward', width: '3.5' },
      { laneId: 'lane-l2', side: 'left', laneIndex: 1, type: 'shoulder', direction: 'none', width: '2.0' },
      { laneId: 'lane-r1', side: 'right', laneIndex: 0, type: 'driving', direction: 'forward', width: '3.5' },
      { laneId: 'lane-r2', side: 'right', laneIndex: 1, type: 'shoulder', direction: 'none', width: '2.0' },
    ],
  },
]

export const LANE_TYPES = [
  { value: 'driving', label: 'Driving' },
  { value: 'shoulder', label: 'Shoulder' },
  { value: 'sidewalk', label: 'Sidewalk' },
  { value: 'bikelane', label: 'Bike Lane' },
  { value: 'parking', label: 'Parking' },
  { value: 'median', label: 'Median' },
]

export const LANE_DIRECTIONS = [
  { value: 'forward', label: 'Forward' },
  { value: 'backward', label: 'Backward' },
  { value: 'bidirectional', label: 'Bidirectional' },
  { value: 'none', label: 'None' },
]

function lanesFromDetails(details: RoadDetails): DraftLane[] {
  if (details.laneSections.length > 0 && details.laneSections[0]!.lanes.length > 0) {
    return details.laneSections[0]!.lanes.map((lane) => ({
      laneId: lane.laneId,
      side: lane.side === 'left' ? 'left' : 'right',
      laneIndex: lane.laneIndex,
      type: lane.type || 'driving',
      direction: lane.direction || (lane.side === 'left' ? 'backward' : 'forward'),
      width: String(lane.width > 0 ? lane.width : 3.5),
    }))
  }
  // Default fallback if lane sections are somehow empty
  return LANE_PRESETS[0]!.createLanes()
}

export function RoadLaneEditor({ road, details, getEngineClient }: RoadLaneEditorProps) {
  const [lanes, setLanes] = useState<DraftLane[]>(() => lanesFromDetails(details))
  const [submitting, setSubmitting] = useState(false)
  const [feedback, setFeedback] = useState<string | null>(null)

  useEffect(() => {
    setLanes(lanesFromDetails(details))
    setFeedback(null)
  }, [details])

  const leftLanes = lanes
    .filter((l) => l.side === 'left')
    .sort((a, b) => a.laneIndex - b.laneIndex)
  const rightLanes = lanes
    .filter((l) => l.side === 'right')
    .sort((a, b) => a.laneIndex - b.laneIndex)

  const applyPreset = (preset: LanePreset) => {
    setLanes(preset.createLanes())
    setFeedback(`Applied preset: ${preset.name}`)
  }

  const addLane = (side: 'left' | 'right') => {
    const existing = side === 'left' ? leftLanes : rightLanes
    const nextIndex = existing.length
    const newLane: DraftLane = {
      laneId: `lane-${side[0]}${nextIndex + 1}`,
      side,
      laneIndex: nextIndex,
      type: 'driving',
      direction: side === 'left' ? 'backward' : 'forward',
      width: '3.5',
    }
    setLanes((current) => [...current, newLane])
    setFeedback(null)
  }

  const removeLane = (laneId: string, side: 'left' | 'right') => {
    setLanes((current) => {
      const remaining = current.filter((l) => l.laneId !== laneId)
      // Re-index remaining lanes for that side
      let idx = 0
      return remaining.map((l) => {
        if (l.side === side) {
          return { ...l, laneIndex: idx++ }
        }
        return l
      })
    })
    setFeedback(null)
  }

  const updateLane = (laneId: string, field: keyof DraftLane, value: string | number) => {
    setLanes((current) =>
      current.map((l) => (l.laneId === laneId ? { ...l, [field]: value } : l)),
    )
    setFeedback(null)
  }

  const saveLanes = async () => {
    const client = getEngineClient()
    if (!client) return

    // Validation
    if (lanes.length === 0) {
      setFeedback('A road must have at least one lane.')
      return
    }

    for (const lane of lanes) {
      const w = Number(lane.width)
      if (!Number.isFinite(w) || w <= 0) {
        setFeedback(`Lane ${lane.laneId} must have a positive finite width.`)
        return
      }
    }

    setSubmitting(true)
    setFeedback(null)

    try {
      const sectionInfo: RoadLaneSectionInfo = create(RoadLaneSectionInfoSchema, {
        sectionIndex: 0,
        startStation: 0,
        endStation: road.length,
        lanes: lanes.map((l, i) =>
          create(RoadLaneInfoSchema, {
            laneId: l.laneId || `lane-${l.side[0]}${i + 1}`,
            sectionIndex: 0,
            side: l.side,
            laneIndex: l.laneIndex,
            type: l.type,
            direction: l.direction,
            width: Number(l.width),
          }),
        ),
      })

      await updateRoadLanes(client, road.roadId, [sectionInfo])
      await getRoad(client, road.roadId)
      setFeedback('Lane configuration saved successfully.')
    } catch (error) {
      setFeedback(error instanceof Error ? error.message : String(error))
    } finally {
      setSubmitting(false)
    }
  }

  const totalLeftWidth = leftLanes.reduce((acc, l) => acc + (Number(l.width) || 0), 0)
  const totalRightWidth = rightLanes.reduce((acc, l) => acc + (Number(l.width) || 0), 0)

  const laneColorByType: Record<string, string> = {
    driving: 'var(--color-primary-subtle, rgba(59, 130, 246, 0.25))',
    shoulder: 'var(--color-warning-subtle, rgba(234, 179, 8, 0.25))',
    sidewalk: 'var(--color-success-subtle, rgba(34, 197, 94, 0.25))',
    bikelane: 'var(--color-info-subtle, rgba(6, 182, 212, 0.25))',
    parking: 'var(--color-neutral-subtle, rgba(148, 163, 184, 0.25))',
    median: 'var(--color-danger-subtle, rgba(239, 68, 68, 0.25))',
  }

  return (
    <div className="road-lane-editor" aria-label="Road lane editor" style={{ display: 'flex', flexDirection: 'column', gap: '12px' }}>
      {/* Metrics and Presets Header */}
      <div className="profile-metrics-bar" style={{ display: 'flex', gap: '16px', alignItems: 'center', flexWrap: 'wrap' }}>
        <span className="profile-metric">
          <strong>Left Width:</strong> {totalLeftWidth.toFixed(2)}m ({leftLanes.length} lanes)
        </span>
        <span className="profile-metric">
          <strong>Right Width:</strong> {totalRightWidth.toFixed(2)}m ({rightLanes.length} lanes)
        </span>
        <span className="profile-metric">
          <strong>Total Road Width:</strong> {(totalLeftWidth + totalRightWidth).toFixed(2)}m
        </span>
        <div style={{ marginLeft: 'auto', display: 'flex', gap: '8px', alignItems: 'center' }}>
          <label style={{ fontSize: '12px', display: 'flex', alignItems: 'center', gap: '4px' }}>
            Preset:
            <select
              aria-label="Lane presets"
              defaultValue=""
              onChange={(e) => {
                const found = LANE_PRESETS.find((p) => p.id === e.target.value)
                if (found) applyPreset(found)
              }}
              style={{ fontSize: '12px', padding: '2px 6px' }}
            >
              <option value="" disabled>Select a preset…</option>
              {LANE_PRESETS.map((p) => (
                <option key={p.id} value={p.id}>{p.name}</option>
              ))}
            </select>
          </label>
          <button
            type="button"
            className="button primary"
            onClick={() => void saveLanes()}
            disabled={submitting}
          >
            {submitting ? 'Saving…' : 'Save Lanes'}
          </button>
        </div>
      </div>

      {feedback ? (
        <div
          role="status"
          style={{
            padding: '4px 8px',
            fontSize: '12px',
            borderRadius: '4px',
            backgroundColor: feedback.includes('success') ? 'rgba(34, 197, 94, 0.15)' : 'rgba(239, 68, 68, 0.15)',
            color: feedback.includes('success') ? '#22c55e' : '#ef4444',
          }}
        >
          {feedback}
        </div>
      ) : null}

      {/* Cross-section Visual Diagram */}
      <div
        className="lane-diagram"
        aria-label="Cross-section diagram"
        style={{
          border: '1px solid var(--if-border-default, #333)',
          borderRadius: '6px',
          padding: '12px',
          backgroundColor: 'var(--if-bg-subtle, rgba(0, 0, 0, 0.15))',
          display: 'flex',
          flexDirection: 'column',
          gap: '6px',
        }}
      >
        <div style={{ fontSize: '11px', color: 'var(--if-text-muted, #888)', textTransform: 'uppercase', letterSpacing: '0.05em' }}>
          Cross-Section Preview (Looking Forward)
        </div>
        <div style={{ display: 'flex', height: '48px', width: '100%', borderRadius: '4px', overflow: 'hidden', border: '1px solid #444' }}>
          {/* Left lanes: outer to inner */}
          {[...leftLanes].reverse().map((lane) => {
            const w = Number(lane.width) || 1
            const pct = (w / (totalLeftWidth + totalRightWidth || 1)) * 100
            return (
              <div
                key={lane.laneId}
                style={{
                  width: `${pct}%`,
                  backgroundColor: laneColorByType[lane.type] || 'rgba(100, 100, 100, 0.2)',
                  borderRight: '1px dashed #666',
                  display: 'flex',
                  flexDirection: 'column',
                  alignItems: 'center',
                  justifyContent: 'center',
                  fontSize: '10px',
                  fontWeight: 600,
                  overflow: 'hidden',
                  whiteSpace: 'nowrap',
                }}
                title={`Left #${lane.laneIndex + 1} (${lane.type}): ${w}m`}
              >
                <span>{lane.type}</span>
                <span style={{ fontSize: '9px', opacity: 0.8 }}>{w}m ⇦</span>
              </div>
            )
          })}

          {/* Centerline Divider */}
          <div
            style={{
              width: '4px',
              backgroundColor: '#eab308',
              boxShadow: '0 0 4px rgba(234, 179, 8, 0.8)',
              zIndex: 1,
            }}
            title="Road Centerline"
          />

          {/* Right lanes: inner to outer */}
          {rightLanes.map((lane) => {
            const w = Number(lane.width) || 1
            const pct = (w / (totalLeftWidth + totalRightWidth || 1)) * 100
            return (
              <div
                key={lane.laneId}
                style={{
                  width: `${pct}%`,
                  backgroundColor: laneColorByType[lane.type] || 'rgba(100, 100, 100, 0.2)',
                  borderRight: '1px dashed #666',
                  display: 'flex',
                  flexDirection: 'column',
                  alignItems: 'center',
                  justifyContent: 'center',
                  fontSize: '10px',
                  fontWeight: 600,
                  overflow: 'hidden',
                  whiteSpace: 'nowrap',
                }}
                title={`Right #${lane.laneIndex + 1} (${lane.type}): ${w}m`}
              >
                <span>{lane.type}</span>
                <span style={{ fontSize: '9px', opacity: 0.8 }}>{w}m ⇨</span>
              </div>
            )
          })}
        </div>
      </div>

      {/* Lane Tables: Left Side and Right Side */}
      <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '16px' }}>
        {/* Left Lanes Panel */}
        <div style={{ display: 'flex', flexDirection: 'column', gap: '8px' }}>
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
            <h4 style={{ margin: 0, fontSize: '13px' }}>Left Lanes (Outward from Center)</h4>
            <button
              type="button"
              className="button secondary"
              style={{ fontSize: '11px', padding: '2px 8px' }}
              onClick={() => addLane('left')}
            >
              + Add Left Lane
            </button>
          </div>
          <table className="profile-breakpoint-table" style={{ width: '100%', fontSize: '12px' }}>
            <thead>
              <tr>
                <th>Index</th>
                <th>Type</th>
                <th>Direction</th>
                <th>Width (m)</th>
                <th>Action</th>
              </tr>
            </thead>
            <tbody>
              {leftLanes.map((lane) => (
                <tr key={lane.laneId}>
                  <td>L{lane.laneIndex + 1}</td>
                  <td>
                    <select
                      aria-label={`Left lane ${lane.laneIndex + 1} type`}
                      value={lane.type}
                      onChange={(e) => updateLane(lane.laneId, 'type', e.target.value)}
                    >
                      {LANE_TYPES.map((t) => (
                        <option key={t.value} value={t.value}>{t.label}</option>
                      ))}
                    </select>
                  </td>
                  <td>
                    <select
                      aria-label={`Left lane ${lane.laneIndex + 1} direction`}
                      value={lane.direction}
                      onChange={(e) => updateLane(lane.laneId, 'direction', e.target.value)}
                    >
                      {LANE_DIRECTIONS.map((d) => (
                        <option key={d.value} value={d.value}>{d.label}</option>
                      ))}
                    </select>
                  </td>
                  <td>
                    <input
                      aria-label={`Left lane ${lane.laneIndex + 1} width`}
                      type="number"
                      min="0.1"
                      step="0.1"
                      value={lane.width}
                      onChange={(e) => updateLane(lane.laneId, 'width', e.target.value)}
                      style={{ width: '60px' }}
                    />
                  </td>
                  <td>
                    <button
                      type="button"
                      onClick={() => removeLane(lane.laneId, 'left')}
                      style={{ fontSize: '11px' }}
                    >
                      Remove
                    </button>
                  </td>
                </tr>
              ))}
              {leftLanes.length === 0 ? (
                <tr>
                  <td colSpan={5} style={{ textAlign: 'center', color: '#888' }}>
                    No left lanes (one-way right).
                  </td>
                </tr>
              ) : null}
            </tbody>
          </table>
        </div>

        {/* Right Lanes Panel */}
        <div style={{ display: 'flex', flexDirection: 'column', gap: '8px' }}>
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
            <h4 style={{ margin: 0, fontSize: '13px' }}>Right Lanes (Outward from Center)</h4>
            <button
              type="button"
              className="button secondary"
              style={{ fontSize: '11px', padding: '2px 8px' }}
              onClick={() => addLane('right')}
            >
              + Add Right Lane
            </button>
          </div>
          <table className="profile-breakpoint-table" style={{ width: '100%', fontSize: '12px' }}>
            <thead>
              <tr>
                <th>Index</th>
                <th>Type</th>
                <th>Direction</th>
                <th>Width (m)</th>
                <th>Action</th>
              </tr>
            </thead>
            <tbody>
              {rightLanes.map((lane) => (
                <tr key={lane.laneId}>
                  <td>R{lane.laneIndex + 1}</td>
                  <td>
                    <select
                      aria-label={`Right lane ${lane.laneIndex + 1} type`}
                      value={lane.type}
                      onChange={(e) => updateLane(lane.laneId, 'type', e.target.value)}
                    >
                      {LANE_TYPES.map((t) => (
                        <option key={t.value} value={t.value}>{t.label}</option>
                      ))}
                    </select>
                  </td>
                  <td>
                    <select
                      aria-label={`Right lane ${lane.laneIndex + 1} direction`}
                      value={lane.direction}
                      onChange={(e) => updateLane(lane.laneId, 'direction', e.target.value)}
                    >
                      {LANE_DIRECTIONS.map((d) => (
                        <option key={d.value} value={d.value}>{d.label}</option>
                      ))}
                    </select>
                  </td>
                  <td>
                    <input
                      aria-label={`Right lane ${lane.laneIndex + 1} width`}
                      type="number"
                      min="0.1"
                      step="0.1"
                      value={lane.width}
                      onChange={(e) => updateLane(lane.laneId, 'width', e.target.value)}
                      style={{ width: '60px' }}
                    />
                  </td>
                  <td>
                    <button
                      type="button"
                      onClick={() => removeLane(lane.laneId, 'right')}
                      style={{ fontSize: '11px' }}
                    >
                      Remove
                    </button>
                  </td>
                </tr>
              ))}
              {rightLanes.length === 0 ? (
                <tr>
                  <td colSpan={5} style={{ textAlign: 'center', color: '#888' }}>
                    No right lanes.
                  </td>
                </tr>
              ) : null}
            </tbody>
          </table>
        </div>
      </div>
    </div>
  )
}
