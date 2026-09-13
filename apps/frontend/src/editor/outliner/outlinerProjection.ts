import type { CanonicalId } from '../selection/selectionStore'

// Outliner projection contract. The outliner consumes backend/domain
// projections rather than owning canonical objects (ADR-0009). Future
// domains (terrain, roads, junctions, infrastructure, simulation) supply
// projections through this API; the outliner itself never hard-codes a
// domain.
//
// A projection is a flat list of outliner nodes plus an expansion-state
// owner. Stable canonical IDs are the identity contract: selection and
// expansion resolve canonical IDs, never frontend object identity.

export type OutlinerNodeType = string

export interface OutlinerNode {
  // Stable canonical ID from the backend. Selection and inspector resolve
  // against this ID.
  id: CanonicalId
  // Parent canonical ID, or null for a root. The outliner uses this to
  // compute the visible (expanded) row list.
  parentId: CanonicalId | null
  // Human-readable label from the projection.
  label: string
  // Domain-supplied type tag, e.g. 'terrain', 'road', 'junction'. Used only
  // for icon/styling; the outliner does not interpret domain semantics.
  type: OutlinerNodeType
  // Depth in the tree (roots = 0). Precomputed by the projection.
  depth: number
  // Whether this node has children. Lets the outliner show a disclosure
  // affordance without fetching children eagerly.
  hasChildren: boolean
}

export interface OutlinerProjection {
  // Unique projection id (e.g. 'terrain', 'roads'). Used for registration.
  id: string
  // Display name for the projection's group header in the outliner.
  label: string
  // Returns the current flat node list. The outliner calls this on every
  // render it subscribes to; projections should be cheap or memoized.
  getNodes: () => OutlinerNode[]
  // Subscribe to projection changes. Returns an unsubscribe function.
  subscribe: (listener: () => void) => () => void
}

// Registry of outliner projections. Domains register a projection; the
// outliner composes all registered projections into one tree.
interface OutlinerProjectionRegistry {
  projections: Map<string, OutlinerProjection>
  register: (projection: OutlinerProjection) => void
  unregister: (id: string) => void
  all: () => OutlinerProjection[]
}

const outlinerRegistry: OutlinerProjectionRegistry = {
  projections: new Map<string, OutlinerProjection>(),
  register(projection) {
    if (outlinerRegistry.projections.has(projection.id)) {
      throw new Error(`Duplicate outliner projection id: ${projection.id}`)
    }
    outlinerRegistry.projections.set(projection.id, projection)
  },
  unregister(id) {
    outlinerRegistry.projections.delete(id)
  },
  all() {
    return Array.from(outlinerRegistry.projections.values())
  },
}

export const outlinerProjectionRegistry = outlinerRegistry
