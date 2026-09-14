import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { ProjectHomeScreen } from './ProjectHomeScreen'
import { registerBuiltinCommands, unregisterBuiltinCommands } from '../commands/builtinCommands'
import { commandRegistry, type CommandContext } from '../commands/useCommands'
import { useShellUiStore } from './shellUiStore'
import type { AvailabilityContext } from '../availability'

function readyContext(): AvailabilityContext {
  return {
    engine: 'ready',
    engineMessage: 'ready',
    project: 'no-project',
    viewportActive: true,
  }
}

function ctx(availability: AvailabilityContext): CommandContext {
  return { availability }
}

beforeEach(() => {
  for (const cmd of commandRegistry.all()) {
    commandRegistry.unregister(cmd.id)
  }
  registerBuiltinCommands({ getEngineClient: () => null })
  useShellUiStore.getState().closeDialog()
})

afterEach(() => {
  unregisterBuiltinCommands()
  useShellUiStore.getState().closeDialog()
})

describe('ProjectHomeScreen', () => {
  it('renders the InfraForge logo and title', () => {
    render(<ProjectHomeScreen context={ctx(readyContext())} />)
    expect(screen.getByText('InfraForge')).toBeInTheDocument()
  })

  it('renders the subtitle', () => {
    render(<ProjectHomeScreen context={ctx(readyContext())} />)
    expect(screen.getByText(/Native infrastructure authoring/)).toBeInTheDocument()
  })

  it('renders New Project and Open Project buttons', () => {
    render(<ProjectHomeScreen context={ctx(readyContext())} />)
    expect(screen.getByText('New Project')).toBeInTheDocument()
    expect(screen.getByText('Open Project')).toBeInTheDocument()
  })

  it('renders the Getting Started section', () => {
    render(<ProjectHomeScreen context={ctx(readyContext())} />)
    expect(screen.getByText('Getting Started')).toBeInTheDocument()
  })

  it('renders version information', () => {
    render(<ProjectHomeScreen context={ctx(readyContext())} />)
    expect(screen.getByText(/Developer Preview/)).toBeInTheDocument()
  })

  it('opens the new-project dialog when New Project is clicked', async () => {
    render(<ProjectHomeScreen context={ctx(readyContext())} />)
    await userEvent.click(screen.getByText('New Project'))
    expect(useShellUiStore.getState().openDialog).toBe('new-project')
  })

  it('has a region label for accessibility', () => {
    render(<ProjectHomeScreen context={ctx(readyContext())} />)
    expect(screen.getByRole('region', { name: 'Start screen' })).toBeInTheDocument()
  })
})
