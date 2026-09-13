import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { render, screen, act } from '@testing-library/react'
import { Inspector } from './Inspector'
import { inspectorSectionRegistry, type InspectorSection } from './inspectorRegistry'
import { useSelectionStore } from '../selection/selectionStore'

function makeSection(id: string, overrides: Partial<InspectorSection> = {}): InspectorSection {
  return {
    id,
    label: id,
    applies: () => true,
    render: () => <span>{id}-body</span>,
    ...overrides,
  }
}

beforeEach(() => {
  for (const section of inspectorSectionRegistry.all()) {
    inspectorSectionRegistry.unregister(section.id)
  }
  useSelectionStore.getState().clear()
})

afterEach(() => {
  for (const section of inspectorSectionRegistry.all()) {
    inspectorSectionRegistry.unregister(section.id)
  }
  useSelectionStore.getState().clear()
})

describe('Inspector reactivity', () => {
  it('renders an empty state when no sections are registered', () => {
    render(<Inspector />)
    expect(screen.getByText('Select an authored entity to inspect its properties.')).toBeInTheDocument()
  })

  it('renders a section registered before mount', () => {
    inspectorSectionRegistry.register(makeSection('overview'))
    render(<Inspector />)
    expect(screen.getByText('overview')).toBeInTheDocument()
    expect(screen.getByText('overview-body')).toBeInTheDocument()
  })

  it('renders a section registered after mount', () => {
    render(<Inspector />)
    expect(screen.queryByText('late')).not.toBeInTheDocument()
    act(() => {
      inspectorSectionRegistry.register(makeSection('late'))
    })
    expect(screen.getByText('late')).toBeInTheDocument()
    expect(screen.getByText('late-body')).toBeInTheDocument()
  })

  it('removes a section when unregistered after mount', () => {
    inspectorSectionRegistry.register(makeSection('gone'))
    render(<Inspector />)
    expect(screen.getByText('gone')).toBeInTheDocument()
    act(() => {
      inspectorSectionRegistry.unregister('gone')
    })
    expect(screen.queryByText('gone')).not.toBeInTheDocument()
  })

  it('updates applicable sections when selection changes', () => {
    inspectorSectionRegistry.register(
      makeSection('empty-only', { applies: (ctx) => ctx.selectedIds.length === 0 }),
    )
    inspectorSectionRegistry.register(
      makeSection('has-selection', { applies: (ctx) => ctx.selectedIds.length > 0 }),
    )
    render(<Inspector />)
    expect(screen.getByText('empty-only')).toBeInTheDocument()
    expect(screen.queryByText('has-selection')).not.toBeInTheDocument()
    act(() => {
      useSelectionStore.getState().select(['entity:1'])
    })
    expect(screen.queryByText('empty-only')).not.toBeInTheDocument()
    expect(screen.getByText('has-selection')).toBeInTheDocument()
  })

  it('renders the unsupported selection empty state', () => {
    inspectorSectionRegistry.register(
      makeSection('only-roads', {
        applies: (ctx) => ctx.selectedIds.length === 1 && ctx.selectedIds[0] === 'road:1',
      }),
    )
    render(<Inspector />)
    act(() => {
      useSelectionStore.getState().select(['terrain:1'])
    })
    expect(
      screen.getByText('No inspector section is available for the current selection.'),
    ).toBeInTheDocument()
  })
})
