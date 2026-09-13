import { beforeEach, describe, expect, it, vi } from 'vitest'
import { create } from '@bufbuild/protobuf'
import { JobRecordSchema, JobState, type EventEnvelope } from '@infraforge/protocol'
import { useTerrainStore } from './terrainStore'
import { applyTerrainEvent } from './terrainEvents'
import type { EngineClient } from '../../lib/engineSession'

function eventOf(partial: Record<string, unknown>): EventEnvelope {
  return partial as unknown as EventEnvelope
}

function queuedEvent(overrides: Record<string, unknown>): EventEnvelope {
  return eventOf({
    event: {
      case: 'jobQueued',
      value: {
        jobId: 'job-1',
        operation: 'terrain.import',
        label: 'copying source raster',
        cancellable: true,
        ...overrides,
      },
    },
  })
}

describe('terrain projection events', () => {
  const client = {} as EngineClient

  beforeEach(() => {
    useTerrainStore.getState().reset()
  })

  it('tracks job lifecycle through queued/started/progress/terminal events', () => {
    applyTerrainEvent(client, queuedEvent({}))
    applyTerrainEvent(
      client,
      eventOf({ event: { case: 'jobStarted', value: { jobId: 'job-1' } } }),
    )
    applyTerrainEvent(
      client,
      eventOf({
        event: {
          case: 'jobProgress',
          value: { jobId: 'job-1', progress: 0.7, processed: 7n, total: 10n, message: 'copying' },
        },
      }),
    )
    let jobs = useTerrainStore.getState().jobs
    expect(jobs).toHaveLength(1)
    expect(jobs[0]?.state).toBe(JobState.RUNNING)
    expect(jobs[0]?.processed).toBe(7n)

    applyTerrainEvent(
      client,
      eventOf({ event: { case: 'jobCompleted', value: { jobId: 'job-1' } } }),
    )
    jobs = useTerrainStore.getState().jobs
    expect(jobs[0]?.state).toBe(JobState.COMPLETED)
  })

  it('keeps the failure message of failed jobs', () => {
    applyTerrainEvent(client, queuedEvent({}))
    applyTerrainEvent(
      client,
      eventOf({
        event: {
          case: 'jobFailed',
          value: { jobId: 'job-1', errorCode: 'TERRAIN_UNSUPPORTED', errorMessage: 'raster could not be opened' },
        },
      }),
    )
    const jobs = useTerrainStore.getState().jobs
    expect(jobs[0]?.state).toBe(JobState.FAILED)
    expect(jobs[0]?.message).toBe('raster could not be opened')
  })

  it('adds datasets from terrain_dataset_added and ignores unknown events', () => {
    applyTerrainEvent(
      client,
      eventOf({
        event: {
          case: 'terrainDatasetAdded',
          value: { dataset: { datasetUuid: 'ds-1', displayName: 'Area DEM' }, revision: 2n },
        },
      }),
    )
    applyTerrainEvent(client, eventOf({ event: { case: 'somethingElse', value: {} } }))
    const state = useTerrainStore.getState()
    expect(state.datasets).toHaveLength(1)
    expect(state.datasets[0]?.datasetUuid).toBe('ds-1')
  })

  it('refreshes jobs and republishes the scene when terrain work settles', async () => {
    const publisher = vi.fn()
    const { setTerrainScenePublisher } = await import('./terrainEvents')
    setTerrainScenePublisher(publisher)

    // Create a tile job first so the terminal event can find it.
    applyTerrainEvent(
      client,
      eventOf({
        event: {
          case: 'jobQueued',
          value: { jobId: 'job-9', operation: 'terrain.tiles', label: 'generating tiles', cancellable: true },
        },
      }),
    )
    applyTerrainEvent(
      client,
      eventOf({ event: { case: 'jobCompleted', value: { jobId: 'job-9' } } }),
    )
    await vi.waitFor(() => {
      expect(publisher).toHaveBeenCalled()
    })
    setTerrainScenePublisher(null)
  })

  it('preserves bigint processed/total values above MAX_SAFE_INTEGER', () => {
    applyTerrainEvent(client, queuedEvent({}))
    applyTerrainEvent(
      client,
      eventOf({ event: { case: 'jobStarted', value: { jobId: 'job-1' } } }),
    )
    const bigProcessed = BigInt(Number.MAX_SAFE_INTEGER) + 1000n
    const bigTotal = BigInt(Number.MAX_SAFE_INTEGER) + 5000n
    applyTerrainEvent(
      client,
      eventOf({
        event: {
          case: 'jobProgress',
          value: { jobId: 'job-1', progress: 0.8, processed: bigProcessed, total: bigTotal, message: 'scanning' },
        },
      }),
    )
    const job = useTerrainStore.getState().jobs[0]
    expect(job?.processed).toBe(bigProcessed)
    expect(job?.total).toBe(bigTotal)
    // Verify no lossy round-trip: the bigint values are preserved exactly.
    expect(typeof job?.processed).toBe('bigint')
    expect(typeof job?.total).toBe('bigint')
  })
})
