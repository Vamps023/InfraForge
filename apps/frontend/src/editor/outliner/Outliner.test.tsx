import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { render, screen, act, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { Outliner } from './Outliner'
import {
  outlinerProjectionRegistry,
  type OutlinerNode,
  type OutlinerProjection,
} from './outlinerProjection'
import { useSelectionStore } from '../selection/selectionStore'
import { useProjectStore } from '../../features/project/projectStore'

function node(id: string, parentId: string | null, hasChildren = false, depth = 0): OutlinerNode {
  return { id, parentId, label: id, type: 'test', depth, hasChildren }
}

function makeProjection(
  id: string,
  initialNodes: OutlinerNode[],
): OutlinerProjection & { emit: () => void; setNodes: (nodes: OutlinerNode[]) => void } {
  let listener: (() => void) | null = null
  let nodes = initialNodes
  return {
    id,
    label: id,
    getNodes: () => nodes,
    subscribe: (l) => {
      listener = l
      return () => {
        listener = null
      }
    },
    emit: () => listener?.(),
    setNodes: (next) => {
      nodes = next
    },
  } as OutlinerProjection & { emit: () => void; setNodes: (nodes: OutlinerNode[]) => void }
}

beforeEach(() => {
  for (const projection of outlinerProjectionRegistry.all()) {
    outlinerProjectionRegistry.unregister(projection.id)
  }
  useSelectionStore.getState().clear()
  useProjectStore.getState().clearProject()
})

afterEach(() => {
  for (const projection of outlinerProjectionRegistry.all()) {
    outlinerProjectionRegistry.unregister(projection.id)
  }
  useSelectionStore.getState().clear()
  useProjectStore.getState().clearProject()
})

describe('Outliner reactivity', () => {
  it('renders an empty state when no projections are registered', () => {
    render(<Outliner />)
    expect(screen.getByText('Open a project to inspect world entities.')).toBeInTheDocument()
  })

  it('renders rows when a projection is registered before mount', () => {
    outlinerProjectionRegistry.register(makeProjection('infra', [node('a', null), node('b', null)]))
    render(<Outliner />)
    expect(screen.getByText('a')).toBeInTheDocument()
    expect(screen.getByText('b')).toBeInTheDocument()
  })

  it('renders rows when a projection is registered after mount', () => {
    render(<Outliner />)
    expect(screen.queryByText('a')).not.toBeInTheDocument()
    act(() => {
      outlinerProjectionRegistry.register(makeProjection('infra', [node('a', null)]))
    })
    expect(screen.getByText('a')).toBeInTheDocument()
  })

  it('updates rows when a projection emits a change', () => {
    const projection = makeProjection('infra', [node('a', null)])
    outlinerProjectionRegistry.register(projection)
    render(<Outliner />)
    expect(screen.getByText('a')).toBeInTheDocument()
    // Mutate the node list and emit — the Outliner must re-read getNodes().
    act(() => {
      projection.setNodes([node('b', null)])
      projection.emit()
    })
    expect(screen.queryByText('a')).not.toBeInTheDocument()
    expect(screen.getByText('b')).toBeInTheDocument()
  })

  it('updates rows across repeated emissions (A -> B -> C)', () => {
    const projection = makeProjection('infra', [node('a', null)])
    outlinerProjectionRegistry.register(projection)
    render(<Outliner />)
    expect(screen.getByText('a')).toBeInTheDocument()
    act(() => {
      projection.setNodes([node('b', null)])
      projection.emit()
    })
    expect(screen.queryByText('a')).not.toBeInTheDocument()
    expect(screen.getByText('b')).toBeInTheDocument()
    act(() => {
      projection.setNodes([node('c', null)])
      projection.emit()
    })
    expect(screen.queryByText('b')).not.toBeInTheDocument()
    expect(screen.getByText('c')).toBeInTheDocument()
  })

  it('removes rows when a projection is unregistered', () => {
    outlinerProjectionRegistry.register(makeProjection('infra', [node('a', null)]))
    render(<Outliner />)
    expect(screen.getByText('a')).toBeInTheDocument()
    act(() => {
      outlinerProjectionRegistry.unregister('infra')
    })
    expect(screen.queryByText('a')).not.toBeInTheDocument()
  })

  it('composes multiple projections registered after mount', () => {
    render(<Outliner />)
    act(() => {
      outlinerProjectionRegistry.register(makeProjection('a', [node('a1', null)]))
      outlinerProjectionRegistry.register(makeProjection('b', [node('b1', null)]))
    })
    expect(screen.getByText('a1')).toBeInTheDocument()
    expect(screen.getByText('b1')).toBeInTheDocument()
  })

  it('selection uses canonical IDs', () => {
    outlinerProjectionRegistry.register(makeProjection('infra', [node('a', null), node('b', null)]))
    render(<Outliner />)
    screen.getByText('a').click()
    expect(useSelectionStore.getState().selectedIds).toEqual(['a'])
    expect(useSelectionStore.getState().primaryId).toBe('a')
  })

  it('background click clears selection', () => {
    outlinerProjectionRegistry.register(makeProjection('infra', [node('a', null)]))
    useSelectionStore.getState().select(['a'])
    render(<Outliner />)
    // Click the outliner body (not a row).
    const body = screen.getByRole('tree')
    body.click()
    expect(useSelectionStore.getState().selectedIds).toEqual([])
  })

  it('search finds descendants inside collapsed parents', () => {
    outlinerProjectionRegistry.register(
      makeProjection('infra', [
        node('parent', null, true),
        node('parent.child', 'parent'),
      ]),
    )
    render(<Outliner />)
    // Parent is collapsed; child is not visible.
    expect(screen.queryByText('parent.child')).not.toBeInTheDocument()
    // Search for the child.
    const input = screen.getByLabelText('Search outliner')
    act(() => {
      // React user-event would be better but this avoids import issues.
      Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')?.set?.call(input, 'child')
      input.dispatchEvent(new Event('input', { bubbles: true }))
    })
    // The child now appears in the search results (with ancestor path).
    expect(screen.getByText('parent.child')).toBeInTheDocument()
    expect(screen.getByText('parent')).toBeInTheDocument()
  })
})

describe('Outliner roving focus', () => {
  it('exactly one treeitem has tabindex=0 when selection is empty', () => {
    outlinerProjectionRegistry.register(
      makeProjection('infra', [node('a', null), node('b', null), node('c', null)]),
    )
    render(<Outliner />)
    const rows = screen.getAllByRole('treeitem')
    const tabIndexZero = rows.filter((r) => r.getAttribute('tabindex') === '0')
    const tabIndexMinusOne = rows.filter((r) => r.getAttribute('tabindex') === '-1')
    expect(tabIndexZero).toHaveLength(1)
    expect(tabIndexMinusOne).toHaveLength(2)
    // The first row should be the roving focus target when nothing is selected.
    expect(tabIndexZero[0]).toBe(rows[0])
  })

  it('keyboard-only Tab can enter the tree without a prior mouse click', async () => {
    outlinerProjectionRegistry.register(
      makeProjection('infra', [node('a', null), node('b', null)]),
    )
    render(<Outliner />)
    const rows = screen.getAllByRole('treeitem')
    // The first row has tabindex=0, so Tab from the search field should
    // land on it.
    const searchInput = screen.getByLabelText('Search outliner')
    searchInput.focus()
    await userEvent.tab()
    expect(rows[0]).toHaveFocus()
  })

  it('selected item becomes the roving focus target', () => {
    outlinerProjectionRegistry.register(
      makeProjection('infra', [node('a', null), node('b', null), node('c', null)]),
    )
    useSelectionStore.getState().select(['b'])
    render(<Outliner />)
    const rows = screen.getAllByRole('treeitem')
    const tabIndexZero = rows.filter((r) => r.getAttribute('tabindex') === '0')
    expect(tabIndexZero).toHaveLength(1)
    // The selected row 'b' (index 1) should be the roving focus target.
    expect(tabIndexZero[0]).toBe(rows[1])
  })

  it('ArrowDown moves focus to the next tree row', async () => {
    outlinerProjectionRegistry.register(
      makeProjection('infra', [node('a', null), node('b', null)]),
    )
    render(<Outliner />)
    const rows = screen.getAllByRole('treeitem')
    rows[0]!.focus()
    await userEvent.keyboard('{ArrowDown}')
    expect(rows[1]).toHaveFocus()
  })

  it('ArrowUp moves focus to the previous tree row', async () => {
    outlinerProjectionRegistry.register(
      makeProjection('infra', [node('a', null), node('b', null)]),
    )
    render(<Outliner />)
    const rows = screen.getAllByRole('treeitem')
    rows[1]!.focus()
    await userEvent.keyboard('{ArrowUp}')
    expect(rows[0]).toHaveFocus()
  })

  it('Enter selects the focused tree row', async () => {
    outlinerProjectionRegistry.register(makeProjection('infra', [node('a', null)]))
    render(<Outliner />)
    const row = screen.getAllByRole('treeitem')[0]!
    row.focus()
    await userEvent.keyboard('{Enter}')
    expect(useSelectionStore.getState().selectedIds).toEqual(['a'])
  })

  it('ArrowRight expands a collapsed parent', async () => {
    outlinerProjectionRegistry.register(
      makeProjection('infra', [
        node('parent', null, true),
        node('parent.child', 'parent'),
      ]),
    )
    render(<Outliner />)
    // Parent is collapsed; child is not visible.
    expect(screen.queryByText('parent.child')).not.toBeInTheDocument()
    const parentRow = screen.getByText('parent').closest('[role="treeitem"]') as HTMLElement
    parentRow.focus()
    await userEvent.keyboard('{ArrowRight}')
    expect(screen.getByText('parent.child')).toBeInTheDocument()
  })

  it('ArrowLeft collapses an expanded parent', async () => {
    outlinerProjectionRegistry.register(
      makeProjection('infra', [
        node('parent', null, true),
        node('parent.child', 'parent'),
      ]),
    )
    render(<Outliner />)
    const parentRow = screen.getByText('parent').closest('[role="treeitem"]') as HTMLElement
    // First expand.
    parentRow.focus()
    await userEvent.keyboard('{ArrowRight}')
    expect(screen.getByText('parent.child')).toBeInTheDocument()
    // Then collapse.
    await userEvent.keyboard('{ArrowLeft}')
    expect(screen.queryByText('parent.child')).not.toBeInTheDocument()
  })

  it('ArrowDown crosses a virtualization boundary and scrolls', async () => {
    // Create enough nodes to exceed the default jsdom viewport.
    // In jsdom, clientHeight is 0, so the Outliner mounts only
    // OVERSCAN (6) rows. This test verifies that ArrowDown can move
    // focus beyond the initial mounted slice by scrolling and
    // re-mounting the target row.
    const manyNodes: OutlinerNode[] = []
    for (let i = 0; i < 50; i++) {
      manyNodes.push(node(`item-${i}`, null))
    }
    outlinerProjectionRegistry.register(makeProjection('infra', manyNodes))
    render(<Outliner />)
    // Focus the first row.
    const firstRow = screen.getAllByRole('treeitem')[0]!
    firstRow.focus()
    expect(firstRow).toHaveFocus()
    // Press ArrowDown past the initial mounted slice (6 rows with
    // OVERSCAN=6 and viewportHeight=0). The Outliner should scroll
    // and focus each subsequent row.
    for (let i = 1; i < 15; i++) {
      await userEvent.keyboard('{ArrowDown}')
      await waitFor(() => {
        const focused = document.activeElement as HTMLElement | null
        expect(focused?.getAttribute('role')).toBe('treeitem')
        expect(focused?.dataset.nodeId).toBe(`item-${i}`)
      })
    }
  })

  it('ArrowUp crosses a virtualization boundary and scrolls', async () => {
    const manyNodes: OutlinerNode[] = []
    for (let i = 0; i < 50; i++) {
      manyNodes.push(node(`item-${i}`, null))
    }
    outlinerProjectionRegistry.register(makeProjection('infra', manyNodes))
    render(<Outliner />)
    const tree = screen.getByRole('tree')
    // Set a small viewport and scroll to the middle.
    act(() => {
      Object.defineProperty(tree, 'clientHeight', { value: 80, configurable: true })
      Object.defineProperty(tree, 'scrollTop', { value: 500, configurable: true })
      tree.dispatchEvent(new Event('scroll'))
    })
    // Find a row in the middle and focus it.
    const rows = screen.getAllByRole('treeitem')
    // Focus the last visible row in the current slice.
    const lastRow = rows[rows.length - 1]!
    lastRow.focus()
    const startId = lastRow.dataset.nodeId
    // Press ArrowUp to cross the boundary upward.
    for (let i = 0; i < 5; i++) {
      await userEvent.keyboard('{ArrowUp}')
      const focused = document.activeElement as HTMLElement | null
      expect(focused).not.toBeNull()
      expect(focused?.getAttribute('role')).toBe('treeitem')
    }
    // The focused row should have moved up from the starting row.
    const focused = document.activeElement as HTMLElement | null
    expect(focused?.dataset.nodeId).not.toBe(startId)
  })

  it('tree rows have aria-level for flat virtualized tree', async () => {
    outlinerProjectionRegistry.register(
      makeProjection('infra', [
        node('parent', null, true, 0),
        node('parent.child', 'parent', false, 1),
      ]),
    )
    render(<Outliner />)
    // Expand the parent so the child is rendered.
    const parentRow = screen.getByText('parent').closest('[role="treeitem"]') as HTMLElement
    parentRow.focus()
    await userEvent.keyboard('{ArrowRight}')
    const rows = screen.getAllByRole('treeitem')
    // Parent at depth 0 => aria-level=1
    expect(rows[0]).toHaveAttribute('aria-level', '1')
    // Child at depth 1 => aria-level=2
    expect(rows[1]).toHaveAttribute('aria-level', '2')
  })
})
