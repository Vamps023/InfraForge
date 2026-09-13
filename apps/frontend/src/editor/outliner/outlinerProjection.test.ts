import { beforeEach, describe, expect, it } from 'vitest'
import {
  outlinerProjectionRegistry,
  type OutlinerNode,
  type OutlinerProjection,
} from './outlinerProjection'

function makeProjection(id: string, nodes: OutlinerNode[]): OutlinerProjection {
  return {
    id,
    label: id,
    getNodes: () => nodes,
    subscribe: () => () => {},
  }
}

beforeEach(() => {
  for (const projection of outlinerProjectionRegistry.all()) {
    outlinerProjectionRegistry.unregister(projection.id)
  }
})

describe('outlinerProjectionRegistry', () => {
  it('registers and lists projections', () => {
    outlinerProjectionRegistry.register(makeProjection('terrain', []))
    expect(outlinerProjectionRegistry.all().map((p) => p.id)).toEqual(['terrain'])
  })

  it('rejects duplicate projection ids', () => {
    outlinerProjectionRegistry.register(makeProjection('roads', []))
    expect(() => outlinerProjectionRegistry.register(makeProjection('roads', []))).toThrowError(
      /Duplicate outliner projection id/,
    )
  })

  it('unregisters a projection', () => {
    outlinerProjectionRegistry.register(makeProjection('junctions', []))
    outlinerProjectionRegistry.unregister('junctions')
    expect(outlinerProjectionRegistry.all()).toEqual([])
  })
})

describe('outliner projection contract', () => {
  it('nodes carry stable canonical ids', () => {
    const nodes: OutlinerNode[] = [
      { id: 'terrain:1', parentId: null, label: 'Terrain 1', type: 'terrain', depth: 0, hasChildren: false },
    ]
    outlinerProjectionRegistry.register(makeProjection('terrain', nodes))
    expect(outlinerProjectionRegistry.all()[0]!.getNodes()[0]!.id).toBe('terrain:1')
  })

  it('nodes express parent/child relationships', () => {
    const nodes: OutlinerNode[] = [
      { id: 'road:1', parentId: null, label: 'Road 1', type: 'road', depth: 0, hasChildren: true },
      { id: 'road:1.lane:1', parentId: 'road:1', label: 'Lane 1', type: 'lane', depth: 1, hasChildren: false },
    ]
    outlinerProjectionRegistry.register(makeProjection('roads', nodes))
    const list = outlinerProjectionRegistry.all()[0]!.getNodes()
    expect(list[1]!.parentId).toBe('road:1')
  })

  it('subscribe returns an unsubscribe function the component uses', () => {
    let calls = 0
    const projection: OutlinerProjection = {
      id: 'infra',
      label: 'infra',
      getNodes: () => [],
      subscribe: (listener) => {
        calls += 1
        listener()
        return () => {}
      },
    }
    outlinerProjectionRegistry.register(projection)
    // The component subscribes after registration; subscribe is not invoked
    // by registration itself.
    const unsub = projection.subscribe(() => {})
    expect(calls).toBe(1)
    unsub()
  })
})
