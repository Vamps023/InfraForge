import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { ContextToolbar } from './ContextToolbar'
import { useWorkspaceStore } from './workspaceStore'
import { useShellUiStore } from './shellUiStore'
import { useProjectStore } from '../../features/project/projectStore'
import { create } from '@bufbuild/protobuf'
import { ProjectSummarySchema, type ProjectSummary } from '@infraforge/protocol'

beforeEach(() => {
  useWorkspaceStore.getState().setWorkspace('terrain')
  useShellUiStore.getState().closeDialog()
  useProjectStore.getState().clearProject()
})

afterEach(() => {
  useWorkspaceStore.getState().setWorkspace('terrain')
  useShellUiStore.getState().closeDialog()
  useProjectStore.getState().clearProject()
})

function makeSummary(): ProjectSummary {
  return create(ProjectSummarySchema, {
    projectUuid: 'test-uuid',
    displayName: 'Test Project',
    directory: '/test',
    revision: 1n,
    dirty: false,
    georeference: { horizontalCrs: 'EPSG:3857', originHeight: 0 },
  })
}

describe('ContextToolbar', () => {
  it('renders terrain actions when terrain workspace is active', () => {
    useProjectStore.getState().setSummary(makeSummary())
    render(<ContextToolbar />)
    expect(screen.getByText('Import')).toBeInTheDocument()
    expect(screen.getByText('Download Area')).toBeInTheDocument()
    expect(screen.getByText('Georeference')).toBeInTheDocument()
  })

  it('does not render when home workspace is active', () => {
    useWorkspaceStore.getState().setWorkspace('home')
    const { container } = render(<ContextToolbar />)
    expect(container.firstChild).toBeNull()
  })

  it('disables actions when no project is open', () => {
    render(<ContextToolbar />)
    expect(screen.getByText('Import').closest('button')).toBeDisabled()
    expect(screen.getByText('Download Area').closest('button')).toBeDisabled()
    expect(screen.getByText('Georeference').closest('button')).toBeDisabled()
  })

  it('enables actions when a project is open', () => {
    useProjectStore.getState().setSummary(makeSummary())
    render(<ContextToolbar />)
    expect(screen.getByText('Import').closest('button')).not.toBeDisabled()
    expect(screen.getByText('Download Area').closest('button')).not.toBeDisabled()
    expect(screen.getByText('Georeference').closest('button')).not.toBeDisabled()
  })

  it('opens import dialog in local-file mode when Import is clicked', async () => {
    useProjectStore.getState().setSummary(makeSummary())
    render(<ContextToolbar />)
    await userEvent.click(screen.getByText('Import'))
    expect(useShellUiStore.getState().openDialog).toBe('import-terrain')
    expect(useShellUiStore.getState().terrainImportMode).toBe('local-file')
  })

  it('opens import dialog in download-area mode when Download Area is clicked', async () => {
    useProjectStore.getState().setSummary(makeSummary())
    render(<ContextToolbar />)
    await userEvent.click(screen.getByText('Download Area'))
    expect(useShellUiStore.getState().openDialog).toBe('import-terrain')
    expect(useShellUiStore.getState().terrainImportMode).toBe('download-area')
  })

  it('opens georeference dialog when Georeference is clicked', async () => {
    useProjectStore.getState().setSummary(makeSummary())
    render(<ContextToolbar />)
    await userEvent.click(screen.getByText('Georeference'))
    expect(useShellUiStore.getState().openDialog).toBe('georeference')
  })
})
