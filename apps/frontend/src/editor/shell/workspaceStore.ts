import { create } from 'zustand'
import type { LucideIcon } from 'lucide-react'
import {
  Home,
  Mountain,
  Road,
  Train,
  Trees,
  Car,
  PlayCircle,
} from 'lucide-react'

// Workspace definitions for the vertical workspace rail. Only workspaces
// with real functionality are enabled; future workspaces appear as clearly
// disabled with a "Coming later" tooltip (no fake functionality).
//
// Terrain is the first fully functional workspace (v0.1). Roads, Rail,
// Environment, Traffic, and Simulation are future modules.

export type WorkspaceId = 'home' | 'terrain' | 'roads' | 'rail' | 'environment' | 'traffic' | 'simulation'

export interface WorkspaceDefinition {
  id: WorkspaceId
  label: string
  icon: LucideIcon
  enabled: boolean
  futureLabel?: string
}

export const WORKSPACES: WorkspaceDefinition[] = [
  { id: 'home', label: 'Home', icon: Home, enabled: true },
  { id: 'terrain', label: 'Terrain', icon: Mountain, enabled: true },
  { id: 'roads', label: 'Roads', icon: Road, enabled: false, futureLabel: 'Roads — coming later' },
  { id: 'rail', label: 'Rail', icon: Train, enabled: false, futureLabel: 'Rail — coming later' },
  { id: 'environment', label: 'Environment', icon: Trees, enabled: false, futureLabel: 'Environment — coming later' },
  { id: 'traffic', label: 'Traffic', icon: Car, enabled: false, futureLabel: 'Traffic — coming later' },
  { id: 'simulation', label: 'Simulation', icon: PlayCircle, enabled: false, futureLabel: 'Simulation — coming later' },
]

// The terrain workspace is the only functional authoring workspace in v0.1.
export const FUNCTIONAL_WORKSPACE_IDS: WorkspaceId[] = ['home', 'terrain']

interface WorkspaceState {
  activeWorkspace: WorkspaceId
  setWorkspace: (id: WorkspaceId) => void
}

export const useWorkspaceStore = create<WorkspaceState>((set) => ({
  activeWorkspace: 'terrain',
  setWorkspace: (activeWorkspace) => set({ activeWorkspace }),
}))

// Returns the workspace definition by ID, or undefined if not found.
export function getWorkspace(id: WorkspaceId): WorkspaceDefinition | undefined {
  return WORKSPACES.find((w) => w.id === id)
}
