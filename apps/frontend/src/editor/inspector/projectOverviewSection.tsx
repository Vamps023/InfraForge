import { inspectorSectionRegistry } from './inspectorRegistry'
import { useProjectStore } from '../../features/project/projectStore'

// Project overview inspector section. This is the only real data currently
// available to inspect: the canonical project summary projected from the
// engine. It applies when no entity is selected (project-level overview).
// No fake road/terrain properties are injected.
export function registerProjectOverviewSection(): void {
  inspectorSectionRegistry.register({
    id: 'project-overview',
    label: 'Project',
    order: 10,
    applies: (context) => context.selectedIds.length === 0,
    render: () => <ProjectOverviewBody />,
  })
}

function ProjectOverviewBody() {
  const summary = useProjectStore((state) => state.summary)
  if (!summary) {
    return <div className="panel-empty">No project is open.</div>
  }
  return (
    <dl className="inspector-fields">
      <div className="inspector-field">
        <dt>Name</dt>
        <dd>{summary.displayName}</dd>
      </div>
      <div className="inspector-field">
        <dt>Directory</dt>
        <dd title={summary.directory}>{summary.directory}</dd>
      </div>
      <div className="inspector-field">
        <dt>Revision</dt>
        <dd>{String(summary.revision)}</dd>
      </div>
      <div className="inspector-field">
        <dt>Unsaved</dt>
        <dd>{summary.dirty ? 'Yes' : 'No'}</dd>
      </div>
      <div className="inspector-field">
        <dt>Horizontal CRS</dt>
        <dd>{summary.georeference?.horizontalCrs || '—'}</dd>
      </div>
      <div className="inspector-field">
        <dt>Linear unit</dt>
        <dd>{summary.georeference?.linearUnit || '—'}</dd>
      </div>
      <div className="inspector-field">
        <dt>Traffic side</dt>
        <dd>{summary.trafficSide === 1 ? 'Left' : summary.trafficSide === 2 ? 'Right' : '—'}</dd>
      </div>
    </dl>
  )
}

export function unregisterProjectOverviewSection(): void {
  inspectorSectionRegistry.unregister('project-overview')
}
