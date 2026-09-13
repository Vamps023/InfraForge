import { describe, expect, it } from 'vitest'
import { buildVisibleRows } from './outlinerTree'
import type { OutlinerNode } from './outlinerProjection'

function node(id: string, parentId: string | null, hasChildren = false, depth = 0): OutlinerNode {
  return { id, parentId, label: id, type: 'test', depth, hasChildren }
}

describe('buildVisibleRows', () => {
  it('renders only roots when nothing is expanded', () => {
    const nodes = [
      node('a', null, true),
      node('a.1', 'a'),
      node('b', null),
    ]
    const visible = buildVisibleRows(nodes, new Set())
    expect(visible.map((n) => n.id)).toEqual(['a', 'b'])
  })

  it('expands children when the parent is expanded', () => {
    const nodes = [
      node('a', null, true),
      node('a.1', 'a'),
      node('a.2', 'a'),
      node('b', null),
    ]
    const visible = buildVisibleRows(nodes, new Set(['a']))
    expect(visible.map((n) => n.id)).toEqual(['a', 'a.1', 'a.2', 'b'])
  })

  it('collapses children when the parent is not expanded', () => {
    const nodes = [
      node('a', null, true),
      node('a.1', 'a', true),
      node('a.1.x', 'a.1'),
    ]
    const visible = buildVisibleRows(nodes, new Set(['a']))
    expect(visible.map((n) => n.id)).toEqual(['a', 'a.1'])
  })

  it('handles deeply nested expansion', () => {
    const nodes = [
      node('root', null, true),
      node('root.child', 'root', true),
      node('root.child.leaf', 'root.child'),
    ]
    const visible = buildVisibleRows(nodes, new Set(['root', 'root.child']))
    expect(visible.map((n) => n.id)).toEqual(['root', 'root.child', 'root.child.leaf'])
  })

  it('returns an empty list for no nodes', () => {
    expect(buildVisibleRows([], new Set())).toEqual([])
  })

  it('renders a large projection without expanding beyond visible (virtualization input)', () => {
    // Virtualization slices the visible list; buildVisibleRows must return
    // the full visible set so the component can window it. Here we verify
    // that a collapsed tree of 1000 roots produces exactly 1000 rows, not
    // their (non-existent) children.
    const nodes: OutlinerNode[] = []
    for (let i = 0; i < 1000; i += 1) {
      nodes.push(node(`n${i}`, null))
    }
    const visible = buildVisibleRows(nodes, new Set())
    expect(visible).toHaveLength(1000)
  })

  it('selection resolves canonical ids, not array index', () => {
    // The visible rows carry canonical ids; selection stores canonical ids.
    // This verifies the contract: a selected id is found by id, not by the
    // row's position in the visible list.
    const nodes = [node('a', null, true), node('a.1', 'a'), node('b', null)]
    const visible = buildVisibleRows(nodes, new Set(['a']))
    const selectedId = 'a.1'
    const selectedRow = visible.find((n) => n.id === selectedId)
    expect(selectedRow).toBeDefined()
    expect(selectedRow!.id).toBe('a.1')
  })
})
