import { useCallback, useEffect, useMemo, useState } from 'react'
import {
  commandRegistry,
  executeCommand,
  resolveCommandAvailability,
  type CommandDefinition,
  type CommandId,
  type CommandContext,
} from './commandRegistry'
import {
  deriveAvailability,
  type AvailabilityContext,
} from '../availability'
import type { EngineSession, EngineSessionStatus } from '../../lib/engineSession'

// Subscribes to the engine/project/viewport projections and builds the live
// CommandContext for the registry. Components call useCommandContext once
// (the shell provides it) and pass it down; commands are never executed
// without a fresh context.
export function useCommandContext(
  engineStatus: EngineSessionStatus,
  engineSession: EngineSession | null,
): CommandContext {
  const [availability, setAvailability] = useState<AvailabilityContext>(() =>
    deriveAvailability(engineStatus, engineSession),
  )

  // Re-derive when the engine status or session reference changes, or when
  // the project/viewport stores change. The stores are zustand; we poll them
  // on the same tick the shell re-renders by subscribing through the
  // useProjectStore/useViewportStore hooks in the shell, which trigger
  // re-renders that re-invoke this hook.
  useEffect(() => {
    setAvailability(deriveAvailability(engineStatus, engineSession))
  }, [engineStatus, engineSession])

  return useMemo(() => ({ availability }), [availability])
}

// Returns a bound executor for a command ID. Returns null if the command is
// not registered. The executor gates on availability before invoking.
export function useCommandExecutor(
  commandId: CommandId,
  context: CommandContext,
): { run: () => Promise<boolean>; command: CommandDefinition | undefined; availability: ReturnType<typeof resolveCommandAvailability> } {
  const command = commandRegistry.get(commandId)
  const availability = command ? resolveCommandAvailability(command, context) : { enabled: false }
  const run = useCallback(async () => executeCommand(commandId, context), [commandId, context])
  return { run, command, availability }
}

// Global keyboard shortcut handler. Listens once at the shell root and
// dispatches to the registry by matching shortcut metadata. Text-input
// contexts are skipped so editor fields keep their normal key behavior.
export function useCommandShortcuts(context: CommandContext): void {
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
      const commands = commandRegistry.all()
      for (const command of commands) {
        const shortcut = command.shortcut
        if (!shortcut) {
          continue
        }
        const ctrlOrCmd = shortcut.ctrlOrCmd ? (event.ctrlKey || event.metaKey) : !(event.ctrlKey || event.metaKey)
        if (!ctrlOrCmd) continue
        if (shortcut.shift !== event.shiftKey) continue
        if (shortcut.alt !== event.altKey) continue
        if (event.key.toLowerCase() !== shortcut.key.toLowerCase()) continue
        const availability = resolveCommandAvailability(command, context)
        if (!availability.enabled) continue
        event.preventDefault()
        void command.execute(context)
        return
      }
    }
    window.addEventListener('keydown', handler)
    return () => window.removeEventListener('keydown', handler)
  }, [context])
}
