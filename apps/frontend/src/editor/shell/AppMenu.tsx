import { useMemo, useRef, useState } from 'react'
import { ChevronDown } from 'lucide-react'
import {
  executeCommand,
  resolveCommandAvailability,
  isCommandVisible,
  commandSurfaces,
  useCommandRegistrySnapshot,
  type CommandContext,
  type CommandDefinition,
} from '../commands/useCommands'

// Application menu. References registered commands by ID; execution goes
// through the central executeCommand path so gating is consistent with
// toolbar/shortcuts. The menu reactively subscribes to the command registry
// via useSyncExternalStore so it updates when commands are registered or
// unregistered after mount. Invisible commands are filtered out via the
// shared isCommandVisible helper so visibility behavior is consistent
// across all surfaces.
//
// Keyboard behavior: ArrowDown/ArrowUp navigate within an open dropdown,
// Escape closes it, Enter activates the focused entry.
export function AppMenu({ context }: { context: CommandContext }) {
  const [openCategory, setOpenCategory] = useState<string | null>(null)
  const [focusIndex, setFocusIndex] = useState(0)
  const commands = useCommandRegistrySnapshot()
  const entryRefs = useRef<Record<string, HTMLButtonElement | null>>({})

  const categories = useMemo(() => {
    const seen = new Set<string>()
    for (const command of commands) {
      if (!commandSurfaces(command).includes('menu')) {
        continue
      }
      if (!isCommandVisible(command, context)) {
        continue
      }
      seen.add(command.category)
    }
    return Array.from(seen)
  }, [commands, context])

  const menuCommands = (category: string) =>
    commands.filter((command) => {
      return (
        command.category === category &&
        commandSurfaces(command).includes('menu') &&
        isCommandVisible(command, context)
      )
    })

  const onMenuKeyDown = (event: React.KeyboardEvent, category: string) => {
    const entries = menuCommands(category)
    if (event.key === 'Escape') {
      event.preventDefault()
      setOpenCategory(null)
      return
    }
    if (event.key === 'ArrowDown' || event.key === 'ArrowUp') {
      event.preventDefault()
      const ids = entries.map((c) => c.id)
      if (ids.length === 0) {
        return
      }
      const direction = event.key === 'ArrowDown' ? 1 : -1
      const nextIndex = (focusIndex + direction + ids.length) % ids.length
      setFocusIndex(nextIndex)
      const nextId = ids[nextIndex]
      if (nextId !== undefined) {
        entryRefs.current[nextId]?.focus()
      }
      return
    }
  }

  const openMenu = (category: string) => {
    setOpenCategory(openCategory === category ? null : category)
    setFocusIndex(0)
  }

  return (
    <nav className="app-menu" aria-label="Application menu">
      {categories.map((category) => (
        <div className="menu-item" key={category}>
          <button
            className="menu-trigger"
            type="button"
            aria-haspopup="menu"
            aria-expanded={openCategory === category}
            onClick={() => openMenu(category)}
          >
            {category} <ChevronDown size={11} />
          </button>
          {openCategory === category ? (
            <div
              className="menu-dropdown"
              role="menu"
              aria-label={`${category} menu`}
              onKeyDown={(e) => onMenuKeyDown(e, category)}
            >
              {menuCommands(category).map((command, index) => (
                <MenuEntry
                  key={command.id}
                  command={command}
                  context={context}
                  entryRef={(el) => { entryRefs.current[command.id] = el }}
                  onFocused={() => setFocusIndex(index)}
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
  entryRef,
  onFocused,
  onClose,
}: {
  command: CommandDefinition
  context: CommandContext
  entryRef: (el: HTMLButtonElement | null) => void
  onFocused: () => void
  onClose: () => void
}) {
  const availability = resolveCommandAvailability(command, context)
  return (
    <button
      ref={entryRef}
      className="menu-entry"
      type="button"
      role="menuitem"
      data-command-id={command.id}
      disabled={!availability.enabled}
      title={availability.disabledReason ?? command.description}
      onFocus={onFocused}
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
