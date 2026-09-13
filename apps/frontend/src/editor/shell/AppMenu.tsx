import { useMemo, useState } from 'react'
import { ChevronDown } from 'lucide-react'
import {
  executeCommand,
  resolveCommandAvailability,
  useCommandRegistrySnapshot,
  type CommandContext,
  type CommandDefinition,
} from '../commands/useCommands'

// Application menu. References registered commands by ID; execution goes
// through the central executeCommand path so gating is consistent with
// toolbar/shortcuts. The menu reactively subscribes to the command registry
// via useSyncExternalStore so it updates when commands are registered or
// unregistered after mount.
export function AppMenu({ context }: { context: CommandContext }) {
  const [openCategory, setOpenCategory] = useState<string | null>(null)
  const commands = useCommandRegistrySnapshot()
  const categories = useMemo(() => {
    const seen = new Set<string>()
    for (const command of commands) {
      const surfaces = command.surfaces ?? ['menu']
      if (surfaces.includes('menu')) {
        seen.add(command.category)
      }
    }
    return Array.from(seen)
  }, [commands])

  return (
    <nav className="app-menu" aria-label="Application menu">
      {categories.map((category) => (
        <div className="menu-item" key={category}>
          <button
            className="menu-trigger"
            type="button"
            aria-haspopup="menu"
            aria-expanded={openCategory === category}
            onClick={() => setOpenCategory(openCategory === category ? null : category)}
          >
            {category} <ChevronDown size={11} />
          </button>
          {openCategory === category ? (
            <div className="menu-dropdown" role="menu">
              {commands
                .filter((command) => {
                  const surfaces = command.surfaces ?? ['menu']
                  return command.category === category && surfaces.includes('menu')
                })
                .map((command) => (
                  <MenuEntry
                    key={command.id}
                    command={command}
                    context={context}
                    onClose={() => setOpenCategory(null)}
                  />
                ))}
            </div>
          ) : null}
        </div>
      ))}
    </nav>
  )
}

function MenuEntry({
  command,
  context,
  onClose,
}: {
  command: CommandDefinition
  context: CommandContext
  onClose: () => void
}) {
  const availability = resolveCommandAvailability(command, context)
  return (
    <button
      className="menu-entry"
      type="button"
      role="menuitem"
      disabled={!availability.enabled}
      title={availability.disabledReason ?? command.description}
      onClick={() => {
        if (availability.enabled) {
          // Central execution path — same gating as toolbar/shortcuts.
          void executeCommand(command.id, context)
          onClose()
        }
      }}
    >
      <span>{command.label}</span>
      {command.shortcut ? <span className="menu-shortcut">{command.shortcut.display}</span> : null}
    </button>
  )
}
