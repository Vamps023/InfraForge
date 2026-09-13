import { evaluateAvailability, type AvailabilityContext, type CommandAvailability } from '../availability'

// Central command registry — the single source of command definitions for
// application menus, toolbars, keyboard shortcuts, and future command
// palette. Each command has a stable ID and metadata; execution logic lives
// in one place so menus, toolbars, and shortcuts never duplicate handlers
// (ADR-0009: the frontend issues commands to the canonical engine owner).
//
// The registry is observable: UI surfaces subscribe via `subscribe`/
// `getSnapshot` (compatible with useSyncExternalStore) and re-render
// automatically when commands are registered or unregistered — no stale
// useMemo([]) caches or per-component force-render hacks.

export type CommandId = string
export type CommandCategory = string
export type CommandGroup = string

// UI surfaces where a command may appear. A command can appear on multiple
// surfaces (e.g. menu + toolbar + shortcut). `category`/`group` remain for
// logical organization; `surfaces` controls placement so grouping is never
// misused as UI placement.
export type CommandSurface = 'menu' | 'toolbar' | 'shortcut' | 'palette'

export interface CommandShortcut {
  // Logical key, e.g. 's', 'o', 'n'. Case-insensitive; modifiers carry the
  // distinction between shifted and unshifted.
  key: string
  ctrlOrCmd?: boolean
  shift?: boolean
  alt?: boolean
  // Platform-aware display label, e.g. "Ctrl+S" on Windows/Linux or
  // "⌘S" on macOS. Computed from the metadata via shortcutDisplayLabel().
  display: string
}

export interface CommandContext {
  availability: AvailabilityContext
}

export interface CommandDefinition {
  id: CommandId
  label: string
  description?: string
  category: CommandCategory
  group?: CommandGroup
  // UI surfaces where this command appears. Defaults to ['menu'].
  surfaces?: CommandSurface[]
  shortcut?: CommandShortcut
  // Engine availability requirement. Defaults to true.
  requiresEngine?: boolean
  // Project availability requirement. Defaults to false.
  requiresProject?: boolean
  // Viewport active requirement. Defaults to false.
  requiresViewport?: boolean
  // When true, the command is also gated when a project operation is busy
  // (project === 'busy'), even if requiresProject is false. This prevents
  // conflicting lifecycle operations (e.g. New/Open while Save is running).
  // Defaults to false for non-project commands, true for project lifecycle
  // commands that mutate project state.
  requiresNotBusy?: boolean
  // Execute handler. Receives the resolved availability context so handlers
  // can branch on engine/project state if needed. Handlers must not assume
  // the command is enabled — the registry gates execution, but a handler
  // may still be invoked programmatically and must guard accordingly.
  execute: (context: CommandContext) => void | Promise<void>
  // Optional enabled predicate for context-sensitive gating beyond the
  // declared requirements (e.g. selection presence).
  enabled?: (context: CommandContext) => boolean
  // Optional visible predicate. Use sparingly; prefer enabled for commands
  // that should remain visible but disabled.
  visible?: (context: CommandContext) => boolean
}

// --- Shortcut helpers ---

export interface ShortcutKey {
  key: string
  ctrlOrCmd: boolean
  shift: boolean
  alt: boolean
}

export function shortcutKey(shortcut: CommandShortcut): ShortcutKey {
  return {
    key: shortcut.key.toLowerCase(),
    ctrlOrCmd: shortcut.ctrlOrCmd ?? false,
    shift: shortcut.shift ?? false,
    alt: shortcut.alt ?? false,
  }
}

function shortcutKeyEqual(a: ShortcutKey, b: ShortcutKey): boolean {
  return (
    a.key === b.key &&
    a.ctrlOrCmd === b.ctrlOrCmd &&
    a.shift === b.shift &&
    a.alt === b.alt
  )
}

// Platform-aware display label for a shortcut. Uses ⌘ on macOS, Ctrl
// elsewhere. The `display` field on the definition is the authoritative
// label (set at registration time via shortcutDisplayLabel), but this
// helper is exported for tests and future palette UI.
export function shortcutDisplayLabel(shortcut: Omit<CommandShortcut, 'display'>): string {
  const mod = shortcut.ctrlOrCmd ? (isMac() ? '⌘' : 'Ctrl+') : ''
  const shift = shortcut.shift ? (isMac() ? '⇧' : 'Shift+') : ''
  const alt = shortcut.alt ? (isMac() ? '⌥' : 'Alt+') : ''
  const key = shortcut.key.toUpperCase()
  return `${mod}${shift}${alt}${key}`
}

function isMac(): boolean {
  if (typeof navigator === 'undefined') {
    return false
  }
  return navigator.platform.toLowerCase().includes('mac')
}

// --- Observable registry ---

const commands = new Map<CommandId, CommandDefinition>()
const listeners = new Set<() => void>()
let snapshot: CommandDefinition[] | null = null

function notify(): void {
  snapshot = null
  for (const listener of listeners) {
    listener()
  }
}

export interface CommandRegistry {
  register: (command: CommandDefinition) => void
  unregister: (commandId: CommandId) => void
  get: (commandId: CommandId) => CommandDefinition | undefined
  all: () => CommandDefinition[]
  byCategory: (category: CommandCategory) => CommandDefinition[]
  bySurface: (surface: CommandSurface) => CommandDefinition[]
  subscribe: (listener: () => void) => () => void
  getSnapshot: () => CommandDefinition[]
}

export const commandRegistry: CommandRegistry = {
  register(command) {
    if (commands.has(command.id)) {
      throw new Error(`Duplicate command id: ${command.id}`)
    }
    // Shortcut conflict detection: reject two commands claiming the same
    // key+modifier combination. This is a deterministic error rather than
    // silently executing whichever was registered first.
    if (command.shortcut) {
      const newKey = shortcutKey(command.shortcut)
      for (const existing of commands.values()) {
        if (existing.shortcut && shortcutKeyEqual(shortcutKey(existing.shortcut), newKey)) {
          throw new Error(
            `Shortcut conflict: command '${command.id}' and '${existing.id}' both claim ${command.shortcut.display}`,
          )
        }
      }
    }
    commands.set(command.id, command)
    notify()
  },
  unregister(commandId) {
    if (commands.delete(commandId)) {
      notify()
    }
  },
  get(commandId) {
    return commands.get(commandId)
  },
  all() {
    return Array.from(commands.values())
  },
  byCategory(category) {
    return commandRegistry.all().filter((command) => command.category === category)
  },
  bySurface(surface) {
    return commandRegistry.all().filter((command) => {
      const surfaces = command.surfaces ?? ['menu']
      return surfaces.includes(surface)
    })
  },
  subscribe(listener) {
    listeners.add(listener)
    return () => {
      listeners.delete(listener)
    }
  },
  getSnapshot() {
    if (snapshot === null) {
      snapshot = Array.from(commands.values())
    }
    return snapshot
  },
}

// Evaluates the effective availability of a command, combining its declared
// requirements with its optional enabled predicate. Used by menus, toolbars,
// and shortcuts to render consistent enabled state without re-implementing
// gating.
export function resolveCommandAvailability(
  command: CommandDefinition,
  context: CommandContext,
): CommandAvailability {
  const requirements = {
    requiresEngine: command.requiresEngine,
    requiresProject: command.requiresProject,
    requiresViewport: command.requiresViewport,
    requiresNotBusy: command.requiresNotBusy,
  }
  const base = evaluateAvailability(context.availability, requirements)
  if (!base.enabled) {
    return base
  }
  if (command.enabled && !command.enabled(context)) {
    return { enabled: false, disabledReason: 'Command is not available in this context.' }
  }
  return base
}

// Executes a command by ID after gating. Returns false if the command was
// not found or was gated off (so callers can surface a no-op honestly
// rather than silently swallowing the invocation). This is the single
// execution path — menu, toolbar, shortcuts, and future palette all call
// this so availability checks and execution behavior never drift between
// surfaces.
export async function executeCommand(
  commandId: CommandId,
  context: CommandContext,
): Promise<boolean> {
  const command = commands.get(commandId)
  if (!command) {
    return false
  }
  const availability = resolveCommandAvailability(command, context)
  if (!availability.enabled) {
    return false
  }
  await command.execute(context)
  return true
}
