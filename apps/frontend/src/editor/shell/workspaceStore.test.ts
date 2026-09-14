import { beforeEach, describe, expect, it } from 'vitest'
import { useWorkspaceStore, WORKSPACES, FUNCTIONAL_WORKSPACE_IDS, getWorkspace, type WorkspaceId } from './workspaceStore'

beforeEach(() => {
  useWorkspaceStore.getState().setWorkspace('terrain')
})

describe('workspaceStore', () => {
  it('starts on the terrain workspace', () => {
    expect(useWorkspaceStore.getState().activeWorkspace).toBe('terrain')
  })

  it('switches to a functional workspace', () => {
    useWorkspaceStore.getState().setWorkspace('home')
    expect(useWorkspaceStore.getState().activeWorkspace).toBe('home')
    useWorkspaceStore.getState().setWorkspace('terrain')
    expect(useWorkspaceStore.getState().activeWorkspace).toBe('terrain')
  })

  it('exposes all defined workspaces', () => {
    const ids = WORKSPACES.map((w) => w.id)
    expect(ids).toContain('home')
    expect(ids).toContain('terrain')
    expect(ids).toContain('roads')
    expect(ids).toContain('rail')
    expect(ids).toContain('environment')
    expect(ids).toContain('traffic')
    expect(ids).toContain('simulation')
  })

  it('only home and terrain are functional in v0.1', () => {
    expect(FUNCTIONAL_WORKSPACE_IDS).toEqual(['home', 'terrain'])
  })

  it('future workspaces are disabled', () => {
    const future = WORKSPACES.filter((w) => !w.enabled)
    const futureIds = future.map((w) => w.id)
    expect(futureIds).toEqual(['roads', 'rail', 'environment', 'traffic', 'simulation'])
  })

  it('future workspaces have a coming-later label', () => {
    for (const ws of WORKSPACES) {
      if (!ws.enabled) {
        expect(ws.futureLabel).toBeDefined()
        expect(ws.futureLabel).toContain('coming later')
      }
    }
  })

  it('getWorkspace returns the definition by id', () => {
    expect(getWorkspace('terrain')?.label).toBe('Terrain')
    expect(getWorkspace('nonexistent' as WorkspaceId)).toBeUndefined()
  })
})
