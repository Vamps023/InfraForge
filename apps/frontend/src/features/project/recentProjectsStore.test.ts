import { beforeEach, describe, expect, it } from 'vitest'
import { useRecentProjectsStore } from './recentProjectsStore'
import { create } from '@bufbuild/protobuf'
import { ProjectSummarySchema } from '@infraforge/protocol'

describe('recentProjectsStore', () => {
  beforeEach(() => {
    localStorage.clear()
    useRecentProjectsStore.setState({ projects: [] })
  })

  it('adds and updates recent projects', () => {
    useRecentProjectsStore.getState().addOrUpdate({
      id: 'proj-1',
      name: 'Highway 101',
      directory: '/path/to/highway101',
      createdAt: '2026-09-19T10:00:00Z',
      horizontalCrs: 'EPSG:32632',
      trafficSide: 'RIGHT',
      roadCount: 5,
      hasTerrain: true,
    })

    const state = useRecentProjectsStore.getState()
    expect(state.projects).toHaveLength(1)
    expect(state.projects[0]!.name).toBe('Highway 101')
    expect(state.projects[0]!.horizontalCrs).toBe('EPSG:32632')
  })

  it('records from project summary', () => {
    const summary = create(ProjectSummarySchema, {
      projectUuid: 'uuid-123',
      displayName: 'Alpine Pass',
      directory: '/data/alpine',
      georeference: {
        horizontalCrs: 'EPSG:2056',
        originHeight: 500,
      },
      trafficSide: 2, // RIGHT
    })

    useRecentProjectsStore.getState().recordFromSummary(summary, 12, true)
    const state = useRecentProjectsStore.getState()
    expect(state.projects).toHaveLength(1)
    expect(state.projects[0]!.name).toBe('Alpine Pass')
    expect(state.projects[0]!.horizontalCrs).toBe('EPSG:2056')
    expect(state.projects[0]!.roadCount).toBe(12)
    expect(state.projects[0]!.hasTerrain).toBe(true)
  })

  it('removes a project by directory', () => {
    useRecentProjectsStore.getState().addOrUpdate({
      id: 'p1',
      name: 'To Delete',
      directory: '/delete/me',
      createdAt: '2026-09-19T10:00:00Z',
    })
    expect(useRecentProjectsStore.getState().projects).toHaveLength(1)

    useRecentProjectsStore.getState().remove('/delete/me')
    expect(useRecentProjectsStore.getState().projects).toHaveLength(0)
  })
})
