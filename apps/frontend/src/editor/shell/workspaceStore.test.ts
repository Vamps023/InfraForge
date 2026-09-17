import { beforeEach, describe, expect, it, vi } from 'vitest'
import {
  useWorkspaceStore,
  WORKSPACES,
  FUNCTIONAL_WORKSPACE_IDS,
  getWorkspace,
  type WorkspaceId,
} from './workspaceStore'
import { useToolStore } from '../tools/toolStore'

beforeEach(() => {
  useToolStore.getState().clearTool()
  useWorkspaceStore.getState().setWorkspace('terrain')
})

describe('workspaceStore', () => {
  it('starts on the terrain workspace', () => {
    expect(useWorkspaceStore.getState().activeWorkspace).toBe('terrain')
  })

  it('switches to a functional workspace', () => {
    useWorkspaceStore.getState().setWorkspace('home')
    expect(useWorkspaceStore.getState().activeWorkspace).toBe('home')
    useWorkspaceStore.getState().setWorkspace('world')
    expect(useWorkspaceStore.getState().activeWorkspace).toBe('world')
    useWorkspaceStore.getState().setWorkspace('roads')
    expect(useWorkspaceStore.getState().activeWorkspace).toBe('roads')
    useWorkspaceStore.getState().setWorkspace('terrain')
    expect(useWorkspaceStore.getState().activeWorkspace).toBe('terrain')
  })

  it('exposes all defined workspaces', () => {
    const ids = WORKSPACES.map((w) => w.id)
    expect(ids).toContain('home')
    expect(ids).toContain('world')
    expect(ids).toContain('terrain')
    expect(ids).toContain('roads')
    expect(ids).toContain('rail')
    expect(ids).toContain('environment-assets')
    expect(ids).toContain('infrastructure')
    expect(ids).toContain('simulation')
  })

  it('home, world, terrain, and roads are functional', () => {
    expect(FUNCTIONAL_WORKSPACE_IDS).toEqual(['home', 'world', 'terrain', 'roads'])
  })

  it('future workspaces are disabled with coming-later description', () => {
    const future = WORKSPACES.filter((w) => !w.enabled)
    expect(future.length).toBeGreaterThanOrEqual(4)
    for (const ws of future) {
      expect(ws.futureLabel).toBeDefined()
      expect(ws.futureLabel).toContain('coming in future release')
    }
  })

  it('getWorkspace returns the definition by id', () => {
    expect(getWorkspace('terrain')?.label).toBe('Terrain')
    expect(getWorkspace('roads')?.label).toBe('Roads')
    expect(getWorkspace('nonexistent' as WorkspaceId)).toBeUndefined()
  })

  it('cancels active tool when switching to a different workspace', () => {
    const cancelSpy = vi.fn()
    useToolStore.getState().activateTool({
      id: 'road.drawing',
      workspaceId: 'roads',
      cancel: cancelSpy,
    })

    expect(useToolStore.getState().activeToolId).toBe('road.drawing')

    // Switch from roads to terrain
    useWorkspaceStore.getState().setWorkspace('terrain')

    expect(cancelSpy).toHaveBeenCalledOnce()
    expect(useToolStore.getState().activeToolId).toBeNull()
  })
})
