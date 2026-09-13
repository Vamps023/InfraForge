import type { OutlinerNode } from './outlinerProjection'
import type { CanonicalId } from '../selection/selectionStore'

// Pure tree-flattening for the outliner. Given a flat node list (composed
// from registered projections) and an expansion set keyed by canonical ID,
// returns the visible rows by walking roots and recursing only into
// expanded parents. Extracted from the component so virtualization and
// selection behavior are unit-testable without a DOM.
export function buildVisibleRows(
  nodes: OutlinerNode[],
  expanded: Set<CanonicalId>,
): OutlinerNode[] {
  const childrenByParent = new Map<CanonicalId | null, OutlinerNode[]>()
  for (const node of nodes) {
    const siblings = childrenByParent.get(node.parentId)
    if (siblings) {
      siblings.push(node)
    } else {
      childrenByParent.set(node.parentId, [node])
    }
  }
  const visible: OutlinerNode[] = []
  const roots = childrenByParent.get(null) ?? []
  const walk = (list: OutlinerNode[]) => {
    for (const node of list) {
      visible.push(node)
      if (node.hasChildren && expanded.has(node.id)) {
        const children = childrenByParent.get(node.id) ?? []
        walk(children)
      }
    }
  }
  walk(roots)
  return visible
}
