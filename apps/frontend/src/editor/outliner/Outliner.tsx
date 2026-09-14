import { useCallback, useEffect, useMemo, useRef, useState, useSyncExternalStore } from 'react'
import { ChevronDown, ChevronRight, Search } from 'lucide-react'
import { outlinerProjectionRegistry, type OutlinerNode, type OutlinerProjection } from './outlinerProjection'
import { buildVisibleRows } from './outlinerTree'
import { useSelectionStore, type CanonicalId } from '../selection/selectionStore'
import { useProjectStore } from '../../features/project/projectStore'

// Virtualized outliner. Consumes registered projections (backend/domain
// projections, never canonical objects — ADR-0009), composes them into one
// tree, tracks expansion state by canonical ID, and virtualizes the visible
// row list so large projects never render thousands of DOM rows.
//
// Roving-focus model:
//   - Keyboard focus is tracked via `focusedNodeId`, separate from selection.
//   - Exactly one rendered treeitem has tabIndex=0 (the roving focus target);
//     all others have tabIndex=-1. This lets a keyboard user Tab into the
//     tree at the focused row, then use Arrow keys to move within the tree.
//   - When `focusedNodeId` is null, it is derived from selection (if the
//     selected node is visible) or defaults to the first visible row.
//   - When an Arrow key targets a row outside the mounted virtualization
//     slice, the scroll container is scrolled so the target row becomes
//     mounted, then focus is moved to it after the render commits.
//
// Reactivity model:
//   - Registry membership is observed via useSyncExternalStore, so
//     registering/unregistering a projection after mount updates the tree.
//   - Each projection's `subscribe` is wired in an effect keyed on the
//     current projection list, so projection-emitted updates recompute node
//     data and subscriptions are cleaned up correctly (no stale
//     subscriptions, no leaks, no duplicate subscriptions).
// Selection resolves canonical IDs through the shared selection store; the
// outliner never substitutes its own object identity.

const ROW_HEIGHT = 26
const OVERSCAN = 6

interface OutlinerViewState {
  expanded: Set<CanonicalId>
  toggle: (id: CanonicalId) => void
  setExpanded: (next: Set<CanonicalId>) => void
}

function useOutlinerView(): OutlinerViewState {
  const [expanded, setExpanded] = useState<Set<CanonicalId>>(new Set())
  const toggle = (id: CanonicalId) => {
    setExpanded((prev) => {
      const next = new Set(prev)
      if (next.has(id)) {
        next.delete(id)
      } else {
        next.add(id)
      }
      return next
    })
  }
  return { expanded, toggle, setExpanded }
}

// Composes all registered projections into a flat node list. Reactively
// subscribes to the registry (membership changes) and to each projection
// (data changes). The node list recomputes whenever either changes.
function useComposedNodes(): OutlinerNode[] {
  const projections = useSyncExternalStore(
    outlinerProjectionRegistry.subscribe,
    outlinerProjectionRegistry.getSnapshot,
  )
  // A version counter bumped by any projection's change notification. This
  // is included in the useMemo dependency graph below so a projection data
  // change actually re-reads getNodes() rather than reusing a stale array.
  const [projectionVersion, setProjectionVersion] = useState(0)
  useEffect(() => {
    const unsubs = projections.map((projection) =>
      projection.subscribe(() => setProjectionVersion((n) => n + 1)),
    )
    return () => {
      for (const unsub of unsubs) {
        unsub()
      }
    }
  }, [projections])
  return useMemo(() => {
    const all: OutlinerNode[] = []
    for (const projection of projections) {
      all.push(...projection.getNodes())
    }
    return all
  }, [projections, projectionVersion])
}

// Builds a search result that includes matching nodes plus their ancestor
// paths so hierarchy remains understandable. Does not mutate permanent
// expansion state; the search view is computed from the full node list.
function buildSearchRows(
  nodes: OutlinerNode[],
  query: string,
): OutlinerNode[] {
  const byId = new Map<CanonicalId, OutlinerNode>()
  for (const node of nodes) {
    byId.set(node.id, node)
  }
  const lower = query.toLowerCase()
  const matched = new Set<CanonicalId>()
  for (const node of nodes) {
    if (node.label.toLowerCase().includes(lower)) {
      matched.add(node.id)
    }
  }
  // Include ancestors so the hierarchy path to each match is visible.
  for (const node of nodes) {
    if (!matched.has(node.id)) {
      continue
    }
    let parentId = node.parentId
    while (parentId !== null) {
      if (matched.has(parentId)) {
        break
      }
      matched.add(parentId)
      const parent = byId.get(parentId)
      if (!parent) {
        break
      }
      parentId = parent.parentId
    }
  }
  // Preserve original projection order; only filter by membership.
  return nodes.filter((node) => matched.has(node.id))
}

export function Outliner() {
  const { expanded, toggle } = useOutlinerView()
  const nodes = useComposedNodes()
  const selectedIds = useSelectionStore((state) => state.selectedIds)
  const primaryId = useSelectionStore((state) => state.primaryId)
  const select = useSelectionStore((state) => state.select)
  const clear = useSelectionStore((state) => state.clear)
  const projectSummary = useProjectStore((state) => state.summary)
  const projectOpen = projectSummary !== null
  const [query, setQuery] = useState('')
  const [scrollTop, setScrollTop] = useState(0)
  const [viewportHeight, setViewportHeight] = useState(0)
  const scrollRef = useRef<HTMLDivElement | null>(null)

  // Roving focus: the node ID that currently holds tabIndex=0. This is
  // separate from selection. When null, it is derived from selection or
  // defaults to the first visible row.
  const [focusedNodeId, setFocusedNodeId] = useState<CanonicalId | null>(null)
  // When a scroll-to-row is pending (target outside the mounted slice),
  // we store the target index and focus it after the render commits.
  const [pendingFocusIndex, setPendingFocusIndex] = useState<number | null>(null)

  const isSearching = query.trim() !== ''
  const visibleRows = useMemo(() => {
    if (isSearching) {
      // Search across all projected nodes regardless of expansion state.
      return buildSearchRows(nodes, query.trim())
    }
    return buildVisibleRows(nodes, expanded)
  }, [nodes, expanded, isSearching, query])

  const totalHeight = visibleRows.length * ROW_HEIGHT
  const startIndex = Math.max(0, Math.floor(scrollTop / ROW_HEIGHT) - OVERSCAN)
  const endIndex = Math.min(
    visibleRows.length,
    Math.ceil((scrollTop + viewportHeight) / ROW_HEIGHT) + OVERSCAN,
  )
  const visibleSlice = visibleRows.slice(startIndex, endIndex)

  useEffect(() => {
    const element = scrollRef.current
    if (!element) {
      return
    }
    const observer = new ResizeObserver(() => setViewportHeight(element.clientHeight))
    observer.observe(element)
    setViewportHeight(element.clientHeight)
    return () => observer.disconnect()
  }, [])

  const onScroll = (event: React.UIEvent<HTMLDivElement>) => {
    setScrollTop(event.currentTarget.scrollTop)
  }

  const onRowClick = (event: React.MouseEvent, node: OutlinerNode) => {
    event.stopPropagation()
    setFocusedNodeId(node.id)
    if (event.shiftKey || event.ctrlKey || event.metaKey) {
      select([node.id], 'toggle')
    } else {
      select([node.id], 'replace')
    }
  }

  const onBackgroundClick = () => {
    clear()
  }

  const rowRefs = useRef<Record<string, HTMLDivElement | null>>({})

  // Resolve the effective focus target: if focusedNodeId is set and visible,
  // use it. Otherwise derive from the primary selected ID if visible, else
  // default to the first visible row.
  const effectiveFocusedId = useMemo<CanonicalId | null>(() => {
    if (focusedNodeId !== null && visibleRows.some((n) => n.id === focusedNodeId)) {
      return focusedNodeId
    }
    if (primaryId !== null && visibleRows.some((n) => n.id === primaryId)) {
      return primaryId
    }
    return visibleRows.length > 0 ? visibleRows[0]!.id : null
  }, [focusedNodeId, primaryId, visibleRows])

  const effectiveFocusIndex = useMemo<number>(() => {
    if (effectiveFocusedId === null) return -1
    return visibleRows.findIndex((n) => n.id === effectiveFocusedId)
  }, [effectiveFocusedId, visibleRows])

  // After a scroll-to-row renders the target slice, focus the target row.
  useEffect(() => {
    if (pendingFocusIndex === null) return
    // Check if the pending row is now mounted.
    if (pendingFocusIndex >= startIndex && pendingFocusIndex < endIndex) {
      const node = visibleRows[pendingFocusIndex]
      if (node) {
        const el = rowRefs.current[node.id]
        if (el) {
          el.focus()
          setFocusedNodeId(node.id)
        }
      }
      setPendingFocusIndex(null)
    }
  }, [pendingFocusIndex, startIndex, endIndex, visibleRows])

  // Scroll the container so that the row at `index` becomes visible and
  // mounted. Called when an Arrow key targets a row outside the slice.
  const scrollToRow = useCallback((index: number) => {
    const element = scrollRef.current
    if (!element) return
    const rowTop = index * ROW_HEIGHT
    const rowBottom = rowTop + ROW_HEIGHT
    const viewTop = element.scrollTop
    const viewBottom = element.scrollTop + element.clientHeight
    if (rowTop < viewTop) {
      element.scrollTop = rowTop
    } else if (rowBottom > viewBottom) {
      element.scrollTop = rowBottom - element.clientHeight
    }
    // Update scrollTop state so the virtualization slice recomputes.
    setScrollTop(element.scrollTop)
  }, [])

  const onTreeKeyDown = (event: React.KeyboardEvent) => {
    if (visibleRows.length === 0) return
    const currentIndex = effectiveFocusIndex
    if (event.key === 'ArrowDown') {
      event.preventDefault()
      const nextIndex = Math.min(currentIndex + 1, visibleRows.length - 1)
      if (nextIndex === currentIndex) return
      const nextNode = visibleRows[nextIndex]!
      setFocusedNodeId(nextNode.id)
      // If the target is outside the mounted slice, scroll to it and
      // focus after render; otherwise focus immediately.
      if (nextIndex < startIndex || nextIndex >= endIndex) {
        scrollToRow(nextIndex)
        setPendingFocusIndex(nextIndex)
      } else {
        rowRefs.current[nextNode.id]?.focus()
      }
      return
    }
    if (event.key === 'ArrowUp') {
      event.preventDefault()
      const nextIndex = Math.max(currentIndex - 1, 0)
      if (nextIndex === currentIndex) return
      const nextNode = visibleRows[nextIndex]!
      setFocusedNodeId(nextNode.id)
      if (nextIndex < startIndex || nextIndex >= endIndex) {
        scrollToRow(nextIndex)
        setPendingFocusIndex(nextIndex)
      } else {
        rowRefs.current[nextNode.id]?.focus()
      }
      return
    }
    if (event.key === 'Enter' && currentIndex >= 0) {
      event.preventDefault()
      select([visibleRows[currentIndex]!.id], 'replace')
      return
    }
    if (event.key === 'ArrowRight' && currentIndex >= 0) {
      const node = visibleRows[currentIndex]!
      if (node.hasChildren && !expanded.has(node.id)) {
        event.preventDefault()
        toggle(node.id)
      }
      return
    }
    if (event.key === 'ArrowLeft' && currentIndex >= 0) {
      const node = visibleRows[currentIndex]!
      if (node.hasChildren && expanded.has(node.id)) {
        event.preventDefault()
        toggle(node.id)
      }
      return
    }
  }

  const hasProjections = nodes.length > 0
  const searchEnabled = projectOpen

  return (
    <aside className="panel outliner-panel" aria-label="Outliner">
      <div className="panel-title-row">
        <span>Scene</span>
      </div>
      {projectOpen ? (
        <div className="panel-tabs" role="tablist" aria-label="Outliner views">
          <button
            className="panel-tab active"
            type="button"
            role="tab"
            aria-selected="true"
            aria-controls="outliner-scene-panel"
          >
            Scene
          </button>
          <button
            className="panel-tab"
            type="button"
            role="tab"
            aria-selected="false"
            disabled
            title="Layers — coming later"
          >
            Layers
          </button>
          <button
            className="panel-tab"
            type="button"
            role="tab"
            aria-selected="false"
            disabled
            title="Assets — coming later"
          >
            Assets
          </button>
        </div>
      ) : null}
      <div className="search-box">
        <Search size={14} />
        <input
          aria-label="Search outliner"
          placeholder="Search"
          value={query}
          onChange={(event) => setQuery(event.target.value)}
          disabled={!searchEnabled}
        />
      </div>
      <div
        className="outliner-body"
        id="outliner-scene-panel"
        ref={scrollRef}
        onScroll={onScroll}
        onClick={onBackgroundClick}
        onKeyDown={onTreeKeyDown}
        role="tree"
        aria-label="World entities"
      >
        {visibleRows.length === 0 ? (
          <div className="panel-empty">
            {!projectOpen
              ? 'Open a project to inspect world entities.'
              : isSearching
                ? 'No entities match the current filter.'
                : 'No entities in this project.'}
          </div>
        ) : (
          <div style={{ position: 'relative', height: totalHeight }}>
            {visibleSlice.map((node, localIndex) => {
              // Absolute index in the visible (virtualized) list — no O(n)
              // indexOf lookup per row.
              const absoluteIndex = startIndex + localIndex
              const top = absoluteIndex * ROW_HEIGHT
              const selected = selectedIds.includes(node.id)
              const isFocused = node.id === effectiveFocusedId
              return (
                <div
                  key={node.id}
                  ref={(el) => { rowRefs.current[node.id] = el }}
                  role="treeitem"
                  data-node-id={node.id}
                  aria-selected={selected}
                  aria-expanded={node.hasChildren ? expanded.has(node.id) : undefined}
                  aria-level={node.depth + 1}
                  tabIndex={isFocused ? 0 : -1}
                  className={`outliner-row${selected ? ' selected' : ''}`}
                  style={{ position: 'absolute', top, height: ROW_HEIGHT, paddingLeft: 8 + node.depth * 14 }}
                  onClick={(event) => onRowClick(event, node)}
                  onFocus={() => setFocusedNodeId(node.id)}
                >
                  {node.hasChildren ? (
                    <button
                      className="outliner-disclosure"
                      type="button"
                      tabIndex={-1}
                      aria-label={expanded.has(node.id) ? 'Collapse' : 'Expand'}
                      onClick={(event) => {
                        event.stopPropagation()
                        toggle(node.id)
                      }}
                    >
                      {expanded.has(node.id) ? <ChevronDown size={12} /> : <ChevronRight size={12} />}
                    </button>
                  ) : (
                    <span className="outliner-disclosure-spacer" />
                  )}
                  <span className="outliner-label">{node.label}</span>
                </div>
              )
            })}
          </div>
        )}
      </div>
    </aside>
  )
}
