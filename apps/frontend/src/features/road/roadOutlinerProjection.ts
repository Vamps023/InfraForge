import { outlinerProjectionRegistry, type OutlinerNode } from '../../editor/outliner/outlinerProjection'
import { useRoadStore } from './roadStore'

// Road outliner projection: exposes roads through the Issue #5 outliner
// projection registry. Roads appear as children of a "Roads" group node,
// using stable canonical road UUIDs as their outliner IDs. Selection
// flows through the existing selection infrastructure.

const ROAD_GROUP_ID = 'road-group'
const ROAD_SOURCE = 'road'

export function registerRoadOutlinerProjection(): void {
  const listeners = new Set<() => void>()

  const unsubscribeStore = useRoadStore.subscribe((state) => {
    void state.roads
    for (const listener of listeners) {
      listener()
    }
  })

  const projection = {
    id: ROAD_SOURCE,
    label: 'Roads',
    getNodes: () => {
      const roads = useRoadStore.getState().roads
      const nodes: OutlinerNode[] = []

      nodes.push({
        id: ROAD_GROUP_ID,
        parentId: null,
        label: 'Roads',
        type: 'road-group',
        depth: 0,
        hasChildren: roads.length > 0,
      })

      for (const road of roads) {
        nodes.push({
          id: `road:${road.roadId}`,
          parentId: ROAD_GROUP_ID,
          label: road.name,
          type: 'road',
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
      }
    },
  }

  outlinerProjectionRegistry.register(projection)
  void unsubscribeStore
}

export function unregisterRoadOutlinerProjection(): void {
  outlinerProjectionRegistry.unregister(ROAD_SOURCE)
}
