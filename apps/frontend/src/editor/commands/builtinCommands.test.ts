import { beforeEach, describe, expect, it, vi } from 'vitest'
import {
  registerBuiltinCommands,
  unregisterBuiltinCommands,
} from './builtinCommands'
import { commandRegistry, executeCommand } from './commandRegistry'
import { useShellUiStore } from '../shell/shellUiStore'
import { useProjectStore } from '../../features/project/projectStore'
import { useLayoutStore, clearLayoutPreferences } from '../layout/layoutStore'
import * as projectApi from '../../features/project/projectApi'
import type { AvailabilityContext } from '../availability'

function readyCtx(): { availability: AvailabilityContext } {
  return {
    availability: {
      engine: 'ready',
      engineMessage: 'ready',
      project: 'project-open',
      viewportActive: true,
    },
  }
}

function noProjectCtx(): { availability: AvailabilityContext } {
  return { availability: { ...readyCtx().availability, project: 'no-project' } }
}

function noEngineCtx(): { availability: AvailabilityContext } {
  return { availability: { ...readyCtx().availability, engine: 'starting', engineMessage: 'starting' } }
}

function busyCtx(): { availability: AvailabilityContext } {
  return { availability: { ...readyCtx().availability, project: 'busy' } }
}

beforeEach(() => {
  unregisterBuiltinCommands()
  for (const command of commandRegistry.all()) {
    commandRegistry.unregister(command.id)
  }
  registerBuiltinCommands({ getEngineClient: () => null })
  useShellUiStore.getState().closeDialog()
  useProjectStore.getState().clearProject()
  useProjectStore.getState().setLastError(null)
  clearLayoutPreferences()
  useLayoutStore.getState().hydrate()
})

describe('builtin commands registration', () => {
  it('registers the project lifecycle commands', () => {
    expect(commandRegistry.get('project.new')).toBeDefined()
    expect(commandRegistry.get('project.open')).toBeDefined()
    expect(commandRegistry.get('project.save')).toBeDefined()
    expect(commandRegistry.get('project.close')).toBeDefined()
    expect(commandRegistry.get('project.georeference')).toBeDefined()
  })

  it('registers the panel toggle commands', () => {
    expect(commandRegistry.get('panel.toggle-outliner')).toBeDefined()
    expect(commandRegistry.get('panel.toggle-inspector')).toBeDefined()
    expect(commandRegistry.get('panel.toggle-bottom')).toBeDefined()
  })
})

describe('builtin commands gating', () => {
  it('project.save is disabled with no project', async () => {
    const ran = await executeCommand('project.save', noProjectCtx())
    expect(ran).toBe(false)
  })

  it('project.save is disabled when the engine is not ready', async () => {
    const ran = await executeCommand('project.save', noEngineCtx())
    expect(ran).toBe(false)
  })

  it('project.new requires the engine but not a project', async () => {
    // No project is fine for New; only engine readiness gates it.
    const noEngine = await executeCommand('project.new', noEngineCtx())
    expect(noEngine).toBe(false)
  })

  it('project.close requires both engine and project', async () => {
    expect(await executeCommand('project.close', noProjectCtx())).toBe(false)
    expect(await executeCommand('project.close', noEngineCtx())).toBe(false)
  })

  it('panel.toggle-outliner does not require engine or project', async () => {
    const ran = await executeCommand('panel.toggle-outliner', noEngineCtx())
    expect(ran).toBe(true)
  })

  it('project.new is disabled while a project operation is busy', async () => {
    expect(await executeCommand('project.new', busyCtx())).toBe(false)
  })

  it('project.open is disabled while a project operation is busy', async () => {
    expect(await executeCommand('project.open', busyCtx())).toBe(false)
  })

  it('project.save is disabled while a project operation is busy', async () => {
    expect(await executeCommand('project.save', busyCtx())).toBe(false)
  })

  it('project.close is disabled while a project operation is busy', async () => {
    expect(await executeCommand('project.close', busyCtx())).toBe(false)
  })

  it('project.georeference is disabled while a project operation is busy', async () => {
    expect(await executeCommand('project.georeference', busyCtx())).toBe(false)
  })
})

describe('builtin commands execution', () => {
  it('project.new opens the new-project dialog', async () => {
    await executeCommand('project.new', readyCtx())
    expect(useShellUiStore.getState().openDialog).toBe('new-project')
  })

  it('project.georeference opens the georeference dialog', async () => {
    await executeCommand('project.georeference', readyCtx())
    expect(useShellUiStore.getState().openDialog).toBe('georeference')
  })

  it('panel.toggle-outliner toggles the outliner visibility', async () => {
    const before = useLayoutStore.getState().panels.left.visible
    await executeCommand('panel.toggle-outliner', readyCtx())
    expect(useLayoutStore.getState().panels.left.visible).toBe(!before)
  })

  it('project.save issues the real engine save command', async () => {
    const saveSpy = vi.spyOn(projectApi, 'saveProject').mockResolvedValue(undefined as never)
    unregisterBuiltinCommands()
    registerBuiltinCommands({ getEngineClient: () => ({}) as never })
    await executeCommand('project.save', readyCtx())
    expect(saveSpy).toHaveBeenCalledTimes(1)
    saveSpy.mockRestore()
  })

  it('project.close issues the real engine close command', async () => {
    const closeSpy = vi.spyOn(projectApi, 'closeProject').mockResolvedValue(undefined as never)
    unregisterBuiltinCommands()
    registerBuiltinCommands({ getEngineClient: () => ({}) as never })
    await executeCommand('project.close', readyCtx())
    expect(closeSpy).toHaveBeenCalledTimes(1)
    closeSpy.mockRestore()
  })

  it('project.open surfaces a real error when no desktop picker is available', async () => {
    unregisterBuiltinCommands()
    registerBuiltinCommands({ getEngineClient: () => ({}) as never })
    await executeCommand('project.open', readyCtx())
    // No desktop bridge in jsdom -> surfaces a real error, not a fake success.
    expect(useProjectStore.getState().lastError?.message).toMatch(/directory picker/)
  })
})

describe('builtin commands shortcut metadata', () => {
  it('project.new has Ctrl+N', () => {
    expect(commandRegistry.get('project.new')?.shortcut?.display).toBe('Ctrl+N')
  })
  it('project.open has Ctrl+O', () => {
    expect(commandRegistry.get('project.open')?.shortcut?.display).toBe('Ctrl+O')
  })
  it('project.save has Ctrl+S', () => {
    expect(commandRegistry.get('project.save')?.shortcut?.display).toBe('Ctrl+S')
  })
})
