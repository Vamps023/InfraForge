import { beforeEach, describe, expect, it } from 'vitest'
import { render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { ToolOptionsPanel } from './ToolOptionsPanel'
import { useAuthoringDraftStore } from '../tools/authoringDraftStore'

describe('ToolOptionsPanel', () => {
  beforeEach(() => {
    useAuthoringDraftStore.getState().clearDraft()
    useAuthoringDraftStore.getState().setTool('select')
  })

  it('renders nothing when activeTool is select', () => {
    render(<ToolOptionsPanel />)
    expect(screen.queryByRole('region', { name: /tool options/i })).not.toBeInTheDocument()
  })

  it('renders floating panel when road tool is active', () => {
    useAuthoringDraftStore.getState().setTool('road.straight')
    render(<ToolOptionsPanel />)

    expect(screen.getByRole('region', { name: /tool options/i })).toBeInTheDocument()
    expect(screen.getByText('Straight')).toBeInTheDocument()
    expect(screen.getByRole('tab', { name: /tool/i })).toBeInTheDocument()
    expect(screen.getByRole('tab', { name: /snap/i })).toBeInTheDocument()
    expect(screen.getByRole('tab', { name: /lanes/i })).toBeInTheDocument()
  })

  it('switches to Snap tab and toggles snapping options', async () => {
    useAuthoringDraftStore.getState().setTool('road.arc')
    render(<ToolOptionsPanel />)

    const snapTab = screen.getByRole('tab', { name: /snap/i })
    await userEvent.click(snapTab)

    expect(screen.getByText('Grid Snap')).toBeInTheDocument()
    expect(screen.getByText('Angle Snap')).toBeInTheDocument()
    expect(screen.getByText('Endpoint Snap')).toBeInTheDocument()
  })
})
