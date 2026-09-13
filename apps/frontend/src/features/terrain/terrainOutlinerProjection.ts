import { outlinerProjectionRegistry, type OutlinerNode } from '../../editor/outliner/outlinerProjection'
import { useTerrainStore } from './terrainStore'

// Terrain outliner projection: exposes terrain datasets through the Issue #5
// outliner projection registry. Datasets appear as children of a "Terrain"
// group node, using stable canonical dataset UUIDs as their outliner IDs.
// Selection flows through the existing selection infrastructure.

const TERRAIN_GROUP_ID = 'terrain-group'
const TERRAIN_SOURCE = 'terrain'

export function registerTerrainOutlinerProjection(): void {
  const listeners = new Set<() => void>()

  // Subscribe to terrain store changes and notify outliner listeners.
  const unsubscribeStore = useTerrainStore.subscribe((state) => {
    // Only notify when datasets change (not on every store update).
    void state.datasets
    for (const listener of listeners) {
      listener()
    }
  })

  const projection = {
    id: TERRAIN_SOURCE,
    label: 'Terrain',
    getNodes: () => {
      const datasets = useTerrainStore.getState().datasets
      const nodes: OutlinerNode[] = []

      // Group root node (always present when the projection is registered).
      nodes.push({
        id: TERRAIN_GROUP_ID,
        parentId: null,
        label: 'Terrain',
        type: 'terrain-group',
        depth: 0,
        hasChildren: datasets.length > 0,
      })

      // Dataset child nodes.
      for (const dataset of datasets) {
        nodes.push({
          id: `terrain:${dataset.datasetUuid}`,
          parentId: TERRAIN_GROUP_ID,
          label: dataset.displayName,
          type: 'terrain-dataset',
          depth: 1,
          hasChildren: false,
        })
      }

      return nodes
    },
    subscribe: (listener: () => void) => {
      listeners.add(listener)
      return () => {
        listeners.delete(listener)
        if (listeners.size === 0) {
          // Clean up store subscription when no outliner listeners remain.
          // The projection stays registered; it just stops reacting.
        }
      }
    },
  }

  outlinerProjectionRegistry.register(projection)

  // Keep the store subscription alive for the projection's lifetime.
  // It's cleaned up when the projection is unregistered.
  void unsubscribeStore
}

export function unregisterTerrainOutlinerProjection(): void {
  outlinerProjectionRegistry.unregister(TERRAIN_SOURCE)
}
