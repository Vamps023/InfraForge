import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { render, screen, fireEvent } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { BottomPanel } from './BottomPanel'
import { useUiStore } from '../../state/uiStore'
import { useProblemsStore } from '../problems/problemsStore'
import { useOperationsStore } from '../operations/operationsStore'
import { useSelectionStore } from '../selection/selectionStore'

beforeEach(() => {
  useUiStore.getState().setActiveBottomTab('Problems')
  useProblemsStore.getState().clear()
  useOperationsStore.getState().clear()
  useSelectionStore.getState().clear()
})

afterEach(() => {
  useProblemsStore.getState().clear()
  useOperationsStore.getState().clear()
  useSelectionStore.getState().clear()
})

describe('BottomPanel accessibility', () => {
  it('renders tabs with tablist/tab semantics', () => {
    render(<BottomPanel />)
    const tablist = screen.getByRole('tablist')
    expect(tablist).toBeInTheDocument()
    const tabs = screen.getAllByRole('tab')
    expect(tabs).toHaveLength(2)
    expect(tabs[0]).toHaveAttribute('aria-selected', 'true')
    expect(tabs[1]).toHaveAttribute('aria-selected', 'false')
  })

  it('ArrowRight moves from Problems to Operations tab', async () => {
    render(<BottomPanel />)
    const tabs = screen.getAllByRole('tab')
    await userEvent.click(tabs[0]!)
    await userEvent.keyboard('{ArrowRight}')
    expect(useUiStore.getState().activeBottomTab).toBe('Operations')
  })

  it('ArrowLeft moves from Operations to Problems tab', async () => {
    useUiStore.getState().setActiveBottomTab('Operations')
    render(<BottomPanel />)
    const tabs = screen.getAllByRole('tab')
    await userEvent.click(tabs[1]!)
    await userEvent.keyboard('{ArrowLeft}')
    expect(useUiStore.getState().activeBottomTab).toBe('Problems')
  })

  it('problem rows are focusable and keyboard-activatable', async () => {
    useProblemsStore.getState().upsert({
      id: 'diag-1',
      severity: 'error',
      message: 'Test error',
      source: 'test',
      targetId: 'entity-1',
    })
    render(<BottomPanel />)
    const row = screen.getByText('Test error').closest('li')!
    expect(row).toHaveAttribute('tabindex', '0')
    expect(row).toHaveAttribute('role', 'button')
    await userEvent.click(row)
    expect(useSelectionStore.getState().selectedIds).toContain('entity-1')
  })

  it('problem rows can be activated with Enter key', async () => {
    useProblemsStore.getState().upsert({
      id: 'diag-1',
      severity: 'error',
      message: 'Test error',
      source: 'test',
      targetId: 'entity-1',
    })
    render(<BottomPanel />)
    const row = screen.getByText('Test error').closest('li')!
    row.focus()
    await userEvent.keyboard('{Enter}')
    expect(useSelectionStore.getState().selectedIds).toContain('entity-1')
  })

  it('problem rows without targetId do not select on Enter', async () => {
    useProblemsStore.getState().upsert({
      id: 'diag-1',
      severity: 'warning',
      message: 'No target',
      source: 'test',
    })
    render(<BottomPanel />)
    const row = screen.getByText('No target').closest('li')!
    row.focus()
    await userEvent.keyboard('{Enter}')
    expect(useSelectionStore.getState().selectedIds).toHaveLength(0)
  })
})
