import type { LucideIcon } from 'lucide-react'
import {
  Home,
  Globe,
  Mountain,
  Road,
  GitFork,
  TrafficCone,
  Trees,
  Train,
  Film,
  PlayCircle,
} from 'lucide-react'
import type { ReactNode } from 'react'

// Workspace model specification from docs/06_UI_UX/WORKSPACE_MODEL.md.
// Workspaces define presentation configuration, tool groups, Navigator emphasis,
// and Inspector/ContextEditor contributions.
// Workspaces NEVER own canonical project or domain data.

export type WorkspaceId =
  | 'home'
  | 'world'
  | 'terrain'
  | 'roads'
  | 'lanes-junctions'
  | 'infrastructure'
  | 'environment-assets'
  | 'rail'
  | 'scenario'
  | 'simulation'

export interface WorkspaceAvailability {
  // Whether this workspace has a real production path in the current build.
  enabled: boolean
  // Whether this workspace is hidden from normal release UI.
  hidden?: boolean
  // Feature-gated: visible only when developer/feature-flag mode is active.
  featureGated?: boolean
  // Explanation when unavailable.
  disabledReason?: string
}

export interface WorkspaceToolItem {
  commandId: string
  label?: string
  icon?: LucideIcon
  primary?: boolean
  tooltip?: string
}

export interface WorkspaceToolGroup {
  id: string
  label: string
  tools: WorkspaceToolItem[]
  // Specialized styling class (e.g. 'context-toolbar-group--drawing').
  className?: string
  // Dynamic visibility predicate (e.g. shown only while active drawing).
  condition?: () => boolean
}

export type NavigatorTabId = 'scene' | 'layers' | 'assets' | 'sources'

export interface ContextEditorContribution {
  id: string
  label: string
  render: () => ReactNode
}

export interface WorkspaceDefinition {
  id: WorkspaceId
  label: string
  icon: LucideIcon
  availability: WorkspaceAvailability
  toolGroups: WorkspaceToolGroup[]
  defaultNavigatorTab?: NavigatorTabId
  selectableTypes?: readonly string[]
  inspectorContributions?: readonly string[]
  contextEditor?: ContextEditorContribution
  viewportController?: string
  statusHints?: {
    default?: string
    [toolId: string]: string | undefined
  }
}

// Built-in workspace definitions conforming to the workspace matrix
export const BUILTIN_WORKSPACES: readonly WorkspaceDefinition[] = [
  {
    id: 'home',
    label: 'Home',
    icon: Home,
    availability: {
      enabled: true,
      hidden: false,
    },
    toolGroups: [],
    statusHints: {
      default: 'Create or open a project to begin infrastructure authoring',
    },
  },
  {
    id: 'world',
    label: 'World',
    icon: Globe,
    availability: {
      enabled: true,
      hidden: false,
    },
    defaultNavigatorTab: 'sources',
    toolGroups: [
      {
        id: 'world-geo',
        label: 'Project spatial setup',
        tools: [
          {
            commandId: 'project.georeference',
            label: 'Georeference',
            tooltip: 'Open project CRS and origin georeference settings',
          },
        ],
      },
    ],
    statusHints: {
      default: 'Configure project coordinate reference system, origin, and spatial context',
    },
  },
  {
    id: 'terrain',
    label: 'Terrain',
    icon: Mountain,
    availability: {
      enabled: true,
      hidden: false,
    },
    defaultNavigatorTab: 'scene',
    selectableTypes: ['terrain-dataset'],
    toolGroups: [
      {
        id: 'terrain-acquire',
        label: 'Terrain acquisition',
        tools: [
          {
            commandId: 'terrain.import-local',
            label: 'Import',
            tooltip: 'Import a local GeoTIFF DEM file',
          },
          {
            commandId: 'terrain.download-area',
            label: 'Download Area',
            tooltip: 'Download terrain DEM tiles for a selected area',
          },
        ],
      },
      {
        id: 'terrain-manage',
        label: 'Terrain management',
        tools: [
          {
            commandId: 'terrain.export',
            label: 'Export',
            tooltip: 'Export terrain heightmaps and albedo textures',
          },
          {
            commandId: 'project.georeference',
            label: 'Georeference',
            tooltip: 'Open canonical georeference settings',
          },
        ],
      },
    ],
    statusHints: {
      default: 'Acquire, inspect, and manage digital elevation models and terrain coverage',
    },
  },
  {
    id: 'roads',
    label: 'Roads',
    icon: Road,
    availability: {
      enabled: true,
      hidden: false,
    },
    defaultNavigatorTab: 'scene',
    selectableTypes: ['road'],
    toolGroups: [
      {
        id: 'road-drawing',
        label: 'Road drawing mode',
        className: 'context-toolbar-group--drawing',
        tools: [
          {
            commandId: 'road.finish-drawing',
            label: 'Finish',
            tooltip: 'Commit draft road points and fit mathematical alignment',
            primary: true,
          },
          {
            commandId: 'road.cancel-drawing',
            label: 'Cancel',
            tooltip: 'Cancel active road drawing preview',
          },
        ],
      },
      {
        id: 'road-author',
        label: 'Road authoring',
        tools: [
          {
            commandId: 'road.create',
            label: 'Create Road',
            tooltip: 'Create a new road alignment from control points',
          },
          {
            commandId: 'road.delete',
            label: 'Delete',
            tooltip: 'Delete the selected road',
          },
          {
            commandId: 'road.rename',
            label: 'Rename',
            tooltip: 'Rename the selected road',
          },
          {
            commandId: 'road.fit-source',
            label: 'Refit',
            tooltip: 'Refit selected road alignment with current fitting parameters',
          },
        ],
      },
      {
        id: 'road-history',
        label: 'Road edit history',
        tools: [
          {
            commandId: 'road.undo',
            label: 'Undo',
            tooltip: 'Undo last road operation',
          },
          {
            commandId: 'road.redo',
            label: 'Redo',
            tooltip: 'Redo last undone road operation',
          },
        ],
      },
    ],
    statusHints: {
      default: 'Author and edit horizontal alignment and vertical profile geometry',
      'road.drawing': 'Click in viewport to place alignment control points. Finish or Cancel in toolbar.',
      'road.move-control': 'Click in viewport to place selected control point.',
      'road.insert-control': 'Click in viewport to insert new control point.',
    },
  },
  // Future workspaces are gated and hidden in release builds (no fake features)
  {
    id: 'lanes-junctions',
    label: 'Lanes & Junctions',
    icon: GitFork,
    availability: {
      enabled: false,
      hidden: true,
      featureGated: true,
      disabledReason: 'Lane sections and junction topology domain coming in future release',
    },
    toolGroups: [],
  },
  {
    id: 'infrastructure',
    label: 'Infrastructure',
    icon: TrafficCone,
    availability: {
      enabled: false,
      hidden: true,
      featureGated: true,
      disabledReason: 'Roadside signage, signals, and barriers coming in future release',
    },
    toolGroups: [],
  },
  {
    id: 'environment-assets',
    label: 'Environment & Assets',
    icon: Trees,
    availability: {
      enabled: false,
      hidden: true,
      featureGated: true,
      disabledReason: 'Environment and asset catalog coming in future release',
    },
    toolGroups: [],
  },
  {
    id: 'rail',
    label: 'Rail',
    icon: Train,
    availability: {
      enabled: false,
      hidden: true,
      featureGated: true,
      disabledReason: 'Rail track, cant, and switch topology coming in future release',
    },
    toolGroups: [],
  },
  {
    id: 'scenario',
    label: 'Scenario',
    icon: Film,
    availability: {
      enabled: false,
      hidden: true,
      featureGated: true,
      disabledReason: 'Scenario timeline and actor authoring coming in future release',
    },
    toolGroups: [],
  },
  {
    id: 'simulation',
    label: 'Simulation',
    icon: PlayCircle,
    availability: {
      enabled: false,
      hidden: true,
      featureGated: true,
      disabledReason: 'Traffic and sensor simulation runtime coming in future release',
    },
    toolGroups: [],
  },
]

class WorkspaceRegistry {
  private readonly definitions = new Map<WorkspaceId, WorkspaceDefinition>()
  private readonly listeners = new Set<() => void>()

  constructor() {
    this.reset()
  }

  reset = (): void => {
    this.definitions.clear()
    for (const def of BUILTIN_WORKSPACES) {
      this.definitions.set(def.id, def)
    }
    this.notify()
  }

  register = (def: WorkspaceDefinition): void => {
    this.definitions.set(def.id, def)
    this.notify()
  }

  unregister = (id: WorkspaceId): void => {
    if (this.definitions.delete(id)) {
      this.notify()
    }
  }

  get = (id: WorkspaceId): WorkspaceDefinition | undefined => {
    return this.definitions.get(id)
  }

  getAll = (): WorkspaceDefinition[] => {
    return Array.from(this.definitions.values())
  }

  getVisible = (options?: { includeFeatureGated?: boolean }): WorkspaceDefinition[] => {
    return Array.from(this.definitions.values()).filter((w) => {
      if (options?.includeFeatureGated) {
        return true
      }
      return w.availability.enabled && !w.availability.hidden
    })
  }

  resolveValidWorkspace = (id: string | null | undefined, projectOpen: boolean): WorkspaceId => {
    if (id && this.definitions.has(id as WorkspaceId)) {
      const def = this.definitions.get(id as WorkspaceId)!
      if (def.availability.enabled) {
        return def.id
      }
    }
    // Safe fallback:
    return projectOpen ? 'terrain' : 'home'
  }

  subscribe = (listener: () => void): () => void => {
    this.listeners.add(listener)
    return () => {
      this.listeners.delete(listener)
    }
  }

  private notify(): void {
    for (const listener of this.listeners) {
      listener()
    }
  }
}

export const workspaceRegistry = new WorkspaceRegistry()
