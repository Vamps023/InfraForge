import { beforeEach, describe, expect, it, vi } from 'vitest'
import { render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { AppHeader } from './AppHeader'
import { useProjectStore } from '../../features/project/projectStore'
import { create } from '@bufbuild/protobuf'
import { ProjectSummarySchema } from '@infraforge/protocol'

describe('AppHeader', () => {
  beforeEach(() => {
    useProjectStore.getState().clearProject()
    window.infraforgeDesktop = {
      ...window.infraforgeDesktop,
      setViewportCamera: vi.fn(),
    } as any
  })

  it('renders Projects back button and navigates', async () => {
    const onNavigateProjects = vi.fn()
    render(
      <AppHeader
        activePage="design"
        onNavigateProjects={onNavigateProjects}
      />,
    )

    const backBtn = screen.getByRole('button', { name: /projects/i })
    expect(backBtn).toBeInTheDocument()
    await userEvent.click(backBtn)
    expect(onNavigateProjects).toHaveBeenCalledOnce()
  })

  it('renders 2D and 3D toggle buttons in design mode and dispatches camera commands', async () => {
    render(
      <AppHeader
        activePage="design"
        onNavigateProjects={vi.fn()}
      />,
    )

    const toggle2D = screen.getByRole('button', { name: '2D' })
    const toggle3D = screen.getByRole('button', { name: '3D' })
    expect(toggle2D).toBeInTheDocument()
    expect(toggle3D).toBeInTheDocument()

    await userEvent.click(toggle2D)
    expect(window.infraforgeDesktop?.setViewportCamera).toHaveBeenCalledWith('top')

    await userEvent.click(toggle3D)
    expect(window.infraforgeDesktop?.setViewportCamera).toHaveBeenCalledWith('perspective')
  })

  it('renders Fit View button and calls frame-all', async () => {
    render(
      <AppHeader
        activePage="design"
        onNavigateProjects={vi.fn()}
      />,
    )

    const fitBtn = screen.getByRole('button', { name: /fit view/i })
    await userEvent.click(fitBtn)
    expect(window.infraforgeDesktop?.setViewportCamera).toHaveBeenCalledWith('frame-all')
  })
})
