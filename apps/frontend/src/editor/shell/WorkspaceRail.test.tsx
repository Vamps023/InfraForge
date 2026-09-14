import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { render, screen, fireEvent } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { WorkspaceRail } from './WorkspaceRail'
import { useWorkspaceStore, WORKSPACES } from './workspaceStore'

beforeEach(() => {
  useWorkspaceStore.getState().setWorkspace('terrain')
})

afterEach(() => {
  useWorkspaceStore.getState().setWorkspace('terrain')
})

describe('WorkspaceRail', () => {
  it('renders all workspace buttons', () => {
    render(<WorkspaceRail />)
    for (const ws of WORKSPACES) {
      expect(screen.getByLabelText(ws.label)).toBeInTheDocument()
    }
  })

  it('marks the active workspace with aria-current', () => {
    render(<WorkspaceRail />)
    const terrainBtn = screen.getByLabelText('Terrain')
    expect(terrainBtn).toHaveAttribute('aria-current', 'page')
    const homeBtn = screen.getByLabelText('Home')
    expect(homeBtn).not.toHaveAttribute('aria-current')
  })

  it('switches workspace when clicking a functional workspace', async () => {
    render(<WorkspaceRail />)
    const homeBtn = screen.getByLabelText('Home')
    await userEvent.click(homeBtn)
    expect(useWorkspaceStore.getState().activeWorkspace).toBe('home')
  })

  it('does not switch when clicking a disabled workspace', async () => {
    render(<WorkspaceRail />)
    const roadsBtn = screen.getByLabelText('Roads')
    expect(roadsBtn).toBeDisabled()
    await userEvent.click(roadsBtn)
    expect(useWorkspaceStore.getState().activeWorkspace).toBe('terrain')
  })

  it('disabled workspaces have aria-disabled', () => {
    render(<WorkspaceRail />)
    const roadsBtn = screen.getByLabelText('Roads')
    expect(roadsBtn).toHaveAttribute('aria-disabled', 'true')
  })

  it('functional workspaces do not have aria-disabled', () => {
    render(<WorkspaceRail />)
    const terrainBtn = screen.getByLabelText('Terrain')
    expect(terrainBtn).not.toHaveAttribute('aria-disabled')
  })
})
