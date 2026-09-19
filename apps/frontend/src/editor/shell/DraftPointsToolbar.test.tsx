import { beforeEach, describe, expect, it, vi } from 'vitest'
import { render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { DraftPointsToolbar } from './DraftPointsToolbar'
import { useAuthoringDraftStore } from '../tools/authoringDraftStore'

describe('DraftPointsToolbar', () => {
  beforeEach(() => {
    useAuthoringDraftStore.getState().clearDraft()
    useAuthoringDraftStore.getState().setTool('select')
  })

  it('renders nothing when activeTool is select', () => {
    render(<DraftPointsToolbar onCommit={vi.fn()} onCancel={vi.fn()} />)
    expect(screen.queryByRole('toolbar')).not.toBeInTheDocument()
  })

  it('renders nothing when draft has 0 points', () => {
    useAuthoringDraftStore.getState().setTool('road.straight')
    render(<DraftPointsToolbar onCommit={vi.fn()} onCancel={vi.fn()} />)
    expect(screen.queryByRole('toolbar')).not.toBeInTheDocument()
  })

  it('renders vertex count and buttons when draft has points', () => {
    useAuthoringDraftStore.getState().setTool('road.straight')
    useAuthoringDraftStore.getState().addDraftPoint({ easting: 0, northing: 0 })

    render(<DraftPointsToolbar onCommit={vi.fn()} onCancel={vi.fn()} />)
    expect(screen.getByRole('toolbar')).toBeInTheDocument()
    expect(screen.getByText('1 vertex')).toBeInTheDocument()
    expect(screen.getByRole('button', { name: /cancel/i })).toBeInTheDocument()
  })

  it('enables complete button when minimum points are reached', () => {
    useAuthoringDraftStore.getState().setTool('road.straight')
    useAuthoringDraftStore.getState().addDraftPoint({ easting: 0, northing: 0 })
    useAuthoringDraftStore.getState().addDraftPoint({ easting: 100, northing: 0 })

    render(<DraftPointsToolbar onCommit={vi.fn()} onCancel={vi.fn()} />)
    expect(screen.getByText('2 vertices')).toBeInTheDocument()
    const completeBtn = screen.getByRole('button', { name: /complete/i })
    expect(completeBtn).not.toBeDisabled()
  })
})
