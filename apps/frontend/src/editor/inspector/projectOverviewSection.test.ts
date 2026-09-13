import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { create } from '@bufbuild/protobuf'
import { ProjectSummarySchema, type ProjectSummary } from '@infraforge/protocol'
import {
  registerProjectOverviewSection,
  unregisterProjectOverviewSection,
} from './projectOverviewSection'
import { inspectorSectionRegistry } from './inspectorRegistry'
import { useProjectStore } from '../../features/project/projectStore'
import { useSelectionStore } from '../selection/selectionStore'

function makeSummary(uuid: string, name = 'Test'): ProjectSummary {
  return create(ProjectSummarySchema, {
    projectUuid: uuid,
    displayName: name,
    directory: '/test',
    revision: 1n,
    dirty: false,
  })
}

beforeEach(() => {
  useProjectStore.getState().clearProject()
  useSelectionStore.getState().clear()
  registerProjectOverviewSection()
})

afterEach(() => {
  unregisterProjectOverviewSection()
  useProjectStore.getState().clearProject()
  useSelectionStore.getState().clear()
})

describe('projectOverviewSection applies context', () => {
  it('applies when no entity is selected', () => {
    useProjectStore.getState().setSummary(makeSummary('uuid-1'))
    const sections = inspectorSectionRegistry.resolve({
      selectedIds: [],
      primaryId: null,
    })
    expect(sections.some((s) => s.id === 'project-overview')).toBe(true)
  })

  it('applies when the project root (canonical UUID) is selected', () => {
    useProjectStore.getState().setSummary(makeSummary('canonical-uuid'))
    const sections = inspectorSectionRegistry.resolve({
      selectedIds: ['canonical-uuid'],
      primaryId: 'canonical-uuid',
    })
    expect(sections.some((s) => s.id === 'project-overview')).toBe(true)
  })

  it('does not apply when a non-project entity is selected', () => {
    useProjectStore.getState().setSummary(makeSummary('uuid-1'))
    const sections = inspectorSectionRegistry.resolve({
      selectedIds: ['some-other-entity'],
      primaryId: 'some-other-entity',
    })
    expect(sections.some((s) => s.id === 'project-overview')).toBe(false)
  })

  it('does not apply when no project is open but an entity is selected', () => {
    const sections = inspectorSectionRegistry.resolve({
      selectedIds: ['orphan-id'],
      primaryId: 'orphan-id',
    })
    expect(sections.some((s) => s.id === 'project-overview')).toBe(false)
  })

  it('applies when no project is open and no entity is selected', () => {
    // The body renders "No project is open." in this case.
    const sections = inspectorSectionRegistry.resolve({
      selectedIds: [],
      primaryId: null,
    })
    expect(sections.some((s) => s.id === 'project-overview')).toBe(true)
  })
})
