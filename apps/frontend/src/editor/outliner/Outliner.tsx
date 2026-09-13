import { useEffect, useMemo, useRef, useState } from 'react'
import { ChevronDown, ChevronRight, Search } from 'lucide-react'
import { outlinerProjectionRegistry, type OutlinerNode } from './outlinerProjection'
import { buildVisibleRows } from './outlinerTree'
import { useSelectionStore, type CanonicalId } from '../selection/selectionStore'

// Virtualized outliner. Consumes registered projections (backend/domain
// projections, never canonical objects — ADR-0009), composes them into one
// tree, tracks expansion state by canonical ID, and virtualizes the visible
// row list so large projects never render thousands of DOM rows.
//
// Selection resolves canonical IDs through the shared selection store; the
// outliner never substitutes its own object identity.

const ROW_HEIGHT = 26
const OVERSCAN = 6

interface OutlinerViewState {
  expanded: Set<CanonicalId>
  toggle: (id: CanonicalId) => void
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
  return { expanded, toggle }
}

// Composes all registered projections into a flat node list, then computes
// the visible rows by walking roots and expanding only expanded parents.
function useComposedNodes(): OutlinerNode[] {
  const [, force] = useState(0)
  useEffect(() => {
    const unsubs = outlinerProjectionRegistry.all().map((projection) =>
      projection.subscribe(() => force((n) => n + 1)),
    )
    return () => {
      for (const unsub of unsubs) {
        unsub()
      }
    }
  }, [])
  return useMemo(() => {
    const all: OutlinerNode[] = []
    for (const projection of outlinerProjectionRegistry.all()) {
      all.push(...projection.getNodes())
    }
    return all
  }, [])
}

export function Outliner() {
  const { expanded, toggle } = useOutlinerView()
  const nodes = useComposedNodes()
  const selectedIds = useSelectionStore((state) => state.selectedIds)
  const select = useSelectionStore((state) => state.select)
  const clear = useSelectionStore((state) => state.clear)
  const [query, setQuery] = useState('')
  const [scrollTop, setScrollTop] = useState(0)
  const [viewportHeight, setViewportHeight] = useState(0)
  const scrollRef = useRef<HTMLDivElement | null>(null)

  const visibleRows = useMemo(() => buildVisibleRows(nodes, expanded), [nodes, expanded])
  const filteredRows = useMemo(() => {
    if (query.trim() === '') {
      return visibleRows
    }
    const lower = query.toLowerCase()
    return visibleRows.filter((row) => row.label.toLowerCase().includes(lower))
  }, [visibleRows, query])

  const totalHeight = filteredRows.length * ROW_HEIGHT
  const startIndex = Math.max(0, Math.floor(scrollTop / ROW_HEIGHT) - OVERSCAN)
  const endIndex = Math.min(
    filteredRows.length,
    Math.ceil((scrollTop + viewportHeight) / ROW_HEIGHT) + OVERSCAN,
  )
  const visibleSlice = filteredRows.slice(startIndex, endIndex)

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
    if (event.shiftKey || event.ctrlKey || event.metaKey) {
      select([node.id], 'toggle')
    } else {
      select([node.id], 'replace')
    }
  }

  const onBackgroundClick = () => {
    clear()
  }

  const hasProjections = outlinerProjectionRegistry.all().length > 0

  return (
    <aside className="panel outliner-panel" aria-label="Outliner">
      <div className="panel-title-row">
        <span>Outliner</span>
      </div>
      <div className="search-box">
        <Search size={14} />
        <input
          aria-label="Search outliner"
          placeholder="Search"
          value={query}
          onChange={(event) => setQuery(event.target.value)}
          disabled={!hasProjections}
        />
      </div>
      <div
        className="outliner-body"
        ref={scrollRef}
        onScroll={onScroll}
        onClick={onBackgroundClick}
        role="tree"
        aria-label="World entities"
      >
        {filteredRows.length === 0 ? (
          <div className="panel-empty">
            {hasProjections
              ? 'No entities match the current filter.'
              : 'Open a project to inspect world entities.'}
          </div>
        ) : (
          <div style={{ position: 'relative', height: totalHeight }}>
            {visibleSlice.map((node) => {
              const index = filteredRows.indexOf(node)
              const top = index * ROW_HEIGHT
              const selected = selectedIds.includes(node.id)
              return (
                <div
                  key={node.id}
                  role="treeitem"
                  aria-selected={selected}
                  className={`outliner-row${selected ? ' selected' : ''}`}
                  style={{ position: 'absolute', top, height: ROW_HEIGHT, paddingLeft: 8 + node.depth * 14 }}
                  onClick={(event) => onRowClick(event, node)}
                >
                  {node.hasChildren ? (
                    <button
                      className="outliner-disclosure"
                      type="button"
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
