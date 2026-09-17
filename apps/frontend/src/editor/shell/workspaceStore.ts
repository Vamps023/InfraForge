import { create } from 'zustand'
import type { LucideIcon } from 'lucide-react'
import {
  workspaceRegistry,
  type WorkspaceId,
  type WorkspaceDefinition as CanonicalWorkspaceDefinition,
} from '../workspaces/workspaceRegistry'
import { useToolStore } from '../tools/toolStore'

export type { WorkspaceId }

// Backwards-compatible interface adapting CanonicalWorkspaceDefinition
export interface WorkspaceDefinition {
  id: WorkspaceId
  label: string
  icon: LucideIcon
  enabled: boolean
  futureLabel?: string
}

// Adapts canonical workspace definitions into legacy-compatible shape
function toLegacyDefinition(def: CanonicalWorkspaceDefinition): WorkspaceDefinition {
  return {
    id: def.id,
    label: def.label,
    icon: def.icon,
    enabled: def.availability.enabled,
    futureLabel: def.availability.disabledReason,
  }
}

export const WORKSPACES: WorkspaceDefinition[] = workspaceRegistry
  .getAll()
  .map(toLegacyDefinition)

// Functional authoring workspaces including World, Terrain, and Roads
export const FUNCTIONAL_WORKSPACE_IDS: WorkspaceId[] = workspaceRegistry
  .getVisible()
  .map((w) => w.id)

interface WorkspaceState {
  activeWorkspace: WorkspaceId
  setWorkspace: (id: WorkspaceId) => void
}

export const useWorkspaceStore = create<WorkspaceState>((set, get) => ({
  activeWorkspace: 'terrain',
  setWorkspace: (nextWorkspace) => {
    // Explicit cancellation invariant:
    // If switching workspaces invalidates an in-progress operation,
    // cancel it explicitly. Never silently commit on workspace switch.
    const toolStore = useToolStore.getState()
    if (toolStore.activeToolId && toolStore.activeWorkspaceId !== nextWorkspace) {
      toolStore.cancelActiveTool()
    }

    const current = get().activeWorkspace
    if (current === nextWorkspace) {
      return
    }

    // Verify valid workspace or fallback
    const resolved = workspaceRegistry.resolveValidWorkspace(nextWorkspace, true)
    set({ activeWorkspace: resolved })
  },
}))

// Returns the workspace definition by ID, or undefined if not found.
export function getWorkspace(id: WorkspaceId): WorkspaceDefinition | undefined {
  const canonical = workspaceRegistry.get(id)
  return canonical ? toLegacyDefinition(canonical) : undefined
}
