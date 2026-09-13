import { useMemo, useState } from 'react'
import { ChevronDown } from 'lucide-react'
import {
  commandRegistry,
  resolveCommandAvailability,
  type CommandContext,
  type CommandDefinition,
} from '../commands/commandRegistry'

// Application menu. References registered commands by ID; execution logic is
// never duplicated here. The menu groups commands by category and renders
// the enabled state from the centralized availability resolver.
export function AppMenu({ context }: { context: CommandContext }) {
  const [openCategory, setOpenCategory] = useState<string | null>(null)
  const categories = useMemo(() => uniqueCategories(), [])
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
              {commandRegistry.byCategory(category).map((command) => (
                <MenuEntry key={command.id} command={command} context={context} onClose={() => setOpenCategory(null)} />
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
          void command.execute(context)
          onClose()
        }
      }}
    >
      <span>{command.label}</span>
      {command.shortcut ? <span className="menu-shortcut">{command.shortcut.display}</span> : null}
    </button>
  )
}

function uniqueCategories(): string[] {
  const seen = new Set<string>()
  for (const command of commandRegistry.all()) {
    seen.add(command.category)
  }
  return Array.from(seen)
}
