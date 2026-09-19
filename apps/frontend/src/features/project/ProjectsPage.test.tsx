import { beforeEach, describe, expect, it, vi } from 'vitest'
import { render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { ProjectsPage } from './ProjectsPage'
import { useRecentProjectsStore } from './recentProjectsStore'

describe('ProjectsPage', () => {
  beforeEach(() => {
    localStorage.clear()
    useRecentProjectsStore.setState({ projects: [] })
  })

  it('renders empty state when no projects exist', () => {
    render(
      <ProjectsPage
        client={null}
        onOpenDesign={vi.fn()}
        onOpenTerrain={vi.fn()}
        onNewProject={vi.fn()}
      />,
    )

    expect(screen.getByText('INFRAFORGE')).toBeInTheDocument()
    expect(screen.getByRole('heading', { name: 'Projects' })).toBeInTheDocument()
    expect(screen.getByText('No projects yet')).toBeInTheDocument()
    expect(screen.getByRole('button', { name: /create a project/i })).toBeInTheDocument()
  })

  it('renders workflow guide cards', () => {
    render(
      <ProjectsPage
        client={null}
        onOpenDesign={vi.fn()}
        onOpenTerrain={vi.fn()}
        onNewProject={vi.fn()}
      />,
    )

    expect(screen.getByText('Project workflow')).toBeInTheDocument()
    expect(screen.getByText('Terrain')).toBeInTheDocument()
    expect(screen.getByText('Design')).toBeInTheDocument()
  })

  it('renders project cards with CRS badge and actions', () => {
    useRecentProjectsStore.getState().addOrUpdate({
      id: 'p1',
      name: 'Highway 40',
      directory: '/projects/hwy40',
      createdAt: '2026-09-19T10:00:00Z',
      horizontalCrs: 'EPSG:32632',
      trafficSide: 'RIGHT',
      roadCount: 4,
      hasTerrain: true,
    })

    render(
      <ProjectsPage
        client={null}
        onOpenDesign={vi.fn()}
        onOpenTerrain={vi.fn()}
        onNewProject={vi.fn()}
      />,
    )

    expect(screen.getByText('Highway 40')).toBeInTheDocument()
    expect(screen.getByText('EPSG:32632')).toBeInTheDocument()
    expect(screen.getByText('RHT')).toBeInTheDocument()
    expect(screen.getByText('· 4 roads')).toBeInTheDocument()
    expect(screen.getByText('Step 3: Open Design Editor')).toBeInTheDocument()
  })
})
