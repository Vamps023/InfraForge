import { create } from 'zustand'
import type { WorkspaceId } from '../workspaces/workspaceRegistry'

export type ViewportInteraction = Readonly<{
  kind: 'primary-click' | 'pointer-move' | 'pointer-leave'
  easting: number
  northing: number
  height: number
  roadId?: string
}>

export interface ToolActivationConfig {
  id: string
  workspaceId: WorkspaceId
  statusHint?: string
  cancel?: () => void
  onViewportInteraction?: (interaction: ViewportInteraction) => void
  allowedSelectionTypes?: readonly string[]
  data?: Record<string, unknown>
}

interface ToolStoreState {
  activeToolId: string | null
  activeWorkspaceId: WorkspaceId | null
  statusHint: string | null
  allowedSelectionTypes: readonly string[] | null
  data: Record<string, unknown>
  cancelTool: (() => void) | null
  viewportHandler: ((interaction: ViewportInteraction) => void) | null

  activateTool: (config: ToolActivationConfig) => void
  cancelActiveTool: () => boolean
  clearTool: () => void
}

export const useToolStore = create<ToolStoreState>((set, get) => ({
  activeToolId: null,
  activeWorkspaceId: null,
  statusHint: null,
  allowedSelectionTypes: null,
  data: {},
  cancelTool: null,
  viewportHandler: null,

  activateTool: (config) => {
    // If a different tool is currently active, cancel it first
    const current = get()
    if (current.activeToolId && current.activeToolId !== config.id) {
      try {
        current.cancelTool?.()
      } catch (err) {
        console.error('Error cancelling previous tool:', err)
      }
    }

    set({
      activeToolId: config.id,
      activeWorkspaceId: config.workspaceId,
      statusHint: config.statusHint ?? null,
      allowedSelectionTypes: config.allowedSelectionTypes ?? null,
      data: config.data ?? {},
      cancelTool: config.cancel ?? null,
      viewportHandler: config.onViewportInteraction ?? null,
    })
  },

  cancelActiveTool: () => {
    const { cancelTool, activeToolId } = get()
    if (!activeToolId) {
      return false
    }

    try {
      cancelTool?.()
    } catch (err) {
      console.error('Error in cancelTool:', err)
    }

    set({
      activeToolId: null,
      activeWorkspaceId: null,
      statusHint: null,
      allowedSelectionTypes: null,
      data: {},
      cancelTool: null,
      viewportHandler: null,
    })
    return true
  },

  clearTool: () => {
    set({
      activeToolId: null,
      activeWorkspaceId: null,
      statusHint: null,
      allowedSelectionTypes: null,
      data: {},
      cancelTool: null,
      viewportHandler: null,
    })
  },
}))
