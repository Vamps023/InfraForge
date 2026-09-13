import { create } from '@bufbuild/protobuf'
import { JobState, JobRecordSchema, type EventEnvelope } from '@infraforge/protocol'
import type { EngineClient } from '../../lib/engineSession'
import { fetchTerrainScene, refreshTerrainJobs } from './terrainApi'
import { useTerrainStore } from './terrainStore'

// Applies engine-originated terrain/job events to the UI projection and
// refreshes the renderer scene projection after terrain work settles. The
// scene payload is forwarded to the native viewport by the App-level
// subscriber; the frontend itself never interprets tile content.
//
// Protocol 1.4: job lifecycle events use the canonical 1.3 format (owned by
// Issue #5). The job.queued event carries the operation name; subsequent
// events carry only the job ID. The frontend tracks job records by ID.

// Set after subscribeTerrainEvents by the shell so event handlers can push
// the updated scene to the viewport without re-subscribing.
let scenePublisher: (() => void) | null = null

export function setTerrainScenePublisher(publisher: (() => void) | null) {
  scenePublisher = publisher
}

function isTerrainJobOperation(operation: string): boolean {
  return operation === 'terrain.import' || operation === 'terrain.tiles'
}

async function refreshAfterTerrainWork(client: EngineClient): Promise<void> {
  try {
    await refreshTerrainJobs(client)
  } catch {
    // Job list refresh is a projection nicety; failures surface through the
    // next command instead of masking the event that triggered this.
  }
  if (scenePublisher) {
    scenePublisher()
  }
}

export function applyTerrainEvent(client: EngineClient, event: EventEnvelope) {
  const store = useTerrainStore.getState()
  switch (event.event.case) {
    case 'jobQueued': {
      const queued = event.event.value
      if (!isTerrainJobOperation(queued.operation)) {
        break
      }
      store.applyJob(
        create(JobRecordSchema, {
          jobId: queued.jobId,
          operation: queued.operation,
          state: JobState.QUEUED,
          label: queued.label,
          cancellable: queued.cancellable,
          createdAt: '',
        }),
      )
      break
    }
    case 'jobStarted': {
      const jobId = event.event.value.jobId
      const existing = store.jobs.find((job) => job.jobId === jobId)
      if (existing) {
        store.applyJob(
          create(JobRecordSchema, {
            ...existing,
            state: JobState.RUNNING,
          }),
        )
      }
      break
    }
    case 'jobProgress': {
      const progress = event.event.value
      const existing = store.jobs.find((job) => job.jobId === progress.jobId)
      if (!existing) {
        break
      }
      store.applyJob(
        create(JobRecordSchema, {
          ...existing,
          state: JobState.RUNNING,
          progress: progress.progress ?? existing.progress,
          processed: progress.processed ?? existing.processed,
          total: progress.total ?? existing.total,
          label: progress.message ?? existing.label,
        }),
      )
      break
    }
    case 'jobCompleted':
    case 'jobFailed':
    case 'jobCancelled': {
      const envelope = event.event.value
      const jobId = envelope.jobId
      const existing = store.jobs.find((job) => job.jobId === jobId)
      if (!existing) {
        break
      }
      const terminalState =
        event.event.case === 'jobCompleted'
          ? JobState.COMPLETED
          : event.event.case === 'jobFailed'
            ? JobState.FAILED
            : JobState.CANCELLED
      const failureMessage =
        event.event.case === 'jobFailed' ? event.event.value.errorMessage : ''
      store.applyJob(
        create(JobRecordSchema, {
          ...existing,
          state: terminalState,
          message: failureMessage,
        }),
      )
      if (isTerrainJobOperation(existing.operation)) {
        void refreshAfterTerrainWork(client)
      }
      break
    }
    case 'terrainDatasetAdded': {
      const dataset = event.event.value.dataset
      if (dataset) {
        store.upsertDataset(dataset)
      }
      break
    }
    default:
      // Unknown events are ignored by this projection; other feature
      // projections subscribe independently.
      break
  }
}

export function subscribeTerrainEvents(client: EngineClient): () => void {
  const listener = (event: EventEnvelope) => applyTerrainEvent(client, event)
  return client.onEvent(listener)
}
