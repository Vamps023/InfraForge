import { inspectorSectionRegistry, type InspectorSectionContext } from '../../editor/inspector/inspectorRegistry'
import { useProjectStore } from '../project/projectStore'
import { useGeoStore } from '../geo/geoStore'
import { useShellUiStore } from '../../editor/shell/shellUiStore'

export function registerWorldInspectorSection(): void {
  inspectorSectionRegistry.register({
    id: 'world-georeference',
    label: 'Spatial Reference & Georeference',
    category: 'geometry',
    order: 10,
    applies: (context) => isWorldContext(context),
    render: () => <WorldGeoreferenceBody />,
  })
}

export function unregisterWorldInspectorSection(): void {
  inspectorSectionRegistry.unregister('world-georeference')
}

function isWorldContext(context: InspectorSectionContext): boolean {
  if (context.selectedIds.length === 0) {
    return true
  }
  const summary = useProjectStore.getState().summary
  if (summary && context.selectedIds.length === 1 && context.selectedIds[0] === summary.projectUuid) {
    return true
  }
  return false
}

function WorldGeoreferenceBody() {
  const summary = useProjectStore((state) => state.summary)
  const info = useGeoStore((state) => state.info)
  const openDialogCommand = useShellUiStore((state) => state.openDialogCommand)

  if (!summary) {
    return <div className="panel-empty">No project is open.</div>
  }

  const config = info?.config ?? summary.georeference

  return (
    <div className="world-inspector-body">
      <dl className="inspector-fields">
        <div className="inspector-field">
          <dt>Horizontal CRS</dt>
          <dd>{config?.horizontalCrs || '—'}</dd>
        </div>
        <div className="inspector-field">
          <dt>Linear Unit</dt>
          <dd>{config?.linearUnit || '—'}</dd>
        </div>
        <div className="inspector-field">
          <dt>Vertical CRS</dt>
          <dd>{config?.verticalCrs || '—'}</dd>
        </div>
        <div className="inspector-field">
          <dt>Origin Easting</dt>
          <dd>{config?.originEasting !== undefined ? String(config.originEasting) : '—'}</dd>
        </div>
        <div className="inspector-field">
          <dt>Origin Northing</dt>
          <dd>{config?.originNorthing !== undefined ? String(config.originNorthing) : '—'}</dd>
        </div>
        <div className="inspector-field">
          <dt>Origin Height</dt>
          <dd>{config?.originHeight !== undefined ? String(config.originHeight) : '—'}</dd>
        </div>
      </dl>
      <div className="inspector-actions" style={{ marginTop: 'var(--space-sm)' }}>
        <button
          type="button"
          className="button button-secondary"
          onClick={() => openDialogCommand('georeference')}
        >
          Configure Georeference…
        </button>
      </div>
    </div>
  )
}
