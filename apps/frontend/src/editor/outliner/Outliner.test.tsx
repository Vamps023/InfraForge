import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { render, screen, act } from '@testing-library/react'
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
    setNodes: (next: OutlinerNode[]) => {
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
