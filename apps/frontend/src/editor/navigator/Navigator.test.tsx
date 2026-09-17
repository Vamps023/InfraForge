import { beforeEach, describe, expect, it } from 'vitest'
import { render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { Navigator } from './Navigator'
import { useWorkspaceStore } from '../shell/workspaceStore'

beforeEach(() => {
  useWorkspaceStore.getState().setWorkspace('terrain')
})

describe('Navigator', () => {
  it('renders Scene and Sources capability tabs', () => {
    render(<Navigator />)
    expect(screen.getByRole('tab', { name: 'Scene' })).toBeInTheDocument()
    expect(screen.getByRole('tab', { name: 'Sources' })).toBeInTheDocument()
  })

  it('switches to Sources tab when clicked', async () => {
    render(<Navigator />)
    const sourcesTab = screen.getByRole('tab', { name: 'Sources' })
    await userEvent.click(sourcesTab)
    expect(sourcesTab).toHaveAttribute('aria-selected', 'true')
    expect(screen.getByPlaceholderText('Filter sources…')).toBeInTheDocument()
  })

  it('defaults to sources tab when in World workspace', () => {
    useWorkspaceStore.getState().setWorkspace('world')
    render(<Navigator />)
    const sourcesTab = screen.getByRole('tab', { name: 'Sources' })
    expect(sourcesTab).toHaveAttribute('aria-selected', 'true')
  })
})
