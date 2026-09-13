import {
  commandRegistry,
  resolveCommandAvailability,
  type CommandContext,
} from '../commands/commandRegistry'

// Toolbar. References registered commands by ID; execution logic is never
// duplicated here. Only commands tagged with the 'toolbar' group render here
// (so the toolbar stays curated while the menu/palette can show everything).
export function Toolbar({ context }: { context: CommandContext }) {
  const toolbarCommands = commandRegistry
    .all()
    .filter((command) => command.group === 'toolbar')
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
                void command.execute(context)
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
