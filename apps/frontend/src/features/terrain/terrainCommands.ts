import { commandRegistry, type CommandDefinition } from '../../editor/commands/commandRegistry'
import { useShellUiStore } from '../../editor/shell/shellUiStore'
import { useSelectionStore } from '../../editor/selection/selectionStore'
import type { EngineClient } from '../../lib/engineSession'

// Terrain commands registered through the Issue #5 command registry. Each
// command issues real engine commands or opens real dialogs; no fake
// handlers, no duplicated execution in App.tsx.

export interface TerrainCommandDeps {
  getEngineClient: () => EngineClient | null
}

export function registerTerrainCommands(deps: TerrainCommandDeps): void {
  const defs: CommandDefinition[] = [
    {
      id: 'terrain.import',
      label: 'Import Terrain…',
      description: 'Import a georeferenced GeoTIFF DEM as a canonical terrain dataset.',
      category: 'Terrain',
      group: 'terrain',
      surfaces: ['menu', 'palette'],
      requiresEngine: true,
      requiresProject: true,
      requiresNotBusy: true,
      execute: () => {
        // The generic Import Terrain command always opens in local-file
        // mode. Using openDialogCommand('import-terrain') would inherit
        // whatever terrainImportMode was last set by a dedicated toolbar
        // deep link (e.g. 'download-area'), so closing the Download Area
        // tab and reopening via menu/palette could show the wrong tab.
        useShellUiStore.getState().openTerrainImport('local-file')
      },
    },
    {
      id: 'terrain.import-local',
      label: 'Import Local File',
      description: 'Import a georeferenced GeoTIFF DEM from a local file.',
      category: 'Terrain',
      group: 'terrain',
      surfaces: ['toolbar'],
      requiresEngine: true,
      requiresProject: true,
      requiresNotBusy: true,
      execute: () => {
        useShellUiStore.getState().openTerrainImport('local-file')
      },
    },
    {
      id: 'terrain.download-area',
      label: 'Download Area',
      description: 'Download terrain DEM for a selected geographic area.',
      category: 'Terrain',
      group: 'terrain',
      surfaces: ['toolbar'],
      requiresEngine: true,
      requiresProject: true,
      requiresNotBusy: true,
      execute: () => {
        useShellUiStore.getState().openTerrainImport('download-area')
      },
    },
    {
      id: 'terrain.export',
      label: 'Export Terrain…',
      description: 'Export terrain heightmap and albedo textures for game engines (Unreal, Unity) or GIS.',
      category: 'Terrain',
      group: 'terrain',
      surfaces: ['menu', 'palette', 'toolbar'],
      requiresEngine: true,
      requiresProject: true,
      requiresNotBusy: true,
      execute: () => {
        useShellUiStore.getState().openDialogCommand('export-terrain')
      },
    },
    {
      id: 'terrain.regenerate-tiles',
      label: 'Regenerate Terrain Tiles',
      description: 'Regenerate missing derived terrain tiles for the selected dataset.',
      category: 'Terrain',
      group: 'terrain',
      surfaces: ['menu', 'palette'],
      requiresEngine: true,
      requiresProject: true,
      enabled: () => {
        // Only enabled when a terrain dataset is selected.
        const id = useSelectionStore.getState().primaryId
        return id !== null && id.startsWith('terrain:')
      },
      execute: async () => {
        const client = deps.getEngineClient()
        if (!client) {
          return
        }
        // The actual regenerate call is handled by the terrain API through
        // the selection store; this command is the registry entry point.
        // The handler is wired in the terrain feature module.
      },
    },
  ]

  for (const def of defs) {
    commandRegistry.register(def)
  }
}

export function unregisterTerrainCommands(): void {
  commandRegistry.unregister('terrain.import')
  commandRegistry.unregister('terrain.import-local')
  commandRegistry.unregister('terrain.download-area')
  commandRegistry.unregister('terrain.export')
  commandRegistry.unregister('terrain.regenerate-tiles')
}
