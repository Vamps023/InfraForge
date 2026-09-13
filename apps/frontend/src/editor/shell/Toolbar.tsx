import {
  executeCommand,
  resolveCommandAvailability,
  useCommandRegistrySnapshot,
  type CommandContext,
} from '../commands/useCommands'

// Toolbar. References registered commands by ID; execution goes through the
// central executeCommand path so gating is consistent with menu/shortcuts.
// Only commands whose `surfaces` include 'toolbar' render here. `surfaces`
// is the explicit placement concept; `category`/`group` remain for logical
// organization and are never misused as UI placement.
export function Toolbar({ context }: { context: CommandContext }) {
  const commands = useCommandRegistrySnapshot()
  const toolbarCommands = commands.filter((command) => {
    const surfaces = command.surfaces ?? ['menu']
    return surfaces.includes('toolbar')
  })

  return (
    <div className="toolbar" aria-label="Editor toolbar">
      {toolbarCommands.map((command) => {
        const availability = resolveCommandAvailability(command, context)
        return (
          <button
            key={command.id}
            className="tool-button"
            type="button"
            disabled={!availability.enabled}
            title={availability.disabledReason ?? command.description ?? command.label}
            onClick={() => {
              if (availability.enabled) {
                // Central execution path — same gating as menu/shortcuts.
                void executeCommand(command.id, context)
              }
            }}
          >
            {command.label}
          </button>
        )
      })}
      <div className="toolbar-spacer" />
      <span className="toolbar-hint">Authoring tools appear only when their production domain is available.</span>
    </div>
  )
}
