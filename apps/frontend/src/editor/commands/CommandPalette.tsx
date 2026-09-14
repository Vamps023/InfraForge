import { useEffect, useMemo, useRef, useState } from 'react'
import { Search } from 'lucide-react'
import {
  executeCommand,
  resolveCommandAvailability,
  isCommandVisible,
  commandSurfaces,
  useCommandRegistrySnapshot,
  type CommandContext,
  type CommandDefinition,
} from './useCommands'

// Command palette. Uses the existing central command registry — no duplicate
// command implementation. Only commands whose `surfaces` include 'palette'
// are listed. Invisible commands (visible() === false) are excluded. Disabled
// commands remain visibly disabled. Execution goes through executeCommand so
// gating is consistent with menu/toolbar/shortcuts.
//
// Keyboard navigation:
//   ArrowUp / ArrowDown changes the active result
//   Enter executes the enabled active result
//   Escape closes the palette
//
// The palette is integrated with the shell's blocking overlay system
// (shellUiStore) so the native viewport child HWND is hidden/restored
// through the same visibility policy as other blocking overlays.
export interface CommandPaletteProps {
  context: CommandContext
  onClose: () => void
}

export function CommandPalette({ context, onClose }: CommandPaletteProps) {
  const commands = useCommandRegistrySnapshot()
  const [query, setQuery] = useState('')
  const [activeIndex, setActiveIndex] = useState(0)
  const inputRef = useRef<HTMLInputElement | null>(null)

  // Filter to palette-surface commands that are visible in the current
  // context, then apply the search query.
  const filtered = useMemo(() => {
    const lower = query.trim().toLowerCase()
    return commands.filter((command) => {
      if (!commandSurfaces(command).includes('palette')) {
        return false
      }
      if (!isCommandVisible(command, context)) {
        return false
      }
      if (lower === '') {
        return true
      }
      const label = command.label.toLowerCase()
      const description = (command.description ?? '').toLowerCase()
      const category = command.category.toLowerCase()
      return (
        label.includes(lower) ||
        description.includes(lower) ||
        category.includes(lower)
      )
    })
  }, [commands, query, context])

  // Clamp activeIndex when the filtered list changes.
  useEffect(() => {
    setActiveIndex((prev) => Math.min(prev, Math.max(0, filtered.length - 1)))
  }, [filtered.length])

  // Focus the search field on open.
  useEffect(() => {
    inputRef.current?.focus()
  }, [])

  const onKeyDown = (event: React.KeyboardEvent) => {
    if (event.key === 'Escape') {
      event.preventDefault()
      onClose()
      return
    }
    if (event.key === 'ArrowDown') {
      event.preventDefault()
      setActiveIndex((prev) => Math.min(prev + 1, filtered.length - 1))
      return
    }
    if (event.key === 'ArrowUp') {
      event.preventDefault()
      setActiveIndex((prev) => Math.max(prev - 1, 0))
      return
    }
    if (event.key === 'Enter') {
      event.preventDefault()
      const command = filtered[activeIndex]
      if (!command) {
        return
      }
      const availability = resolveCommandAvailability(command, context)
      if (!availability.enabled) {
        return
      }
      // Central execution path — same gating as menu/toolbar/shortcuts.
      void executeCommand(command.id, context)
      onClose()
      return
    }
  }

  return (
    <div className="command-palette-overlay" role="dialog" aria-label="Command palette" onClick={onClose}>
      <div className="command-palette" onClick={(e) => e.stopPropagation()} onKeyDown={onKeyDown}>
        <div className="command-palette-search">
          <Search size={14} />
          <input
            ref={inputRef}
            aria-label="Search commands"
            placeholder="Type a command…"
            value={query}
            onChange={(e) => {
              setQuery(e.target.value)
              setActiveIndex(0)
            }}
          />
        </div>
        {filtered.length === 0 ? (
          <div className="command-palette-empty">No matching commands.</div>
        ) : (
          <ul className="command-palette-list" role="listbox">
            {filtered.map((command, index) => (
              <PaletteEntry
                key={command.id}
                command={command}
                context={context}
                active={index === activeIndex}
                onHover={() => setActiveIndex(index)}
                onExecute={() => {
                  const availability = resolveCommandAvailability(command, context)
                  if (availability.enabled) {
                    void executeCommand(command.id, context)
                    onClose()
                  }
                }}
              />
            ))}
          </ul>
        )}
      </div>
    </div>
  )
}

function PaletteEntry({
  command,
  context,
  active,
  onHover,
  onExecute,
}: {
  command: CommandDefinition
  context: CommandContext
  active: boolean
  onHover: () => void
  onExecute: () => void
}) {
  const availability = resolveCommandAvailability(command, context)
  return (
    <li
      role="option"
      aria-selected={active}
      aria-disabled={!availability.enabled}
      className={`command-palette-entry${active ? ' active' : ''}${!availability.enabled ? ' disabled' : ''}`}
      onMouseMove={onHover}
      onClick={onExecute}
      title={availability.disabledReason ?? command.description}
    >
      <span className="command-palette-label">{command.label}</span>
      <span className="command-palette-category">{command.category}</span>
      {command.shortcut ? (
        <span className="command-palette-shortcut">{command.shortcut.display}</span>
      ) : null}
    </li>
  )
}
