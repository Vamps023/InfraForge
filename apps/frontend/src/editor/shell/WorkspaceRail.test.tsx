import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { WorkspaceSwitcher } from './WorkspaceSwitcher'
import { WorkspaceRail } from './WorkspaceRail'
import { useWorkspaceStore } from './workspaceStore'
import { workspaceRegistry } from '../workspaces/workspaceRegistry'

beforeEach(() => {
  useWorkspaceStore.getState().setWorkspace('roads')
})

afterEach(() => {
  useWorkspaceStore.getState().setWorkspace('roads')
})

describe('WorkspaceSwitcher (and WorkspaceRail alias)', () => {
  it('renders enabled production workspaces in release mode', () => {
    render(<WorkspaceSwitcher />)
    const visible = workspaceRegistry.getVisible()
    for (const ws of visible) {
      expect(screen.getByLabelText(ws.label)).toBeInTheDocument()
    }
    // Future unreleased workspaces should not be rendered
    expect(screen.queryByLabelText('Rail')).not.toBeInTheDocument()
    expect(screen.queryByLabelText('Simulation')).not.toBeInTheDocument()
  })

  it('renders all workspaces when includeFeatureGated is true', () => {
    render(<WorkspaceSwitcher includeFeatureGated />)
    expect(screen.getByLabelText('Rail')).toBeInTheDocument()
    expect(screen.getByLabelText('Simulation')).toBeInTheDocument()
    expect(screen.getByLabelText('Rail')).toBeDisabled()
  })

  it('marks the active workspace with aria-current', () => {
    render(<WorkspaceSwitcher />)
    const roadsBtn = screen.getByLabelText('Roads')
    expect(roadsBtn).toHaveAttribute('aria-current', 'page')
    const homeBtn = screen.getByLabelText('Home')
    expect(homeBtn).not.toHaveAttribute('aria-current')
  })

  it('switches workspace when clicking a functional workspace', async () => {
    render(<WorkspaceSwitcher />)
    const homeBtn = screen.getByLabelText('Home')
    await userEvent.click(homeBtn)
    expect(useWorkspaceStore.getState().activeWorkspace).toBe('home')
  })

  it('switches to World and Roads workspaces when clicked', async () => {
    render(<WorkspaceSwitcher />)
    const worldBtn = screen.getByLabelText('World')
    await userEvent.click(worldBtn)
    expect(useWorkspaceStore.getState().activeWorkspace).toBe('world')

    const roadsBtn = screen.getByLabelText('Roads')
    await userEvent.click(roadsBtn)
    expect(useWorkspaceStore.getState().activeWorkspace).toBe('roads')
  })

  it('does not switch when clicking a disabled workspace', async () => {
    render(<WorkspaceSwitcher includeFeatureGated />)
    const railBtn = screen.getByLabelText('Rail')
    expect(railBtn).toBeDisabled()
    expect(railBtn).toHaveAttribute('aria-disabled', 'true')
    await userEvent.click(railBtn)
    expect(useWorkspaceStore.getState().activeWorkspace).toBe('roads')
  })

  it('WorkspaceRail alias works identically', () => {
    render(<WorkspaceRail />)
    expect(screen.getByLabelText('Terrain')).toBeInTheDocument()
    expect(screen.getByLabelText('Roads')).toBeInTheDocument()
  })
})
