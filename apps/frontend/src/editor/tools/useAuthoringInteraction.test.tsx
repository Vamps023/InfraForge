import { act, renderHook } from '@testing-library/react'
import { beforeEach, describe, expect, it, vi } from 'vitest'
import { useAuthoringDraftStore } from './authoringDraftStore'
import { useAuthoringInteraction } from './useAuthoringInteraction'
import { useToolStore } from './toolStore'

beforeEach(() => {
  useAuthoringDraftStore.getState().setTool('select')
  useToolStore.getState().clearTool()
})

describe('useAuthoringInteraction', () => {
  it('leaves central tool ownership to the application bridge', () => {
    const cancel = vi.fn()
    useToolStore.getState().activateTool({
      id: 'road.authoring.bridge-owner',
      workspaceId: 'roads',
      cancel,
    })

    renderHook(() => useAuthoringInteraction({ getClient: () => null }))

    act(() => {
      useAuthoringDraftStore.getState().setTool('road.straight')
    })

    expect(useAuthoringDraftStore.getState().activeTool).toBe('road.straight')
    expect(useToolStore.getState().activeToolId).toBe('road.authoring.bridge-owner')
    expect(cancel).not.toHaveBeenCalled()
  })

  it('updates hover point on pointer-move and clears on pointer-leave', async () => {
    const { result } = renderHook(() => useAuthoringInteraction({ getClient: () => null }))

    act(() => {
      useAuthoringDraftStore.getState().setTool('road.straight')
    })

    await act(async () => {
      await result.current.handleViewportInteraction({
        kind: 'pointer-move',
        easting: 500000,
        northing: 4000000,
        height: 100,
      })
    })

    expect(useAuthoringDraftStore.getState().hoverPoint).toEqual({
      easting: 500000,
      northing: 4000000,
    })

    await act(async () => {
      await result.current.handleViewportInteraction({
        kind: 'pointer-leave',
        easting: 0,
        northing: 0,
        height: 0,
      })
    })

    expect(useAuthoringDraftStore.getState().hoverPoint).toBeNull()
    expect(useAuthoringDraftStore.getState().snappedHoverPoint).toBeNull()
  })
})
