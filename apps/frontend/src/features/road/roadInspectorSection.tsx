import { inspectorSectionRegistry, type InspectorSectionContext } from '../../editor/inspector/inspectorRegistry'
import { useRoadStore } from './roadStore'
import { getRoad } from './roadApi'
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

export function registerRoadInspectorSection(deps: RoadInspectorDeps): void {
  const section = {
    id: 'road',
    label: 'Road',
    order: 60,
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
      const roads = useRoadStore.getState().roads
      const road = roads.find((r) => r.roadId === roadId)
      const details = useRoadStore.getState().details

      const client = deps.getEngineClient()
      if (client && (!details || details.roadId !== roadId)) {
        void getRoad(client, roadId).catch(() => undefined)
      }

      if (!road) {
        return null
      }

      return (
        <div className="inspector-section road-inspector">
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
    },
  }

  inspectorSectionRegistry.register(section)
}

export function unregisterRoadInspectorSection(): void {
  inspectorSectionRegistry.unregister('road')
}
