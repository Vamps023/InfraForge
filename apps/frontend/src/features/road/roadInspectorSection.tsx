import { inspectorSectionRegistry, type InspectorSectionContext } from '../../editor/inspector/inspectorRegistry'
import { useRoadStore } from './roadStore'
import { getRoad, deleteRoadControl, moveRoadControl, insertRoadControl } from './roadApi'
import { useRoadToolStore } from './roadToolStore'
import type { EngineClient } from '../../lib/engineSession'

// Road inspector section: shows real canonical road metadata for the
// selected road through the Issue #5 inspector section registry.
// Blocker 21: the manual "Update Road Scene" button has been removed.
// Road scene updates flow through the event-driven scene publisher
// (roadEvents.ts -> scenePublisher -> desktop -> viewport) and require
// no manual user action.

export interface RoadInspectorDeps {
  getEngineClient: () => EngineClient | null
}

function RoadInspectorContent({ roadId, client }: { roadId: string; client: EngineClient | null }) {
  const road = useRoadStore((state) => state.roads.find((r) => r.roadId === roadId))
  const details = useRoadStore((state) => state.details)
  const lastError = useRoadStore((state) => state.lastError)

  if (!road) {
    return null
  }

  return (
    <div className="inspector-section road-inspector">
      {lastError ? (
        <div
          className="inspector-error"
          role="alert"
          style={{
            padding: '6px 8px',
            marginBottom: '8px',
            fontSize: '12px',
            color: 'var(--color-danger, #ef4444)',
            backgroundColor: 'var(--color-danger-subtle, rgba(239, 68, 68, 0.1))',
            border: '1px solid var(--color-danger-border, rgba(239, 68, 68, 0.2))',
            borderRadius: '4px',
            wordBreak: 'break-word',
          }}
        >
          <strong>Error:</strong> {lastError}
        </div>
      ) : null}
      <dl className="inspector-fields">
        <dt>Road ID</dt>
        <dd className="mono">{road.roadId}</dd>
        <dt>Name</dt>
        <dd>{road.name}</dd>
        <dt>Length</dt>
        <dd>{road.length.toFixed(3)} project units</dd>
        <dt>Alignment Segments</dt>
        <dd>{road.alignmentSegmentCount}</dd>
        <dt>Protected Anchors</dt>
        <dd>{road.protectedAnchorCount}</dd>
        {road.sourceProvider ? (
          <>
            <dt>Source Provider</dt>
            <dd>{road.sourceProvider}</dd>
          </>
        ) : null}
        {road.sourceId ? (
          <>
            <dt>Source ID</dt>
            <dd className="mono">{road.sourceId}</dd>
          </>
        ) : null}
        <dt>Revision</dt>
        <dd>{road.revision.toString()}</dd>
        {details && details.roadId === roadId && details.controlPoints.length > 0 ? (
          <>
            <dt>Control Points ({details.controlPoints.length})</dt>
            <dd>
              <ol className="road-controls">
                {details.controlPoints.map((control, index) => (
                  <li key={index}>
                    <span className="mono">
                      {control.easting.toFixed(3)}, {control.northing.toFixed(3)}
                    </span>
                    {control.protectedAnchor ? <strong> Protected</strong> : (
                      <>
                        <button type="button" onClick={() =>
                          useRoadToolStore.getState().beginMove(roadId, index, async (easting, northing) => {
                            if (!client) return
                            await moveRoadControl(client, roadId, index, easting, northing)
                            await getRoad(client, roadId)
                          })}>Move in viewport</button>
                        <button type="button" onClick={() => {
                          if (!client) return
                          void deleteRoadControl(client, roadId, index)
                            .then(() => getRoad(client, roadId))
                        }}>Delete</button>
                      </>
                    )}
                    <button type="button" onClick={() =>
                      useRoadToolStore.getState().beginInsert(roadId, index + 1, async (easting, northing) => {
                        if (!client) return
                        await insertRoadControl(client, roadId, index + 1, easting, northing)
                        await getRoad(client, roadId)
                      })}>Insert after</button>
                  </li>
                ))}
              </ol>
            </dd>
          </>
        ) : null}
        {details && details.roadId === roadId && details.laneSections.length > 0 ? (
          <>
            <dt>Lane Sections ({details.laneSections.length})</dt>
            <dd>
              {details.laneSections.map((sec, idx) => (
                <div key={idx} style={{ marginBottom: '4px' }}>
                  <span>Section {sec.sectionIndex + 1} ({sec.startStation.toFixed(1)}m – {sec.endStation.toFixed(1)}m): </span>
                  <strong>{sec.lanes.length} lanes</strong> ({sec.lanes.filter((l) => l.side === 'left').length} left, {sec.lanes.filter((l) => l.side === 'right').length} right)
                </div>
              ))}
            </dd>
          </>
        ) : null}
        {details && details.diagnostics.length > 0 ? (
          <>
            <dt>Diagnostics</dt>
            <dd>
              <ul className="road-diagnostics">
                {details.diagnostics.map((diag, i) => (
                  <li key={i}>
                    {diag.code}: {diag.message}
                  </li>
                ))}
              </ul>
            </dd>
          </>
        ) : null}
      </dl>
    </div>
  )
}

export function registerRoadInspectorSection(deps: RoadInspectorDeps): void {
  const section = {
    id: 'road',
    label: 'Road',
    category: 'geometry' as const,
    order: 0,
    applies: (context: InspectorSectionContext) => {
      const id = context.primaryId
      return id !== null && id.startsWith('road:')
    },
    render: (context: InspectorSectionContext) => {
      const id = context.primaryId
      if (!id || !id.startsWith('road:')) {
        return null
      }
      const roadId = id.slice('road:'.length)
      const client = deps.getEngineClient()
      return <RoadInspectorContent roadId={roadId} client={client} />
    },
  }

  inspectorSectionRegistry.register(section)
}

export function unregisterRoadInspectorSection(): void {
  inspectorSectionRegistry.unregister('road')
}
