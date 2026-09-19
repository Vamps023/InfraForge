import { inspectorSectionRegistry, type InspectorSectionContext } from '../../editor/inspector/inspectorRegistry'
import { useRoadStore } from './roadStore'
import { deleteJunction, updateJunction } from './roadApi'
import type { EngineClient } from '../../lib/engineSession'

export interface JunctionInspectorDeps {
  getEngineClient: () => EngineClient | null
}

const JUNCTION_TYPES = [
  { value: 'priority', label: 'Priority / Stop' },
  { value: 'signalized', label: 'Signalized' },
  { value: 'roundabout', label: 'Roundabout' },
  { value: 'all_way_stop', label: 'All-Way Stop' },
  { value: 'uncontrolled', label: 'Uncontrolled' },
]

function JunctionInspectorContent({ junctionId, client }: { junctionId: string; client: EngineClient | null }) {
  const junction = useRoadStore((state) => state.junctions.find((j) => j.junctionId === junctionId))
  const lastError = useRoadStore((state) => state.lastError)

  if (!junction) {
    return <div className="inspector-section">Junction not found.</div>
  }

  const handleTypeChange = async (newType: string) => {
    if (!client) return
    await updateJunction(client, junctionId, {
      type: newType,
      name: junction.name,
      posX: junction.posX,
      posY: junction.posY,
      elevation: junction.elevation,
      approaches: junction.approaches,
      connections: junction.connections,
    })
  }

  const handleDelete = async () => {
    if (!client) return
    await deleteJunction(client, junctionId)
  }

  return (
    <div className="inspector-section junction-inspector">
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
        <dt>Junction ID</dt>
        <dd className="mono">{junction.junctionId}</dd>

        <dt>Name</dt>
        <dd>{junction.name || 'Unnamed Junction'}</dd>

        <dt>Control Type</dt>
        <dd>
          <select
            aria-label="Junction control type"
            value={junction.type || 'priority'}
            onChange={(e) => void handleTypeChange(e.target.value)}
          >
            {JUNCTION_TYPES.map((t) => (
              <option key={t.value} value={t.value}>{t.label}</option>
            ))}
          </select>
        </dd>

        <dt>Position</dt>
        <dd className="mono">
          ({junction.posX.toFixed(3)}, {junction.posY.toFixed(3)}) @ {junction.elevation.toFixed(3)}m
        </dd>

        <dt>Revision</dt>
        <dd>{junction.revision.toString()}</dd>

        <dt>Approaches ({junction.approaches.length})</dt>
        <dd>
          {junction.approaches.length > 0 ? (
            <ul className="junction-approaches" style={{ paddingLeft: '16px', margin: '4px 0' }}>
              {junction.approaches.map((app, idx) => (
                <li key={idx}>
                  <span className="mono">Road {app.roadId.slice(0, 8)}…</span> ({app.contactPoint}) - {(app.heading * (180 / Math.PI)).toFixed(1)}°
                </li>
              ))}
            </ul>
          ) : (
            <span style={{ color: '#888' }}>None</span>
          )}
        </dd>

        <dt>Connections ({junction.connections.length})</dt>
        <dd>
          {junction.connections.length > 0 ? (
            <ul className="junction-connections" style={{ paddingLeft: '16px', margin: '4px 0' }}>
              {junction.connections.map((conn, idx) => (
                <li key={idx}>
                  {conn.movementType} ({conn.allowed ? 'allowed' : 'restricted'})
                </li>
              ))}
            </ul>
          ) : (
            <span style={{ color: '#888' }}>None</span>
          )}
        </dd>
      </dl>

      <div style={{ marginTop: '12px' }}>
        <button
          type="button"
          className="button secondary"
          style={{ color: 'var(--color-danger, #ef4444)' }}
          onClick={() => void handleDelete()}
        >
          Delete Junction
        </button>
      </div>
    </div>
  )
}

export function registerJunctionInspectorSection(deps: JunctionInspectorDeps): void {
  const section = {
    id: 'junction',
    label: 'Junction',
    category: 'geometry' as const,
    order: 0,
    applies: (context: InspectorSectionContext) => {
      const id = context.primaryId
      return id !== null && id.startsWith('junction:')
    },
    render: (context: InspectorSectionContext) => {
      const id = context.primaryId
      if (!id || !id.startsWith('junction:')) {
        return null
      }
      const junctionId = id.slice('junction:'.length)
      const client = deps.getEngineClient()
      return <JunctionInspectorContent junctionId={junctionId} client={client} />
    },
  }

  inspectorSectionRegistry.register(section)
}

export function unregisterJunctionInspectorSection(): void {
  inspectorSectionRegistry.unregister('junction')
}
