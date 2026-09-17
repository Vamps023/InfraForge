import { useEffect, useState } from 'react'
import { useRoadStore } from './roadStore'
import { useSelectionStore } from '../../editor/selection/selectionStore'
import { conformRoadToTerrain, getRoad, updateRoadElevation, updateRoadSuperelevation, updateRoadWidth } from './roadApi'
import { contextEditorRegistry } from '../../editor/contextEditor/contextEditorRegistry'
import type { EngineClient } from '../../lib/engineSession'

export interface RoadProfileEditorProps { getEngineClient: () => EngineClient | null }
type ProfileKind = 'elevation' | 'superelevation' | 'width'
interface DraftBreakpoint { station: string; value: string; rightValue?: string }

export function registerRoadProfileContextEditor(deps: RoadProfileEditorProps): void {
  contextEditorRegistry.register({
    id: 'road-profile', label: 'Road Vertical Profile',
    applies: (ctx) => ctx.activeWorkspace === 'roads' && ctx.selectedIds.some((id) => id.startsWith('road:')),
    render: () => <RoadProfileEditor getEngineClient={deps.getEngineClient} />,
  })
}

export function unregisterRoadProfileContextEditor(): void { contextEditorRegistry.unregister('road-profile') }

function projectedRows(kind: ProfileKind, details: NonNullable<ReturnType<typeof useRoadStore.getState>['details']>): DraftBreakpoint[] {
  if (kind === 'width') {
    return details.widthBreakpoints.map((breakpoint) => ({
      station: String(breakpoint.station),
      value: String(breakpoint.leftWidth),
      rightValue: String(breakpoint.rightWidth),
    }))
  }
  const source = kind === 'elevation' ? details.elevationBreakpoints : details.superelevationBreakpoints
  return source.map((breakpoint) => ({ station: String(breakpoint.station), value: String(breakpoint.value) }))
}

export function RoadProfileEditor({ getEngineClient }: RoadProfileEditorProps) {
  const primaryId = useSelectionStore((state) => state.primaryId)
  const roadId = primaryId?.startsWith('road:') ? primaryId.slice('road:'.length) : null
  const roads = useRoadStore((state) => state.roads)
  const details = useRoadStore((state) => state.details)
  const road = roads.find((candidate) => candidate.roadId === roadId)
  const matchingDetails = details?.roadId === roadId ? details : null
  const [kind, setKind] = useState<ProfileKind>('elevation')
  const [rows, setRows] = useState<DraftBreakpoint[]>([])
  const [submitting, setSubmitting] = useState(false)
  const [feedback, setFeedback] = useState<string | null>(null)
  const [terrainInterval, setTerrainInterval] = useState('10')
  const [terrainOffset, setTerrainOffset] = useState('0.1')

  useEffect(() => {
    setRows(matchingDetails ? projectedRows(kind, matchingDetails) : [])
    setFeedback(null)
  }, [kind, matchingDetails])

  if (!road || !roadId) return <div className="context-editor-empty">Select a road to edit its vertical profile.</div>

  const updateRow = (index: number, field: keyof DraftBreakpoint, value: string) => {
    setRows((current) => current.map((row, rowIndex) => rowIndex === index ? { ...row, [field]: value } : row))
  }

  const addRow = () => {
    const lastStation = Number(rows.at(-1)?.station ?? 0)
    const nextStation = rows.length === 0 ? 0 : Math.min(road.length, lastStation + 10)
    setRows((current) => [...current, {
      station: String(nextStation),
      value: kind === 'width' ? '5' : '0',
      ...(kind === 'width' ? { rightValue: '5' } : {}),
    }])
  }

  const save = async () => {
    const client = getEngineClient()
    if (!client || !matchingDetails) return
    const stations = rows.map((row) => Number(row.station))
    const values = rows.map((row) => Number(row.value))
    const rightValues = rows.map((row) => Number(row.rightValue))
    if (stations.some((station) => !Number.isFinite(station)) || values.some((value) => !Number.isFinite(value))
      || (kind === 'width' && rightValues.some((value) => !Number.isFinite(value)))) {
      setFeedback('Every station and value must be a finite number.'); return
    }
    if (kind === 'width' && (values.some((value) => value < 0) || rightValues.some((value) => value < 0))) {
      setFeedback('Road widths cannot be negative.'); return
    }
    if (stations.some((station) => station < 0 || station > road.length)) {
      setFeedback(`Stations must be between 0 and ${road.length.toFixed(3)}.`); return
    }
    if (stations.some((station, index) => index > 0 && station <= stations[index - 1]!)) {
      setFeedback('Stations must be strictly increasing.'); return
    }
    setSubmitting(true); setFeedback(null)
    try {
      if (kind === 'elevation') await updateRoadElevation(client, roadId, stations, values)
      else if (kind === 'superelevation') await updateRoadSuperelevation(client, roadId, stations, values)
      else await updateRoadWidth(client, roadId, stations, values, rightValues)
      if (useSelectionStore.getState().primaryId === `road:${roadId}`) {
        await getRoad(client, roadId)
        const label = kind === 'elevation' ? 'Elevation' : kind === 'superelevation' ? 'Superelevation' : 'Width'
        setFeedback(`${label} profile saved.`)
      }
    } catch (error) { setFeedback(error instanceof Error ? error.message : String(error)) }
    finally { setSubmitting(false) }
  }

  const applyTerrainConformance = async () => {
    const client = getEngineClient()
    if (!client || !matchingDetails) return
    const interval = Number(terrainInterval)
    const offset = Number(terrainOffset)
    if (!Number.isFinite(interval) || interval <= 0 || !Number.isFinite(offset)) {
      setFeedback('Terrain interval must be positive and offset must be finite.'); return
    }
    setSubmitting(true); setFeedback(null)
    try {
      await conformRoadToTerrain(client, roadId, interval, offset)
      if (useSelectionStore.getState().primaryId === `road:${roadId}`) {
        await getRoad(client, roadId)
        setKind('elevation')
        setFeedback('Elevation profile conformed to terrain.')
      }
    } catch (error) { setFeedback(error instanceof Error ? error.message : String(error)) }
    finally { setSubmitting(false) }
  }

  return <div className="road-profile-editor" aria-label="Road profile editor">
    <div className="profile-metrics-bar">
      <span className="profile-metric"><strong>Road:</strong> {road.name}</span>
      <span className="profile-metric"><strong>Length:</strong> {road.length.toFixed(2)} project units</span>
      <span className="profile-metric"><strong>Segments:</strong> {road.alignmentSegmentCount}</span>
    </div>
    <div className="profile-controls-row" role="tablist" aria-label="Road profile type">
      <button type="button" role="tab" aria-selected={kind === 'elevation'} onClick={() => setKind('elevation')}>Elevation</button>
      <button type="button" role="tab" aria-selected={kind === 'superelevation'} onClick={() => setKind('superelevation')}>Superelevation</button>
      <button type="button" role="tab" aria-selected={kind === 'width'} onClick={() => setKind('width')}>Width</button>
      <button type="button" className="button secondary" onClick={addRow} disabled={submitting || !matchingDetails}>Add breakpoint</button>
      <button type="button" className="button primary" onClick={() => void save()} disabled={submitting || !matchingDetails}>{submitting ? 'Saving…' : !matchingDetails ? 'Loading details…' : 'Save profile'}</button>
      <label>Terrain interval <input aria-label="Terrain conformance interval" type="number" min="0.01" step="1" value={terrainInterval} onChange={(event) => setTerrainInterval(event.target.value)} /></label>
      <label>Surface offset <input aria-label="Terrain conformance offset" type="number" step="0.1" value={terrainOffset} onChange={(event) => setTerrainOffset(event.target.value)} /></label>
      <button type="button" className="button secondary" onClick={() => void applyTerrainConformance()} disabled={submitting || !matchingDetails}>Conform to terrain</button>
      {feedback ? <span className="profile-feedback" role="status">{feedback}</span> : null}
    </div>
    {matchingDetails ? <div className="profile-breakpoint-table-wrap"><table className="profile-breakpoint-table">
      <thead><tr><th>Station (project units)</th><th>{kind === 'elevation' ? 'Elevation (project units)' : kind === 'superelevation' ? 'Superelevation (radians)' : 'Left width'}</th>{kind === 'width' ? <th>Right width</th> : null}<th>Action</th></tr></thead>
      <tbody>
        {rows.map((row, index) => <tr key={index}>
          <td><input aria-label={`Breakpoint ${index + 1} station`} type="number" min="0" max={road.length} step="0.1" value={row.station} onChange={(event) => updateRow(index, 'station', event.target.value)} /></td>
          <td><input aria-label={`Breakpoint ${index + 1} value`} type="number" min={kind === 'width' ? 0 : undefined} step={kind === 'superelevation' ? '0.001' : '0.1'} value={row.value} onChange={(event) => updateRow(index, 'value', event.target.value)} /></td>
          {kind === 'width' ? <td><input aria-label={`Breakpoint ${index + 1} right width`} type="number" min="0" step="0.1" value={row.rightValue ?? ''} onChange={(event) => updateRow(index, 'rightValue', event.target.value)} /></td> : null}
          <td><button type="button" onClick={() => setRows((current) => current.filter((_, rowIndex) => rowIndex !== index))}>Remove</button></td>
        </tr>)}
        {rows.length === 0 ? <tr><td colSpan={kind === 'width' ? 4 : 3}>{kind === 'width' ? 'No authored breakpoints. The canonical surface uses the default five-unit width on each side.' : 'No authored breakpoints. The canonical profile evaluates to zero.'}</td></tr> : null}
      </tbody>
    </table></div> : <div className="context-editor-empty">Loading canonical road profile…</div>}
  </div>
}
