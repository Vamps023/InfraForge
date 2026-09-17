import { inspectorSectionRegistry } from './inspectorRegistry'
import { useProjectStore } from '../../features/project/projectStore'
import type { InspectorSectionContext } from './inspectorRegistry'

// Project overview inspector section. This is the only real data currently
// available to inspect: the canonical project summary projected from the
// engine. It applies when no entity is selected (project-level overview)
// OR when the project root itself is selected (the Outliner's project-root
// projection surfaces the canonical project UUID as a selectable node).
// No fake road/terrain properties are injected.
export function registerProjectOverviewSection(): void {
  inspectorSectionRegistry.register({
    id: 'project-overview',
    label: 'Project',
    category: 'identity',
    order: 0,
    applies: (context) => isProjectOverviewContext(context),
    render: () => <ProjectOverviewBody />,
  })
}

function isProjectOverviewContext(context: InspectorSectionContext): boolean {
  if (context.selectedIds.length === 0) {
    return true
  }
  // When the project root is selected, show the project overview. The
  // project root's canonical ID is the backend project UUID.
  const summary = useProjectStore.getState().summary
  if (summary && context.selectedIds.length === 1 && context.selectedIds[0] === summary.projectUuid) {
    return true
  }
  return false
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
