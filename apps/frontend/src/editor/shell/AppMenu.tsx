import { useEffect, useMemo, useRef, useState } from 'react'
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
import { useShellUiStore } from './shellUiStore'

// Application menu. References registered commands by ID; execution goes
// through the central executeCommand path so gating is consistent with
// toolbar/shortcuts. The menu reactively subscribes to the command registry
// via useSyncExternalStore so it updates when commands are registered or
// unregistered after mount. Invisible commands are filtered out via the
// shared isCommandVisible helper so visibility behavior is consistent
// across all surfaces.
//
// Keyboard behavior (menu-button pattern):
//   - Enter/Space on a trigger opens the dropdown and focuses the first
//     focusable menu item.
//   - ArrowDown on a trigger opens the dropdown and focuses the first
//     focusable item.
//   - ArrowUp on a trigger opens the dropdown and focuses the last focusable
//     item.
//   - ArrowDown/ArrowUp within an open dropdown moves focus between items,
//     skipping aria-disabled entries (they remain focusable but are not
//     activation targets).
//   - Escape from an open dropdown closes it and restores focus to the
//     trigger button.
//   - Disabled commands use aria-disabled (not native disabled) so they
//     remain keyboard-focusable for navigation but cannot be activated.

type PendingFocus = 'first' | 'last' | null

export function AppMenu({ context }: { context: CommandContext }) {
  const [openCategory, setOpenCategory] = useState<string | null>(null)
  const [focusIndex, setFocusIndex] = useState(0)
  // When the menu opens, this drives a useEffect to focus the first/last
  // item after the dropdown renders. This is more reliable than
  // requestAnimationFrame in test environments.
  const [pendingFocus, setPendingFocus] = useState<PendingFocus>(null)
  const commands = useCommandRegistrySnapshot()
  const entryRefs = useRef<Record<string, HTMLButtonElement | null>>({})
  const triggerRefs = useRef<Record<string, HTMLButtonElement | null>>({})

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

  // Focus the menu item at the given index within the open category.
  const focusMenuItem = (category: string, index: number) => {
    const entries = menuCommands(category)
    if (entries.length === 0) return
    const clamped = ((index % entries.length) + entries.length) % entries.length
    setFocusIndex(clamped)
    const id = entries[clamped]?.id
    if (id !== undefined) {
      entryRefs.current[id]?.focus()
    }
  }

  // Find the next/previous focusable (non-aria-disabled) index, wrapping.
  const nextFocusableIndex = (category: string, from: number, direction: 1 | -1): number => {
    const entries = menuCommands(category)
    if (entries.length === 0) return from
    let idx = from
    for (let i = 0; i < entries.length; i++) {
      idx = ((idx + direction) % entries.length + entries.length) % entries.length
      const availability = resolveCommandAvailability(entries[idx]!, context)
      if (availability.enabled) return idx
    }
    return from
  }

  const onTriggerKeyDown = (event: React.KeyboardEvent, category: string) => {
    if (event.key === 'ArrowDown' || event.key === 'Enter' || event.key === ' ') {
      event.preventDefault()
      setOpenCategory(category)
      setFocusIndex(0)
      setPendingFocus('first')
      return
    }
    if (event.key === 'ArrowUp') {
      event.preventDefault()
      setOpenCategory(category)
      const entries = menuCommands(category)
      setFocusIndex(entries.length > 0 ? entries.length - 1 : 0)
      setPendingFocus('last')
      return
    }
  }

  const onMenuKeyDown = (event: React.KeyboardEvent, category: string) => {
    const entries = menuCommands(category)
    if (event.key === 'Escape') {
      event.preventDefault()
      setOpenCategory(null)
      setPendingFocus(null)
      triggerRefs.current[category]?.focus()
      return
    }
    if (event.key === 'ArrowDown' || event.key === 'ArrowUp') {
      event.preventDefault()
      if (entries.length === 0) return
      const direction = event.key === 'ArrowDown' ? 1 : -1
      const next = nextFocusableIndex(category, focusIndex, direction)
      setFocusIndex(next)
      const id = entries[next]?.id
      if (id !== undefined) {
        entryRefs.current[id]?.focus()
      }
      return
    }
  }

  const openMenu = (category: string) => {
    if (openCategory === category) {
      setOpenCategory(null)
      setPendingFocus(null)
    } else {
      setOpenCategory(category)
      setFocusIndex(0)
      setPendingFocus('first')
    }
  }

  // When the menu opens and a pending focus is set, focus the appropriate
  // item after the dropdown renders. This runs synchronously after render
  // via useEffect, which is reliable in jsdom.
  useEffect(() => {
    if (openCategory === null || pendingFocus === null) return
    const entries = menuCommands(openCategory)
    if (entries.length === 0) return
    if (pendingFocus === 'first') {
      const firstFocusable = entries.findIndex(
        (e) => resolveCommandAvailability(e, context).enabled,
      )
      const idx = firstFocusable >= 0 ? firstFocusable : 0
      setFocusIndex(idx)
      const id = entries[idx]?.id
      if (id !== undefined) {
        entryRefs.current[id]?.focus()
      }
    } else {
      // Find last focusable
      let lastFocusable = entries.length - 1
      for (let i = entries.length - 1; i >= 0; i--) {
        if (resolveCommandAvailability(entries[i]!, context).enabled) {
          lastFocusable = i
          break
        }
      }
      setFocusIndex(lastFocusable)
      const id = entries[lastFocusable]?.id
      if (id !== undefined) {
        entryRefs.current[id]?.focus()
      }
    }
    setPendingFocus(null)
  }, [openCategory, pendingFocus])

  // When the menu closes, clear stale focus index.
  useEffect(() => {
    if (openCategory === null) {
      setFocusIndex(0)
    }
  }, [openCategory])

  // Sync menu open state to the centralized shell UI store so the occlusion
  // policy (useViewportHost) knows to hide the native child viewport while
  // dropdowns are open, without bypassing the centralized visibility owner.
  useEffect(() => {
    useShellUiStore.getState().setMenuOpen(openCategory !== null)
    return () => {
      useShellUiStore.getState().setMenuOpen(false)
    }
  }, [openCategory])

  return (
    <nav className="app-menu" aria-label="Application menu">
      {categories.map((category) => (
        <div className="menu-item" key={category}>
          <button
            ref={(el) => { triggerRefs.current[category] = el }}
            className="menu-trigger"
            type="button"
            aria-haspopup="menu"
            aria-expanded={openCategory === category}
            onClick={() => openMenu(category)}
            onKeyDown={(e) => onTriggerKeyDown(e, category)}
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
      aria-disabled={!availability.enabled}
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
