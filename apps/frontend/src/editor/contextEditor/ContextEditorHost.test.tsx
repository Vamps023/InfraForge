import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { cleanup, render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { ContextEditorHost } from './ContextEditorHost'
import { contextEditorRegistry } from './contextEditorRegistry'
import { useSelectionStore } from '../selection/selectionStore'
import { useWorkspaceStore } from '../shell/workspaceStore'
import { useLayoutStore } from '../layout/layoutStore'

beforeEach(() => {
  useSelectionStore.getState().clear()
  useWorkspaceStore.getState().setWorkspace('roads')
  useLayoutStore.getState().setPanelVisible('contextEditor', true)
})

afterEach(() => {
  cleanup()
})

describe('ContextEditorHost', () => {
  it('renders nothing when no editor applies to selection', () => {
    const { container } = render(<ContextEditorHost />)
    expect(container.firstChild).toBeNull()
  })

  it('renders active context editor when condition is met', () => {
    contextEditorRegistry.register({
      id: 'test-road-profile',
      label: 'Test Road Profile',
      applies: (ctx) => ctx.activeWorkspace === 'roads' && ctx.selectedIds.some((id) => id.startsWith('road:')),
      render: () => <div data-testid="profile-content">Profile Details</div>,
    })

    useSelectionStore.getState().select(['road:test-123'])

    render(<ContextEditorHost />)
    expect(screen.getByText('Test Road Profile')).toBeTruthy()
    expect(screen.getByTestId('profile-content')).toBeTruthy()

    contextEditorRegistry.unregister('test-road-profile')
  })

  it('collapses when close button is clicked', async () => {
    contextEditorRegistry.register({
      id: 'test-close',
      label: 'Test Close',
      applies: () => true,
      render: () => <div>Body</div>,
    })

    render(<ContextEditorHost />)
    const closeBtn = screen.getByLabelText('Close context editor')
    await userEvent.click(closeBtn)

    expect(useLayoutStore.getState().panels.contextEditor.visible).toBe(false)
    contextEditorRegistry.unregister('test-close')
  })
})
