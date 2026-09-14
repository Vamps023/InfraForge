import {
  commandRegistry,
  shortcutDisplayLabel,
  type CommandDefinition,
} from './commandRegistry'
import { useShellUiStore } from '../shell/shellUiStore'
import { useProjectStore } from '../../features/project/projectStore'
import { useLayoutStore } from '../layout/layoutStore'
import { closeProject, openProject, saveProject } from '../../features/project/projectApi'
import type { EngineClient } from '../../lib/engineSession'

// Builtin commands that migrate the existing real project operations onto the
// command registry. No command here invents a domain action; each issues a
// real engine command or toggles presentation state (dialogs/panels).
//
// The engine client is supplied by the shell at registration time so handlers
// close over the live session without the registry holding global state.

export interface BuiltinCommandDeps {
  getEngineClient: () => EngineClient | null
}

function shortcut(key: string) {
  const base = { key, ctrlOrCmd: true }
  return { ...base, display: shortcutDisplayLabel(base) }
}

function shiftShortcut(key: string) {
  const base = { key, ctrlOrCmd: true, shift: true }
  return { ...base, display: shortcutDisplayLabel(base) }
}

export function registerBuiltinCommands(deps: BuiltinCommandDeps): void {
  const defs: CommandDefinition[] = [
    {
      id: 'command-palette.open',
      label: 'Command Palette…',
      description: 'Open the command palette to search and run any command.',
      category: 'View',
      group: 'palette',
      surfaces: ['menu', 'shortcut'],
      shortcut: shiftShortcut('p'),
      execute: () => {
        useShellUiStore.getState().openDialogCommand('command-palette')
      },
    },
    {
      id: 'project.new',
      label: 'New Project…',
      description: 'Create a new InfraForge project.',
      category: 'Project',
      group: 'project',
      surfaces: ['menu', 'toolbar', 'shortcut', 'palette'],
      shortcut: shortcut('n'),
      requiresEngine: true,
      // New Project must not start while another lifecycle operation is
      // running — the project API is not designed to serialize concurrent
      // create/open/save/close operations.
      requiresNotBusy: true,
      execute: () => {
        useShellUiStore.getState().openDialogCommand('new-project')
      },
    },
    {
      id: 'project.open',
      label: 'Open Project…',
      description: 'Open an existing InfraForge project directory.',
      category: 'Project',
      group: 'project',
      surfaces: ['menu', 'toolbar', 'shortcut', 'palette'],
      shortcut: shortcut('o'),
      requiresEngine: true,
      requiresNotBusy: true,
      execute: async () => {
        const client = deps.getEngineClient()
        if (!client) {
          return
        }
        const desktop = window.infraforgeDesktop
        if (!desktop?.pickDirectory) {
          useProjectStore.getState().setLastError({
            code: '4',
            message: 'The desktop shell did not expose a directory picker.',
          })
          return
        }
        const selected = await desktop.pickDirectory({
          title: 'Open an InfraForge project directory',
          buttonLabel: 'Open Project',
        })
        if (!selected) {
          return
        }
        await openProject(client, selected).catch(() => undefined)
      },
    },
    {
      id: 'project.save',
      label: 'Save Project',
      description: 'Save the open project to its database.',
      category: 'Project',
      group: 'project',
      surfaces: ['menu', 'toolbar', 'shortcut', 'palette'],
      shortcut: shortcut('s'),
      requiresEngine: true,
      requiresProject: true,
      execute: async () => {
        const client = deps.getEngineClient()
        if (!client) {
          return
        }
        await saveProject(client).catch(() => undefined)
      },
    },
    {
      id: 'project.close',
      label: 'Close Project',
      description: 'Close the open project session.',
      category: 'Project',
      group: 'project',
      surfaces: ['menu', 'shortcut', 'palette'],
      requiresEngine: true,
      requiresProject: true,
      execute: async () => {
        const client = deps.getEngineClient()
        if (!client) {
          return
        }
        await closeProject(client).catch(() => undefined)
      },
    },
    {
      id: 'project.georeference',
      label: 'Georeference…',
      description: 'Open the canonical georeference settings panel.',
      category: 'Project',
      group: 'project',
      surfaces: ['menu', 'palette'],
      requiresEngine: true,
      requiresProject: true,
      execute: () => {
        useShellUiStore.getState().openDialogCommand('georeference')
      },
    },
    {
      id: 'panel.toggle-outliner',
      label: 'Toggle Outliner',
      description: 'Show or hide the outliner panel.',
      category: 'View',
      group: 'panels',
      surfaces: ['menu', 'palette'],
      execute: () => {
        const store = useLayoutStore.getState()
        store.setPanelVisible('left', !store.panels.left.visible)
      },
    },
    {
      id: 'panel.toggle-inspector',
      label: 'Toggle Inspector',
      description: 'Show or hide the inspector panel.',
      category: 'View',
      group: 'panels',
      surfaces: ['menu', 'palette'],
      execute: () => {
        const store = useLayoutStore.getState()
        store.setPanelVisible('right', !store.panels.right.visible)
      },
    },
    {
      id: 'panel.toggle-bottom',
      label: 'Toggle Bottom Panel',
      description: 'Show or hide the bottom panel.',
      category: 'View',
      group: 'panels',
      surfaces: ['menu', 'palette'],
      execute: () => {
        const store = useLayoutStore.getState()
        store.setPanelVisible('bottom', !store.panels.bottom.visible)
      },
    },
    {
      id: 'help.diagnostics',
      label: 'Diagnostics…',
      description: 'Show build version, resolved native paths, and runtime info.',
      category: 'Help',
      group: 'help',
      surfaces: ['menu', 'palette'],
      execute: () => {
        useShellUiStore.getState().openDialogCommand('diagnostics')
      },
    },
  ]

  for (const def of defs) {
    commandRegistry.register(def)
  }
}

export function unregisterBuiltinCommands(): void {
  const ids = [
    'command-palette.open',
    'project.new',
    'project.open',
    'project.save',
    'project.close',
    'project.georeference',
    'panel.toggle-outliner',
    'panel.toggle-inspector',
    'panel.toggle-bottom',
    'help.diagnostics',
  ]
  for (const id of ids) {
    commandRegistry.unregister(id)
  }
}
