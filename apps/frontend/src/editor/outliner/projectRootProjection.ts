import {
  outlinerProjectionRegistry,
  type OutlinerNode,
  type OutlinerProjection,
} from './outlinerProjection'
import { useProjectStore } from '../../features/project/projectStore'
import type { ProjectSummary } from '@infraforge/protocol'

// Project-root outliner projection. Surfaces the real canonical project
// root as a single outliner node sourced from the backend-projected
// ProjectSummary. This is NOT a fake domain projection — it uses the real
// canonical project UUID as the node ID and the real display name as the
// label. No fake terrain/road/world children are generated; `hasChildren`
// is false until real domain projections register child entities.
//
// The projection reacts to project open/close/display-name changes by
// subscribing to the project store. It emits a change notification whenever
// the summary changes so the Outliner re-reads getNodes().

const PROJECTION_ID = 'project-root'

let listener: (() => void) | null = null
let currentSummary: ProjectSummary | null = null
let registered = false
let storeUnsub: (() => void) | null = null

function getNodes(): OutlinerNode[] {
  if (currentSummary === null) {
    return []
  }
  return [
    {
      id: currentSummary.projectUuid,
      parentId: null,
      label: currentSummary.displayName,
      type: 'project',
      depth: 0,
      // No fake children — hasChildren is false until real domain
      // projections register child entities under this canonical project ID.
      hasChildren: false,
    },
  ]
}

const projection: OutlinerProjection = {
  id: PROJECTION_ID,
  label: 'Project',
  getNodes,
  subscribe: (l) => {
    listener = l
    return () => {
      listener = null
    }
  },
}

export function registerProjectRootProjection(): void {
  if (registered) {
    return
  }
  registered = true
  // Subscribe to the project store so the projection reacts to summary
  // changes (open/close/display-name/replacement).
  currentSummary = useProjectStore.getState().summary
  storeUnsub = useProjectStore.subscribe((state) => {
    if (state.summary !== currentSummary) {
      currentSummary = state.summary
      listener?.()
    }
  })
  outlinerProjectionRegistry.register(projection)
}

export function unregisterProjectRootProjection(): void {
  if (!registered) {
    return
  }
  registered = false
  storeUnsub?.()
  storeUnsub = null
  outlinerProjectionRegistry.unregister(PROJECTION_ID)
  currentSummary = null
  listener = null
}
