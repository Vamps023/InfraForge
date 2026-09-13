import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { render, screen, act } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { CommandPalette } from './CommandPalette'
import {
  commandRegistry,
  executeCommand,
  type CommandContext,
  type CommandDefinition,
} from './commandRegistry'
import type { AvailabilityContext } from '../availability'

function readyContext(): AvailabilityContext {
  return {
    engine: 'ready',
    engineMessage: 'ready',
    project: 'project-open',
    viewportActive: true,
  }
}

function ctx(availability: AvailabilityContext): CommandContext {
  return { availability }
}

function makeCommand(overrides: Partial<CommandDefinition> = {}): CommandDefinition {
  return {
    id: 'test.cmd',
    label: 'Test',
    category: 'Test',
    execute: vi.fn(),
    ...overrides,
  }
}

beforeEach(() => {
  for (const command of commandRegistry.all()) {
    commandRegistry.unregister(command.id)
  }
})

afterEach(() => {
  for (const command of commandRegistry.all()) {
    commandRegistry.unregister(command.id)
  }
})

describe('CommandPalette', () => {
  it('renders palette-surface commands', () => {
    act(() => {
      commandRegistry.register(
        makeCommand({ id: 'a', label: 'Alpha', category: 'X', surfaces: ['palette'] }),
      )
      commandRegistry.register(
        makeCommand({ id: 'b', label: 'Beta', category: 'Y', surfaces: ['palette'] }),
      )
    })
    render(<CommandPalette context={ctx(readyContext())} onClose={() => {}} />)
    expect(screen.getByText('Alpha')).toBeInTheDocument()
    expect(screen.getByText('Beta')).toBeInTheDocument()
  })

  it('does not render non-palette commands', () => {
    act(() => {
      commandRegistry.register(
        makeCommand({ id: 'menu-only', label: 'Menu Only', category: 'X', surfaces: ['menu'] }),
      )
    })
    render(<CommandPalette context={ctx(readyContext())} onClose={() => {}} />)
    expect(screen.queryByText('Menu Only')).not.toBeInTheDocument()
  })

  it('search filters by label', async () => {
    act(() => {
      commandRegistry.register(
        makeCommand({ id: 'a', label: 'Alpha', category: 'X', surfaces: ['palette'] }),
      )
      commandRegistry.register(
        makeCommand({ id: 'b', label: 'Beta', category: 'Y', surfaces: ['palette'] }),
      )
    })
    render(<CommandPalette context={ctx(readyContext())} onClose={() => {}} />)
    const input = screen.getByLabelText('Search commands')
    await userEvent.type(input, 'alph')
    expect(screen.getByText('Alpha')).toBeInTheDocument()
    expect(screen.queryByText('Beta')).not.toBeInTheDocument()
  })

  it('search filters by category', async () => {
    act(() => {
      commandRegistry.register(
        makeCommand({ id: 'a', label: 'Alpha', category: 'Terrain', surfaces: ['palette'] }),
      )
      commandRegistry.register(
        makeCommand({ id: 'b', label: 'Beta', category: 'Roads', surfaces: ['palette'] }),
      )
    })
    render(<CommandPalette context={ctx(readyContext())} onClose={() => {}} />)
    const input = screen.getByLabelText('Search commands')
    await userEvent.type(input, 'road')
    expect(screen.queryByText('Alpha')).not.toBeInTheDocument()
    expect(screen.getByText('Beta')).toBeInTheDocument()
  })

  it('ArrowDown moves active result', async () => {
    act(() => {
      commandRegistry.register(
        makeCommand({ id: 'a', label: 'Alpha', category: 'X', surfaces: ['palette'] }),
      )
      commandRegistry.register(
        makeCommand({ id: 'b', label: 'Beta', category: 'Y', surfaces: ['palette'] }),
      )
    })
    render(<CommandPalette context={ctx(readyContext())} onClose={() => {}} />)
    const input = screen.getByLabelText('Search commands')
    await userEvent.click(input)
    // First entry is active by default.
    expect(screen.getByText('Alpha').closest('li')).toHaveAttribute('aria-selected', 'true')
    await userEvent.keyboard('{ArrowDown}')
    expect(screen.getByText('Beta').closest('li')).toHaveAttribute('aria-selected', 'true')
  })

  it('ArrowUp moves active result', async () => {
    act(() => {
      commandRegistry.register(
        makeCommand({ id: 'a', label: 'Alpha', category: 'X', surfaces: ['palette'] }),
      )
      commandRegistry.register(
        makeCommand({ id: 'b', label: 'Beta', category: 'Y', surfaces: ['palette'] }),
      )
    })
    render(<CommandPalette context={ctx(readyContext())} onClose={() => {}} />)
    const input = screen.getByLabelText('Search commands')
    await userEvent.click(input)
    await userEvent.keyboard('{ArrowDown}')
    await userEvent.keyboard('{ArrowUp}')
    expect(screen.getByText('Alpha').closest('li')).toHaveAttribute('aria-selected', 'true')
  })

  it('Enter executes enabled result through central executor', async () => {
    const execute = vi.fn()
    act(() => {
      commandRegistry.register(
        makeCommand({ id: 'a', label: 'Alpha', category: 'X', surfaces: ['palette'], execute }),
      )
    })
    render(<CommandPalette context={ctx(readyContext())} onClose={() => {}} />)
    const input = screen.getByLabelText('Search commands')
    await userEvent.click(input)
    await userEvent.keyboard('{Enter}')
    expect(execute).toHaveBeenCalledTimes(1)
  })

  it('disabled commands cannot execute via Enter', async () => {
    const execute = vi.fn()
    act(() => {
      commandRegistry.register(
        makeCommand({
          id: 'a',
          label: 'Alpha',
          category: 'X',
          surfaces: ['palette'],
          requiresProject: true,
          execute,
        }),
      )
    })
    const noProjectCtx = ctx({ ...readyContext(), project: 'no-project' })
    render(<CommandPalette context={noProjectCtx} onClose={() => {}} />)
    const input = screen.getByLabelText('Search commands')
    await userEvent.click(input)
    await userEvent.keyboard('{Enter}')
    expect(execute).not.toHaveBeenCalled()
  })

  it('Escape closes the palette', async () => {
    const onClose = vi.fn()
    act(() => {
      commandRegistry.register(
        makeCommand({ id: 'a', label: 'Alpha', category: 'X', surfaces: ['palette'] }),
      )
    })
    render(<CommandPalette context={ctx(readyContext())} onClose={onClose} />)
    const input = screen.getByLabelText('Search commands')
    await userEvent.click(input)
    await userEvent.keyboard('{Escape}')
    expect(onClose).toHaveBeenCalledTimes(1)
  })

  it('dynamically registered commands appear in the open palette', () => {
    render(<CommandPalette context={ctx(readyContext())} onClose={() => {}} />)
    expect(screen.queryByText('Late')).not.toBeInTheDocument()
    act(() => {
      commandRegistry.register(
        makeCommand({ id: 'late', label: 'Late', category: 'X', surfaces: ['palette'] }),
      )
    })
    expect(screen.getByText('Late')).toBeInTheDocument()
  })

  it('dynamically unregistered commands disappear from the open palette', () => {
    act(() => {
      commandRegistry.register(
        makeCommand({ id: 'gone', label: 'Gone', category: 'X', surfaces: ['palette'] }),
      )
    })
    render(<CommandPalette context={ctx(readyContext())} onClose={() => {}} />)
    expect(screen.getByText('Gone')).toBeInTheDocument()
    act(() => {
      commandRegistry.unregister('gone')
    })
    expect(screen.queryByText('Gone')).not.toBeInTheDocument()
  })

  it('focuses the search field on open', () => {
    act(() => {
      commandRegistry.register(
        makeCommand({ id: 'a', label: 'Alpha', category: 'X', surfaces: ['palette'] }),
      )
    })
    render(<CommandPalette context={ctx(readyContext())} onClose={() => {}} />)
    const input = screen.getByLabelText('Search commands')
    expect(input).toHaveFocus()
  })

  it('invisible commands are excluded', () => {
    act(() => {
      commandRegistry.register(
        makeCommand({
          id: 'hidden',
          label: 'Hidden',
          category: 'X',
          surfaces: ['palette'],
          visible: () => false,
        }),
      )
      commandRegistry.register(
        makeCommand({ id: 'visible', label: 'Visible', category: 'X', surfaces: ['palette'] }),
      )
    })
    render(<CommandPalette context={ctx(readyContext())} onClose={() => {}} />)
    expect(screen.queryByText('Hidden')).not.toBeInTheDocument()
    expect(screen.getByText('Visible')).toBeInTheDocument()
  })
})
