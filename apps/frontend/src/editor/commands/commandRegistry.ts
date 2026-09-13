import { evaluateAvailability, type AvailabilityContext, type CommandAvailability } from '../availability'

// Central command registry — the single source of command definitions for
// application menus, toolbars, keyboard shortcuts, and future command
// palette. Each command has a stable ID and metadata; execution logic lives
// in one place so menus, toolbars, and shortcuts never duplicate handlers
// (ADR-0009: the frontend issues commands to the canonical engine owner).
//
// Commands declare availability requirements (engine/project/viewport) that
// the registry evaluates centrally; components do not re-derive gating.

export type CommandId = string

export type CommandCategory = string
export type CommandGroup = string

export interface CommandShortcut {
  // Logical key, e.g. 's', 'o', 'n'. Case-insensitive; modifiers carry the
  // distinction between shifted and unshifted.
  key: string
  ctrlOrCmd?: boolean
  shift?: boolean
  alt?: boolean
  // Display label, e.g. "Ctrl+S". Computed from the metadata above.
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
  shortcut?: CommandShortcut
  // Engine availability requirement. Defaults to true.
  requiresEngine?: boolean
  // Project availability requirement. Defaults to false.
  requiresProject?: boolean
  // Viewport active requirement. Defaults to false.
  requiresViewport?: boolean
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

interface CommandRegistryState {
  commands: Map<CommandId, CommandDefinition>
  register: (command: CommandDefinition) => void
  unregister: (commandId: CommandId) => void
  get: (commandId: CommandId) => CommandDefinition | undefined
  all: () => CommandDefinition[]
  byCategory: (category: CommandCategory) => CommandDefinition[]
}

const registry: CommandRegistryState = {
  commands: new Map<CommandId, CommandDefinition>(),
  register(command) {
    if (registry.commands.has(command.id)) {
      throw new Error(`Duplicate command id: ${command.id}`)
    }
    registry.commands.set(command.id, command)
  },
  unregister(commandId) {
    registry.commands.delete(commandId)
  },
  get(commandId) {
    return registry.commands.get(commandId)
  },
  all() {
    return Array.from(registry.commands.values())
  },
  byCategory(category) {
    return registry.all().filter((command) => command.category === category)
  },
}

export const commandRegistry = registry

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
// rather than silently swallowing the invocation).
export async function executeCommand(
  commandId: CommandId,
  context: CommandContext,
): Promise<boolean> {
  const command = registry.get(commandId)
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


