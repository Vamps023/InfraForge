import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { render, screen, within, act, fireEvent } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { AppMenu } from './AppMenu'
import { Toolbar } from './Toolbar'
import {
  commandRegistry,
  type CommandContext,
  type CommandDefinition,
} from '../commands/commandRegistry'
import { registerBuiltinCommands, unregisterBuiltinCommands } from '../commands/builtinCommands'
import type { AvailabilityContext } from '../availability'

function readyContext(): AvailabilityContext {
  return {
    engine: 'ready',
    engineMessage: 'ready',
    project: 'project-open',
    viewportActive: true,
  }
}

function noProjectContext(): AvailabilityContext {
  return { ...readyContext(), project: 'no-project' }
}

function ctx(availability: AvailabilityContext): CommandContext {
  return { availability }
}

function makeCommand(overrides: Partial<CommandDefinition> = {}): CommandDefinition {
  return {
    id: 'test.cmd',
    label: 'Test',
    category: 'Test',
    execute: () => {},
    ...overrides,
  }
}

beforeEach(() => {
  unregisterBuiltinCommands()
  for (const command of commandRegistry.all()) {
    commandRegistry.unregister(command.id)
  }
})

afterEach(() => {
  unregisterBuiltinCommands()
  for (const command of commandRegistry.all()) {
    commandRegistry.unregister(command.id)
  }
})

describe('AppMenu reactivity', () => {
  it('renders categories when commands are already registered before mount', () => {
    commandRegistry.register(makeCommand({ id: 'a', category: 'Project', label: 'New Project…' }))
    commandRegistry.register(makeCommand({ id: 'b', category: 'View', label: 'Toggle Outliner' }))
    render(<AppMenu context={ctx(readyContext())} />)
    expect(screen.getByText('Project')).toBeInTheDocument()
    expect(screen.getByText('View')).toBeInTheDocument()
  })

  it('updates when a command is registered after mount', () => {
    render(<AppMenu context={ctx(readyContext())} />)
    expect(screen.queryByText('Project')).not.toBeInTheDocument()
    act(() => {
      commandRegistry.register(makeCommand({ id: 'late', category: 'Project', label: 'Late Cmd' }))
    })
    expect(screen.getByText('Project')).toBeInTheDocument()
  })

  it('updates when a command is unregistered after mount', () => {
    commandRegistry.register(makeCommand({ id: 'gone', category: 'Project', label: 'Gone' }))
    render(<AppMenu context={ctx(readyContext())} />)
    expect(screen.getByText('Project')).toBeInTheDocument()
    act(() => {
      commandRegistry.unregister('gone')
    })
    expect(screen.queryByText('Project')).not.toBeInTheDocument()
  })

  it('renders builtin Project and View categories after registration', () => {
    render(<AppMenu context={ctx(readyContext())} />)
    act(() => {
      registerBuiltinCommands({ getEngineClient: () => null })
    })
    expect(screen.getByText('Project')).toBeInTheDocument()
    expect(screen.getByText('View')).toBeInTheDocument()
  })

  it('New and Open become visible in the Project menu after registration', async () => {
    render(<AppMenu context={ctx(readyContext())} />)
    act(() => {
      registerBuiltinCommands({ getEngineClient: () => null })
    })
    // Open the Project menu dropdown.
    await userEvent.click(screen.getByText('Project'))
    const dropdown = screen.getByRole('menu')
    expect(within(dropdown).getByText('New Project…')).toBeInTheDocument()
    expect(within(dropdown).getByText('Open Project…')).toBeInTheDocument()
  })

  it('Save is disabled when no project is open', async () => {
    render(<AppMenu context={ctx(noProjectContext())} />)
    act(() => {
      registerBuiltinCommands({ getEngineClient: () => null })
    })
    await userEvent.click(screen.getByText('Project'))
    const dropdown = screen.getByRole('menu')
    const saveEntry = within(dropdown).getByText('Save Project').closest('button')!
    expect(saveEntry).toBeDisabled()
  })

  it('Save is enabled when a project is open and engine is ready', async () => {
    render(<AppMenu context={ctx(readyContext())} />)
    act(() => {
      registerBuiltinCommands({ getEngineClient: () => null })
    })
    await userEvent.click(screen.getByText('Project'))
    const dropdown = screen.getByRole('menu')
    const saveEntry = within(dropdown).getByText('Save Project').closest('button')!
    expect(saveEntry).not.toBeDisabled()
  })

  it('Save is disabled while a project operation is busy', async () => {
    render(<AppMenu context={ctx({ ...readyContext(), project: 'busy' })} />)
    act(() => {
      registerBuiltinCommands({ getEngineClient: () => null })
    })
    await userEvent.click(screen.getByText('Project'))
    const dropdown = screen.getByRole('menu')
    const saveEntry = within(dropdown).getByText('Save Project').closest('button')!
    expect(saveEntry).toBeDisabled()
  })
})

describe('AppMenu keyboard navigation', () => {
  it('Escape closes the open dropdown', async () => {
    act(() => {
      commandRegistry.register(makeCommand({ id: 'a', category: 'Project', label: 'Alpha' }))
      commandRegistry.register(makeCommand({ id: 'b', category: 'Project', label: 'Beta' }))
    })
    render(<AppMenu context={ctx(readyContext())} />)
    await userEvent.click(screen.getByText('Project'))
    expect(screen.getByRole('menu')).toBeInTheDocument()
    // Focus a menu item so keyboard events bubble through the dropdown.
    const dropdown = screen.getByRole('menu')
    const items = within(dropdown).getAllByRole('menuitem')
    await userEvent.click(items[0]!)
    await userEvent.keyboard('{Escape}')
    expect(screen.queryByRole('menu')).not.toBeInTheDocument()
  })

  it('ArrowDown moves focus to the next menu item', async () => {
    act(() => {
      commandRegistry.register(makeCommand({ id: 'a', category: 'Project', label: 'Alpha' }))
      commandRegistry.register(makeCommand({ id: 'b', category: 'Project', label: 'Beta' }))
    })
    render(<AppMenu context={ctx(readyContext())} />)
    await userEvent.click(screen.getByText('Project'))
    const dropdown = screen.getByRole('menu')
    const items = within(dropdown).getAllByRole('menuitem')
    // Focus the first item so the dropdown's onKeyDown receives the event.
    act(() => {
      items[0]!.focus()
    })
    fireEvent.keyDown(dropdown, { key: 'ArrowDown' })
    expect(items[1]).toHaveFocus()
  })

  it('ArrowUp moves focus to the previous menu item', async () => {
    act(() => {
      commandRegistry.register(makeCommand({ id: 'a', category: 'Project', label: 'Alpha' }))
      commandRegistry.register(makeCommand({ id: 'b', category: 'Project', label: 'Beta' }))
    })
    render(<AppMenu context={ctx(readyContext())} />)
    await userEvent.click(screen.getByText('Project'))
    const dropdown = screen.getByRole('menu')
    const items = within(dropdown).getAllByRole('menuitem')
    // Focus the second item, then ArrowUp should move to the first.
    act(() => {
      items[1]!.focus()
    })
    fireEvent.keyDown(dropdown, { key: 'ArrowUp' })
    expect(items[0]).toHaveFocus()
  })
})

describe('Toolbar reactivity', () => {
  it('renders toolbar commands after registration', () => {
    render(<Toolbar context={ctx(readyContext())} />)
    act(() => {
      registerBuiltinCommands({ getEngineClient: () => null })
    })
    expect(screen.getByText('New Project…')).toBeInTheDocument()
    expect(screen.getByText('Open Project…')).toBeInTheDocument()
  })

  it('updates when toolbar commands are registered after mount', () => {
    render(<Toolbar context={ctx(readyContext())} />)
    expect(screen.queryByText('New Project…')).not.toBeInTheDocument()
    act(() => {
      registerBuiltinCommands({ getEngineClient: () => null })
    })
    expect(screen.getByText('New Project…')).toBeInTheDocument()
  })

  it('updates when a toolbar command is unregistered', () => {
    render(<Toolbar context={ctx(readyContext())} />)
    act(() => {
      registerBuiltinCommands({ getEngineClient: () => null })
    })
    expect(screen.getByText('New Project…')).toBeInTheDocument()
    act(() => {
      unregisterBuiltinCommands()
    })
    expect(screen.queryByText('New Project…')).not.toBeInTheDocument()
  })

  it('does not render non-toolbar commands', () => {
    act(() => {
      commandRegistry.register(
        makeCommand({ id: 'menu-only', label: 'Menu Only', category: 'X', surfaces: ['menu'] }),
      )
    })
    render(<Toolbar context={ctx(readyContext())} />)
    expect(screen.queryByText('Menu Only')).not.toBeInTheDocument()
  })

  it('toolbar execution goes through the central command registry', async () => {
    const execute = vi.fn()
    act(() => {
      commandRegistry.register(
        makeCommand({
          id: 'tb-exec',
          label: 'TB Exec',
          category: 'X',
          surfaces: ['toolbar'],
          execute,
        }),
      )
    })
    render(<Toolbar context={ctx(readyContext())} />)
    await userEvent.click(screen.getByText('TB Exec'))
    expect(execute).toHaveBeenCalledTimes(1)
  })
})
