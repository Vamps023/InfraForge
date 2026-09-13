import { beforeEach, describe, expect, it } from 'vitest'
import { create } from '@bufbuild/protobuf'
import { ProjectSummarySchema, type ProjectSummary } from '@infraforge/protocol'
import { applyProjectEvent } from './projectEvents'
import { useProjectStore } from './projectStore'
import { useSelectionStore } from '../../editor/selection/selectionStore'
import type { EventEnvelope } from '@infraforge/protocol'

function makeSummary(name: string, dir: string, rev: bigint): ProjectSummary {
  return create(ProjectSummarySchema, {
    displayName: name,
    directory: dir,
    revision: rev,
    dirty: false,
  })
}

beforeEach(() => {
  useProjectStore.getState().clearProject()
  useSelectionStore.getState().clear()
})

describe('selection lifecycle boundaries', () => {
  it('clears selection when a project opens', () => {
    useSelectionStore.getState().select(['old-entity:1', 'old-entity:2'])
    expect(useSelectionStore.getState().selectedIds).toHaveLength(2)

    const event = {
      event: {
        case: 'projectOpened',
        value: { summary: makeSummary('New', '/new', 1n) },
      },
    } as unknown as EventEnvelope
    applyProjectEvent(event)

    expect(useSelectionStore.getState().selectedIds).toEqual([])
    expect(useSelectionStore.getState().primaryId).toBeNull()
  })

  it('clears selection when a project closes', () => {
    useProjectStore.getState().setSummary(makeSummary('Old', '/old', 1n))
    useSelectionStore.getState().select(['entity:1'])
    expect(useSelectionStore.getState().selectedIds).toHaveLength(1)

    const event = { event: { case: 'projectClosed', value: {} } } as unknown as EventEnvelope
    applyProjectEvent(event)

    expect(useProjectStore.getState().summary).toBeNull()
    expect(useSelectionStore.getState().selectedIds).toEqual([])
    expect(useSelectionStore.getState().primaryId).toBeNull()
  })

  it('does not clear selection on a revision change (project stays valid)', () => {
    useProjectStore.getState().setSummary(makeSummary('Proj', '/proj', 1n))
    useSelectionStore.getState().select(['entity:1'])
    expect(useSelectionStore.getState().selectedIds).toHaveLength(1)

    const event = {
      event: { case: 'projectRevisionChanged', value: { revision: 2n } },
    } as unknown as EventEnvelope
    applyProjectEvent(event)

    expect(useSelectionStore.getState().selectedIds).toEqual(['entity:1'])
  })

  it('does not clear selection on a dirty-state change', () => {
    useProjectStore.getState().setSummary(makeSummary('Proj', '/proj', 1n))
    useSelectionStore.getState().select(['entity:1'])

    const event = {
      event: { case: 'projectDirtyStateChanged', value: { dirty: true, revision: 2n } },
    } as unknown as EventEnvelope
    applyProjectEvent(event)

    expect(useSelectionStore.getState().selectedIds).toEqual(['entity:1'])
  })
})
