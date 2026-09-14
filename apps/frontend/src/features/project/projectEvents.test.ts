import { beforeEach, describe, expect, it } from 'vitest'
import { create } from '@bufbuild/protobuf'
import {
  ProjectSummarySchema,
  TerrainDatasetInfoSchema,
  type ProjectSummary,
  type TerrainDatasetInfo,
} from '@infraforge/protocol'
import { applyProjectEvent } from './projectEvents'
import { useProjectStore } from './projectStore'
import { useSelectionStore } from '../../editor/selection/selectionStore'
import { useTerrainStore } from '../terrain/terrainStore'
import type { EngineClient } from '../../lib/engineSession'
import type { EventEnvelope } from '@infraforge/protocol'

function makeSummary(name: string, dir: string, rev: bigint): ProjectSummary {
  return create(ProjectSummarySchema, {
    displayName: name,
    directory: dir,
    revision: rev,
    dirty: false,
  })
}

function makeDataset(uuid: string, name: string): TerrainDatasetInfo {
  return create(TerrainDatasetInfoSchema, {
    datasetUuid: uuid,
    displayName: name,
    sourceCrs: 'EPSG:32633',
  })
}

// Minimal mock client: applyProjectEvent only uses the client for async
// terrain refresh calls which are fire-and-forget; the mock returns empty
// results so the projection stays clean.
const mockClient = {
  sendCommand: async () => ({}),
  onEvent: () => () => {},
} as unknown as EngineClient

beforeEach(() => {
  useProjectStore.getState().clearProject()
  useSelectionStore.getState().clear()
  useTerrainStore.getState().reset()
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
    applyProjectEvent(mockClient, event)

    expect(useSelectionStore.getState().selectedIds).toEqual([])
    expect(useSelectionStore.getState().primaryId).toBeNull()
  })

  it('clears selection when a project closes', () => {
    useProjectStore.getState().setSummary(makeSummary('Old', '/old', 1n))
    useSelectionStore.getState().select(['entity:1'])
    expect(useSelectionStore.getState().selectedIds).toHaveLength(1)

    const event = { event: { case: 'projectClosed', value: {} } } as unknown as EventEnvelope
    applyProjectEvent(mockClient, event)

    expect(useProjectStore.getState().summary).toBeNull()
    expect(useSelectionStore.getState().selectedIds).toEqual([])
    expect(useSelectionStore.getState().primaryId).toBeNull()
  })

  it('resets terrain store when a project closes', () => {
    useTerrainStore.getState().setDatasets([
      makeDataset('ds-1', 'test'),
    ])
    useTerrainStore.getState().setImporting(true)
    expect(useTerrainStore.getState().datasets).toHaveLength(1)

    const event = { event: { case: 'projectClosed', value: {} } } as unknown as EventEnvelope
    applyProjectEvent(mockClient, event)

    expect(useTerrainStore.getState().datasets).toEqual([])
    expect(useTerrainStore.getState().importing).toBe(false)
  })

  it('resets terrain store when a new project opens', () => {
    useTerrainStore.getState().setDatasets([
      makeDataset('ds-old', 'old'),
    ])
    useTerrainStore.getState().setImporting(true)
    expect(useTerrainStore.getState().datasets).toHaveLength(1)

    const event = {
      event: {
        case: 'projectOpened',
        value: { summary: makeSummary('New', '/new', 1n) },
      },
    } as unknown as EventEnvelope
    applyProjectEvent(mockClient, event)

    // Terrain store is reset immediately (async refresh runs in background).
    expect(useTerrainStore.getState().datasets).toEqual([])
    expect(useTerrainStore.getState().importing).toBe(false)
  })

  it('does not clear selection on a revision change (project stays valid)', () => {
    useProjectStore.getState().setSummary(makeSummary('Proj', '/proj', 1n))
    useSelectionStore.getState().select(['entity:1'])
    expect(useSelectionStore.getState().selectedIds).toHaveLength(1)

    const event = {
      event: { case: 'projectRevisionChanged', value: { revision: 2n } },
    } as unknown as EventEnvelope
    applyProjectEvent(mockClient, event)

    expect(useSelectionStore.getState().selectedIds).toEqual(['entity:1'])
  })

  it('does not clear selection on a dirty-state change', () => {
    useProjectStore.getState().setSummary(makeSummary('Proj', '/proj', 1n))
    useSelectionStore.getState().select(['entity:1'])

    const event = {
      event: { case: 'projectDirtyStateChanged', value: { dirty: true, revision: 2n } },
    } as unknown as EventEnvelope
    applyProjectEvent(mockClient, event)

    expect(useSelectionStore.getState().selectedIds).toEqual(['entity:1'])
  })
})
