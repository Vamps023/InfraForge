import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { cleanup, render, screen, act } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { ContextToolbar } from './ContextToolbar'
import { useWorkspaceStore } from './workspaceStore'
import { useShellUiStore } from './shellUiStore'
import { useProjectStore } from '../../features/project/projectStore'
import { registerBuiltinCommands, unregisterBuiltinCommands } from '../commands/builtinCommands'
import { registerTerrainCommands, unregisterTerrainCommands } from '../../features/terrain/terrainCommands'
import { registerRoadCommands, unregisterRoadCommands } from '../../features/road/roadCommands'
import { commandRegistry, type CommandContext } from '../commands/useCommands'
import type { AvailabilityContext } from '../availability'
import { create } from '@bufbuild/protobuf'
import { ProjectSummarySchema, type ProjectSummary } from '@infraforge/protocol'
import { useRoadToolStore } from '../../features/road/roadToolStore'

// Context toolbar tests. In addition to the behavioral command-routing tests
// below, a group-composition suite pins the reference-informed grouping and
// accessibility contract (labeled role="group" sections) so the toolbar
// cannot silently regress to a flat, unlabeled button row.

function readyContext(): AvailabilityContext {
  return {
    engine: 'ready',
    engineMessage: 'ready',
    project: 'project-open',
    viewportActive: true,
  }
}

describe('ContextToolbar group composition', () => {
  it('organizes the terrain toolbar into labeled groups', () => {
    useProjectStore.getState().setSummary(makeSummary())
    useWorkspaceStore.getState().setWorkspace('terrain')
    render(<ContextToolbar context={ctx(readyContext())} />)
    expect(screen.getByRole('group', { name: 'Terrain acquisition' })).toBeInTheDocument()
    expect(screen.getByRole('group', { name: 'Terrain management' })).toBeInTheDocument()
  })

  it('organizes the roads toolbar into draw/author/history groups', () => {
    useProjectStore.getState().setSummary(makeSummary())
    useWorkspaceStore.getState().setWorkspace('roads')
    render(<ContextToolbar context={ctx(readyContext())} />)
    expect(screen.getByRole('group', { name: 'Road authoring' })).toBeInTheDocument()
    expect(screen.getByRole('group', { name: 'Road edit history' })).toBeInTheDocument()
    // The drawing-mode group is transient: absent when not drawing.
    expect(screen.queryByRole('group', { name: 'Road drawing mode' })).not.toBeInTheDocument()
  })

  it('transitions cleanly between idle and road drawing without hook violations', async () => {
    useProjectStore.getState().setSummary(makeSummary())
    useWorkspaceStore.getState().setWorkspace('roads')
    const { rerender } = render(<ContextToolbar context={ctx(readyContext())} />)

    // 1. Initially idle: no road drawing group
    expect(screen.queryByRole('group', { name: 'Road drawing mode' })).not.toBeInTheDocument()

    // 2. Begin drawing
    act(() => {
      useRoadToolStore.getState().begin('Test Road', 0.1, null)
    })
    rerender(<ContextToolbar context={ctx(readyContext())} />)

    // Drawing group appears with Finish and Cancel buttons
    expect(screen.getByRole('group', { name: 'Road drawing mode' })).toBeInTheDocument()
    expect(screen.getByRole('button', { name: /Finish/ })).toBeInTheDocument()
    const cancelBtn = screen.getByRole('button', { name: /Cancel/ })
    expect(cancelBtn).toBeInTheDocument()

    // 3. Click cancel
    await userEvent.click(cancelBtn)
    rerender(<ContextToolbar context={ctx(readyContext())} />)

    // Drawing group disappears cleanly
    expect(screen.queryByRole('group', { name: 'Road drawing mode' })).not.toBeInTheDocument()
    expect(useRoadToolStore.getState().mode).toBe('idle')
  })
})

function noProjectContext(): AvailabilityContext {
  return {
    engine: 'ready',
    engineMessage: 'ready',
    project: 'no-project',
    viewportActive: true,
  }
}

function busyContext(): AvailabilityContext {
  return {
    engine: 'ready',
    engineMessage: 'ready',
    project: 'busy',
    viewportActive: true,
  }
}

function ctx(availability: AvailabilityContext): CommandContext {
  return { availability }
}

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

beforeEach(() => {
  for (const cmd of commandRegistry.all()) {
    commandRegistry.unregister(cmd.id)
  }
  registerBuiltinCommands({ getEngineClient: () => null })
  registerTerrainCommands({ getEngineClient: () => null })
  registerRoadCommands({ getEngineClient: () => null })
  useWorkspaceStore.getState().setWorkspace('terrain')
  useShellUiStore.getState().closeDialog()
  useProjectStore.getState().clearProject()
})

afterEach(() => {
  unregisterBuiltinCommands()
  unregisterTerrainCommands()
  unregisterRoadCommands()
  useWorkspaceStore.getState().setWorkspace('terrain')
  useShellUiStore.getState().closeDialog()
  useProjectStore.getState().clearProject()
})

describe('ContextToolbar', () => {
  it('renders terrain actions when terrain workspace is active', () => {
    useProjectStore.getState().setSummary(makeSummary())
    render(<ContextToolbar context={ctx(readyContext())} />)
    expect(screen.getByText('Import')).toBeInTheDocument()
    expect(screen.getByText('Download Area')).toBeInTheDocument()
    expect(screen.getByText('Georeference')).toBeInTheDocument()
  })

  it('does not render when home workspace is active', () => {
    useWorkspaceStore.getState().setWorkspace('home')
    const { container } = render(<ContextToolbar context={ctx(readyContext())} />)
    expect(container.firstChild).toBeNull()
  })

  it('disables actions when no project is open', () => {
    render(<ContextToolbar context={ctx(noProjectContext())} />)
    expect(screen.getByText('Import').closest('button')).toBeDisabled()
    expect(screen.getByText('Download Area').closest('button')).toBeDisabled()
    expect(screen.getByText('Georeference').closest('button')).toBeDisabled()
  })

  it('enables actions when a project is open and engine is ready', () => {
    useProjectStore.getState().setSummary(makeSummary())
    render(<ContextToolbar context={ctx(readyContext())} />)
    expect(screen.getByText('Import').closest('button')).not.toBeDisabled()
    expect(screen.getByText('Download Area').closest('button')).not.toBeDisabled()
    expect(screen.getByText('Georeference').closest('button')).not.toBeDisabled()
  })

  it('disables actions when a project operation is busy', () => {
    useProjectStore.getState().setSummary(makeSummary())
    render(<ContextToolbar context={ctx(busyContext())} />)
    expect(screen.getByText('Import').closest('button')).toBeDisabled()
    expect(screen.getByText('Download Area').closest('button')).toBeDisabled()
    expect(screen.getByText('Georeference').closest('button')).toBeDisabled()
  })

  it('disables actions when engine is not ready', () => {
    useProjectStore.getState().setSummary(makeSummary())
    const engineNotReady: AvailabilityContext = {
      engine: 'starting',
      engineMessage: 'starting',
      project: 'project-open',
      viewportActive: true,
    }
    render(<ContextToolbar context={ctx(engineNotReady)} />)
    expect(screen.getByText('Import').closest('button')).toBeDisabled()
    expect(screen.getByText('Download Area').closest('button')).toBeDisabled()
    expect(screen.getByText('Georeference').closest('button')).toBeDisabled()
  })

  it('opens import dialog in local-file mode when Import is clicked', async () => {
    useProjectStore.getState().setSummary(makeSummary())
    render(<ContextToolbar context={ctx(readyContext())} />)
    await userEvent.click(screen.getByText('Import'))
    expect(useShellUiStore.getState().openDialog).toBe('import-terrain')
    expect(useShellUiStore.getState().terrainImportMode).toBe('local-file')
  })

  it('opens import dialog in download-area mode when Download Area is clicked', async () => {
    useProjectStore.getState().setSummary(makeSummary())
    render(<ContextToolbar context={ctx(readyContext())} />)
    await userEvent.click(screen.getByText('Download Area'))
    expect(useShellUiStore.getState().openDialog).toBe('import-terrain')
    expect(useShellUiStore.getState().terrainImportMode).toBe('download-area')
  })

  it('opens georeference dialog when Georeference is clicked', async () => {
    useProjectStore.getState().setSummary(makeSummary())
    render(<ContextToolbar context={ctx(readyContext())} />)
    await userEvent.click(screen.getByText('Georeference'))
    expect(useShellUiStore.getState().openDialog).toBe('georeference')
  })

  it('does not open import dialog when busy and Import is clicked', async () => {
    useProjectStore.getState().setSummary(makeSummary())
    render(<ContextToolbar context={ctx(busyContext())} />)
    await userEvent.click(screen.getByText('Import'))
    expect(useShellUiStore.getState().openDialog).toBeNull()
  })

  it('generic terrain.import opens local-file mode after Download Area was used and closed', async () => {
    useProjectStore.getState().setSummary(makeSummary())

    // Use the dedicated Download Area command (sets mode to download-area)
    useShellUiStore.getState().openTerrainImport('download-area')
    expect(useShellUiStore.getState().terrainImportMode).toBe('download-area')

    // Close the dialog
    useShellUiStore.getState().closeDialog()
    expect(useShellUiStore.getState().openDialog).toBeNull()
    // Mode is still download-area (stale) — the generic command must
    // override it, not inherit it.

    // Execute the generic terrain.import command
    const command = commandRegistry.get('terrain.import')
    expect(command).toBeDefined()
    await command!.execute(ctx(readyContext()))

    expect(useShellUiStore.getState().openDialog).toBe('import-terrain')
    expect(useShellUiStore.getState().terrainImportMode).toBe('local-file')
  })

  it('dedicated Download Area toolbar command still opens download-area mode', async () => {
    useProjectStore.getState().setSummary(makeSummary())
    render(<ContextToolbar context={ctx(readyContext())} />)

    // First use Import (local-file)
    await userEvent.click(screen.getByText('Import'))
    expect(useShellUiStore.getState().terrainImportMode).toBe('local-file')
    useShellUiStore.getState().closeDialog()

    // Then use Download Area — must open in download-area mode
    await userEvent.click(screen.getByText('Download Area'))
    expect(useShellUiStore.getState().openDialog).toBe('import-terrain')
    expect(useShellUiStore.getState().terrainImportMode).toBe('download-area')
  })
})
