import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { create } from '@bufbuild/protobuf'
import { ProjectSummarySchema, type ProjectSummary } from '@infraforge/protocol'
import {
  registerProjectRootProjection,
  unregisterProjectRootProjection,
} from './projectRootProjection'
import { outlinerProjectionRegistry } from './outlinerProjection'
import { useProjectStore } from '../../features/project/projectStore'

function makeSummary(uuid: string, name: string): ProjectSummary {
  return create(ProjectSummarySchema, {
    projectUuid: uuid,
    displayName: name,
    directory: '/test',
    revision: 1n,
    dirty: false,
  })
}

beforeEach(() => {
  for (const projection of outlinerProjectionRegistry.all()) {
    outlinerProjectionRegistry.unregister(projection.id)
  }
  useProjectStore.getState().clearProject()
  // Reset the module-level unsubscribed flag by re-importing would be
  // complex; instead we ensure clean state by clearing the project store
  // and the registry before each test.
})

afterEach(() => {
  unregisterProjectRootProjection()
  for (const projection of outlinerProjectionRegistry.all()) {
    outlinerProjectionRegistry.unregister(projection.id)
  }
  useProjectStore.getState().clearProject()
})

describe('projectRootProjection', () => {
  it('renders no project-root node when no project is open', () => {
    registerProjectRootProjection()
    const projection = outlinerProjectionRegistry.get('project-root')
    expect(projection).toBeDefined()
    expect(projection!.getNodes()).toEqual([])
  })

  it('renders a project-root node when a project is open', () => {
    useProjectStore.getState().setSummary(makeSummary('uuid-1', 'My Project'))
    registerProjectRootProjection()
    const projection = outlinerProjectionRegistry.get('project-root')
    const nodes = projection!.getNodes()
    expect(nodes).toHaveLength(1)
    expect(nodes[0]!.id).toBe('uuid-1')
    expect(nodes[0]!.label).toBe('My Project')
    expect(nodes[0]!.type).toBe('project')
    expect(nodes[0]!.parentId).toBeNull()
    expect(nodes[0]!.depth).toBe(0)
  })

  it('root ID equals the canonical backend project UUID', () => {
    useProjectStore.getState().setSummary(makeSummary('canonical-uuid-abc', 'Test'))
    registerProjectRootProjection()
    const nodes = outlinerProjectionRegistry.get('project-root')!.getNodes()
    expect(nodes[0]!.id).toBe('canonical-uuid-abc')
  })

  it('updates when the project summary changes', () => {
    useProjectStore.getState().setSummary(makeSummary('uuid-1', 'Old Name'))
    registerProjectRootProjection()
    let nodes = outlinerProjectionRegistry.get('project-root')!.getNodes()
    expect(nodes[0]!.label).toBe('Old Name')
    useProjectStore.getState().setSummary(makeSummary('uuid-2', 'New Name'))
    nodes = outlinerProjectionRegistry.get('project-root')!.getNodes()
    expect(nodes[0]!.id).toBe('uuid-2')
    expect(nodes[0]!.label).toBe('New Name')
  })

  it('removes the root node when the project closes', () => {
    useProjectStore.getState().setSummary(makeSummary('uuid-1', 'Proj'))
    registerProjectRootProjection()
    expect(outlinerProjectionRegistry.get('project-root')!.getNodes()).toHaveLength(1)
    useProjectStore.getState().clearProject()
    expect(outlinerProjectionRegistry.get('project-root')!.getNodes()).toEqual([])
  })

  it('does not generate fake child entities', () => {
    useProjectStore.getState().setSummary(makeSummary('uuid-1', 'Proj'))
    registerProjectRootProjection()
    const nodes = outlinerProjectionRegistry.get('project-root')!.getNodes()
    expect(nodes).toHaveLength(1)
    expect(nodes[0]!.hasChildren).toBe(false)
  })

  it('emits a change notification when the summary changes', () => {
    useProjectStore.getState().setSummary(makeSummary('uuid-1', 'Old'))
    registerProjectRootProjection()
    let emitted = false
    const unsub = outlinerProjectionRegistry.get('project-root')!.subscribe(() => {
      emitted = true
    })
    useProjectStore.getState().setSummary(makeSummary('uuid-2', 'New'))
    expect(emitted).toBe(true)
    unsub()
  })
})
