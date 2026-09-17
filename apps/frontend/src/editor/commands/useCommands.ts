import { useCallback, useEffect, useMemo, useSyncExternalStore } from 'react'
import {
  commandRegistry,
  executeCommand,
  resolveCommandAvailability,
  isCommandVisible,
  commandSurfaces,
  shortcutKey,
  type CommandDefinition,
  type CommandId,
  type CommandContext,
  type CommandSurface,
} from './commandRegistry'
export {
  commandRegistry,
  executeCommand,
  resolveCommandAvailability,
  isCommandVisible,
  commandSurfaces,
  shortcutKey,
  type CommandDefinition,
  type CommandId,
  type CommandContext,
  type CommandSurface,
}
import { deriveAvailability } from '../availability'
import { useProjectStore } from '../../features/project/projectStore'
import { useViewportStore } from '../../features/viewport/viewportStore'
import { useToolStore } from '../tools/toolStore'
import { useSelectionStore } from '../selection/selectionStore'
import type { EngineSession, EngineSessionStatus } from '../../lib/engineSession'

// Subscribes to the engine/project/viewport projections reactively (via
// Zustand selectors) and builds the live CommandContext for the registry.
// All reactive values are read through hooks so the context updates
// immediately on any state change — no hidden getState() reads that can go
// stale between renders.
export function useCommandContext(
  engineStatus: EngineSessionStatus,
  engineSession: EngineSession | null,
): CommandContext {
  // Reactive subscriptions to the stores that drive availability.
  const summary = useProjectStore((state) => state.summary)
  const operation = useProjectStore((state) => state.operation)
  const viewportState = useViewportStore((state) => state.status.state)

  return useMemo(
    () => ({
      availability: deriveAvailability(
        engineStatus,
        engineSession,
        summary,
        operation,
        viewportState,
      ),
    }),
    [engineStatus, engineSession, summary, operation, viewportState],
  )
}

// Reactively subscribes to the command registry snapshot. Re-renders when
// commands are registered or unregistered. Uses useSyncExternalStore so
// the snapshot is stable between mutations (no infinite render loops).
export function useCommandRegistrySnapshot(): CommandDefinition[] {
  return useSyncExternalStore(commandRegistry.subscribe, commandRegistry.getSnapshot)
}

// Returns a bound executor for a command ID. Always goes through the central
// executeCommand path so availability checks and execution behavior are
// consistent across all surfaces.
export function useCommandExecutor(
  commandId: CommandId,
  context: CommandContext,
): {
  run: () => Promise<boolean>
  command: CommandDefinition | undefined
  availability: ReturnType<typeof resolveCommandAvailability>
} {
  const command = commandRegistry.get(commandId)
  const availability = command ? resolveCommandAvailability(command, context) : { enabled: false }
  const run = useCallback(async () => executeCommand(commandId, context), [commandId, context])
  return { run, command, availability }
}

// Global keyboard shortcut handler. Listens once at the shell root and
// dispatches to the registry by matching shortcut metadata. Only commands
// whose effective surfaces include 'shortcut' participate in global
// keyboard dispatch — the `surfaces` contract is authoritative. Text-input
// contexts are skipped so editor fields keep their normal key behavior.
// Invisible commands are not dispatched. Execution goes through the central
// executeCommand path so gating is consistent with menu/toolbar.
export function useCommandShortcuts(context: CommandContext): void {
  const commands = useCommandRegistrySnapshot()
  useEffect(() => {
    const handler = (event: KeyboardEvent) => {
      const target = event.target
      if (
        target instanceof HTMLElement &&
        (target.tagName === 'INPUT' ||
          target.tagName === 'TEXTAREA' ||
          target.tagName === 'SELECT' ||
          target.isContentEditable)
      ) {
        return
      }

      // Shared interaction invariant:
      // First Esc cancels active preview/tool transaction if one exists.
      // Second Esc clears selection when appropriate.
      if (event.key === 'Escape' && !event.ctrlKey && !event.metaKey && !event.shiftKey && !event.altKey) {
        const cancelled = useToolStore.getState().cancelActiveTool()
        if (cancelled) {
          event.preventDefault()
          return
        }
        if (useSelectionStore.getState().selectedIds.length > 0) {
          useSelectionStore.getState().clear()
          event.preventDefault()
          return
        }
      }

      for (const command of commands) {
        const shortcut = command.shortcut
        if (!shortcut) {
          continue
        }
        // Only commands whose surfaces include 'shortcut' participate in
        // global keyboard dispatch.
        if (!commandSurfaces(command).includes('shortcut')) {
          continue
        }
        // Invisible commands are not dispatched via shortcut.
        if (!isCommandVisible(command, context)) {
          continue
        }
        const meta = shortcutKey(shortcut)
        if (meta.ctrlOrCmd ? !(event.ctrlKey || event.metaKey) : (event.ctrlKey || event.metaKey)) {
          continue
        }
        if (meta.shift !== event.shiftKey) continue
        if (meta.alt !== event.altKey) continue
        if (event.key.toLowerCase() !== meta.key) continue
        // Central execution path — same gating as menu/toolbar.
        void executeCommand(command.id, context)
        event.preventDefault()
        return
      }
    }
    window.addEventListener('keydown', handler)
    return () => window.removeEventListener('keydown', handler)
  }, [commands, context])
}
