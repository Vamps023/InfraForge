import { beforeEach, describe, expect, it, vi } from 'vitest'
import { useToolStore } from './toolStore'

beforeEach(() => {
  useToolStore.getState().clearTool()
})

describe('toolStore', () => {
  it('activates a tool and cancels existing tool', () => {
    const cancel1 = vi.fn()
    const cancel2 = vi.fn()

    useToolStore.getState().activateTool({
      id: 'tool-1',
      workspaceId: 'roads',
      cancel: cancel1,
    })

    expect(useToolStore.getState().activeToolId).toBe('tool-1')
    expect(useToolStore.getState().activeWorkspaceId).toBe('roads')

    useToolStore.getState().activateTool({
      id: 'tool-2',
      workspaceId: 'terrain',
      cancel: cancel2,
    })

    expect(cancel1).toHaveBeenCalledOnce()
    expect(useToolStore.getState().activeToolId).toBe('tool-2')
    expect(useToolStore.getState().activeWorkspaceId).toBe('terrain')
  })

  it('cancelActiveTool calls cancel callback and clears state', () => {
    const cancelSpy = vi.fn()
    useToolStore.getState().activateTool({
      id: 'road.drawing',
      workspaceId: 'roads',
      statusHint: 'Click to place road point',
      cancel: cancelSpy,
    })

    expect(useToolStore.getState().statusHint).toBe('Click to place road point')

    const cancelled = useToolStore.getState().cancelActiveTool()
    expect(cancelled).toBe(true)
    expect(cancelSpy).toHaveBeenCalledOnce()
    expect(useToolStore.getState().activeToolId).toBeNull()
    expect(useToolStore.getState().statusHint).toBeNull()
  })

  it('cancelActiveTool returns false if no active tool', () => {
    const cancelled = useToolStore.getState().cancelActiveTool()
    expect(cancelled).toBe(false)
  })

  it('forwards viewport interaction to active tool handler', () => {
    const handler = vi.fn()
    useToolStore.getState().activateTool({
      id: 'road.drawing',
      workspaceId: 'roads',
      onViewportInteraction: handler,
    })

    const interaction = {
      kind: 'primary-click' as const,
      easting: 100,
      northing: 200,
      height: 10,
    }

    useToolStore.getState().viewportHandler?.(interaction)
    expect(handler).toHaveBeenCalledWith(interaction)
  })
})
