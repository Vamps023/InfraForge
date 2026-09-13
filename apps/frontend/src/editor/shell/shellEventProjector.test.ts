import { beforeEach, describe, expect, it } from 'vitest'
import { create, toBinary, fromBinary } from '@bufbuild/protobuf'
import {
  EventEnvelopeSchema,
  FrameSchema,
  JobQueuedEventSchema,
  JobStartedEventSchema,
  JobProgressEventSchema,
  JobCompletedEventSchema,
  JobFailedEventSchema,
  JobCancelledEventSchema,
  DiagnosticAddedEventSchema,
  DiagnosticRemovedEventSchema,
  DiagnosticClearedEventSchema,
  DiagnosticSeverity,
  type EventEnvelope,
} from '@infraforge/protocol'
import { useOperationsStore } from '../operations/operationsStore'
import { useProblemsStore } from '../problems/problemsStore'
import { applyShellEvent } from './shellEventProjector'

function makeEvent(event: EventEnvelope['event']): EventEnvelope {
  return create(EventEnvelopeSchema, { eventId: 'evt-1', event })
}

function jobQueued(jobId: string, operation = 'project.create', label = 'Create Project'): EventEnvelope {
  return makeEvent({
    case: 'jobQueued',
    value: create(JobQueuedEventSchema, { jobId, operation, label, targetId: '', cancellable: false }),
  })
}

function jobStarted(jobId: string): EventEnvelope {
  return makeEvent({ case: 'jobStarted', value: create(JobStartedEventSchema, { jobId }) })
}

function jobProgress(
  jobId: string,
  progress?: number,
  processed?: bigint,
  total?: bigint,
): EventEnvelope {
  return makeEvent({
    case: 'jobProgress',
    value: create(JobProgressEventSchema, {
      jobId,
      progress,
      processed,
      total,
      message: '',
    }),
  })
}

function jobCompleted(jobId: string): EventEnvelope {
  return makeEvent({ case: 'jobCompleted', value: create(JobCompletedEventSchema, { jobId }) })
}

function jobFailed(jobId: string, errorCode = 'INTERNAL', errorMessage = 'something went wrong'): EventEnvelope {
  return makeEvent({
    case: 'jobFailed',
    value: create(JobFailedEventSchema, { jobId, errorCode, errorMessage }),
  })
}

function jobCancelled(jobId: string): EventEnvelope {
  return makeEvent({ case: 'jobCancelled', value: create(JobCancelledEventSchema, { jobId }) })
}

function diagnosticAdded(
  diagnosticId: string,
  source = 'engine',
  severity: DiagnosticSeverity = DiagnosticSeverity.WARNING,
  message = 'missing vertical CRS',
  targetId = '',
): EventEnvelope {
  return makeEvent({
    case: 'diagnosticAdded',
    value: create(DiagnosticAddedEventSchema, {
      diagnosticId,
      source,
      severity,
      message,
      targetId,
    }),
  })
}

function diagnosticRemoved(diagnosticId: string): EventEnvelope {
  return makeEvent({
    case: 'diagnosticRemoved',
    value: create(DiagnosticRemovedEventSchema, { diagnosticId }),
  })
}

function diagnosticCleared(source = ''): EventEnvelope {
  return makeEvent({
    case: 'diagnosticCleared',
    value: create(DiagnosticClearedEventSchema, { source }),
  })
}

beforeEach(() => {
  useOperationsStore.getState().clear()
  useProblemsStore.getState().clear()
})

describe('shellEventProjector — job events', () => {
  it('job.queued creates a pending operation', () => {
    applyShellEvent(jobQueued('job-1', 'project.create', 'Create Project'))
    const ops = useOperationsStore.getState().operations
    expect(ops).toHaveLength(1)
    expect(ops[0]!.id).toBe('job-1')
    expect(ops[0]!.state).toBe('pending')
    expect(ops[0]!.name).toBe('Create Project')
    expect(ops[0]!.type).toBe('project.create')
    expect(ops[0]!.source).toBe('project')
    expect(ops[0]!.cancellable).toBe(false)
    expect(ops[0]!.progress).toBeNull()
  })

  it('job.started transitions to running', () => {
    applyShellEvent(jobQueued('job-1'))
    applyShellEvent(jobStarted('job-1'))
    const op = useOperationsStore.getState().operations[0]!
    expect(op.state).toBe('running')
  })

  it('job.progress updates progress and preserves bigint counts', () => {
    applyShellEvent(jobQueued('job-1'))
    applyShellEvent(jobStarted('job-1'))
    applyShellEvent(jobProgress('job-1', 0.5, 50n, 100n))
    const op = useOperationsStore.getState().operations[0]!
    expect(op.progress).toBe(0.5)
    expect(op.processed).toBe(50n)
    expect(op.total).toBe(100n)
  })

  it('job.progress with absent progress keeps previous value', () => {
    applyShellEvent(jobQueued('job-1'))
    applyShellEvent(jobStarted('job-1'))
    applyShellEvent(jobProgress('job-1', 0.5, 50n, 100n))
    // Replayed progress without progress field should not reset to null
    applyShellEvent(jobProgress('job-1', undefined, 60n, 100n))
    const op = useOperationsStore.getState().operations[0]!
    expect(op.progress).toBe(0.5)
    expect(op.processed).toBe(60n)
  })

  it('job.progress preserves large bigint values without precision loss', () => {
    const large = BigInt(Number.MAX_SAFE_INTEGER) + 1n
    applyShellEvent(jobQueued('job-1'))
    applyShellEvent(jobStarted('job-1'))
    applyShellEvent(jobProgress('job-1', 0.5, large, large * 2n))
    const op = useOperationsStore.getState().operations[0]!
    expect(op.processed).toBe(large)
    expect(op.total).toBe(large * 2n)
  })

  it('job.completed transitions to completed with progress 1', () => {
    applyShellEvent(jobQueued('job-1'))
    applyShellEvent(jobStarted('job-1'))
    applyShellEvent(jobCompleted('job-1'))
    const op = useOperationsStore.getState().operations[0]!
    expect(op.state).toBe('completed')
    expect(op.progress).toBe(1)
  })

  it('job.failed transitions to failed with structured error message', () => {
    applyShellEvent(jobQueued('job-1'))
    applyShellEvent(jobStarted('job-1'))
    applyShellEvent(jobFailed('job-1', 'PROJECT_ALREADY_OPEN', 'a project is already open'))
    const op = useOperationsStore.getState().operations[0]!
    expect(op.state).toBe('failed')
    expect(op.message).toContain('PROJECT_ALREADY_OPEN')
    expect(op.message).toContain('a project is already open')
  })

  it('job.cancelled transitions to cancelled', () => {
    applyShellEvent(jobQueued('job-1'))
    applyShellEvent(jobStarted('job-1'))
    applyShellEvent(jobCancelled('job-1'))
    const op = useOperationsStore.getState().operations[0]!
    expect(op.state).toBe('cancelled')
  })

  it('duplicate job.queued is idempotent (does not create duplicate)', () => {
    applyShellEvent(jobQueued('job-1'))
    applyShellEvent(jobQueued('job-1'))
    expect(useOperationsStore.getState().operations).toHaveLength(1)
  })

  it('replayed job.queued after completion does not regress terminal state', () => {
    applyShellEvent(jobQueued('job-1'))
    applyShellEvent(jobStarted('job-1'))
    applyShellEvent(jobCompleted('job-1'))
    // Replayed queued event arrives after completion
    applyShellEvent(jobQueued('job-1'))
    const op = useOperationsStore.getState().operations[0]!
    expect(op.state).toBe('completed')
  })

  it('late job.started after completion does not regress terminal state', () => {
    applyShellEvent(jobQueued('job-1'))
    applyShellEvent(jobStarted('job-1'))
    applyShellEvent(jobCompleted('job-1'))
    // Late started event arrives after completion
    applyShellEvent(jobStarted('job-1'))
    const op = useOperationsStore.getState().operations[0]!
    expect(op.state).toBe('completed')
  })
})

describe('shellEventProjector — diagnostic events', () => {
  it('diagnostic.added creates a diagnostic', () => {
    applyShellEvent(diagnosticAdded('diag-1', 'geo', DiagnosticSeverity.WARNING, 'missing vertical CRS'))
    const diags = useProblemsStore.getState().diagnostics
    expect(diags).toHaveLength(1)
    expect(diags[0]!.id).toBe('diag-1')
    expect(diags[0]!.severity).toBe('warning')
    expect(diags[0]!.message).toBe('missing vertical CRS')
    expect(diags[0]!.source).toBe('geo')
  })

  it('diagnostic.added maps severity correctly', () => {
    applyShellEvent(diagnosticAdded('diag-err', 'geo', DiagnosticSeverity.ERROR))
    applyShellEvent(diagnosticAdded('diag-warn', 'geo', DiagnosticSeverity.WARNING))
    applyShellEvent(diagnosticAdded('diag-info', 'geo', DiagnosticSeverity.INFO))
    applyShellEvent(diagnosticAdded('diag-unspec', 'geo', DiagnosticSeverity.UNSPECIFIED))
    const diags = useProblemsStore.getState().diagnostics
    expect(diags.find((d) => d.id === 'diag-err')!.severity).toBe('error')
    expect(diags.find((d) => d.id === 'diag-warn')!.severity).toBe('warning')
    expect(diags.find((d) => d.id === 'diag-info')!.severity).toBe('info')
    expect(diags.find((d) => d.id === 'diag-unspec')!.severity).toBe('info')
  })

  it('diagnostic.removed removes the diagnostic', () => {
    applyShellEvent(diagnosticAdded('diag-1'))
    expect(useProblemsStore.getState().diagnostics).toHaveLength(1)
    applyShellEvent(diagnosticRemoved('diag-1'))
    expect(useProblemsStore.getState().diagnostics).toHaveLength(0)
  })

  it('diagnostic.cleared with source clears only that source', () => {
    applyShellEvent(diagnosticAdded('diag-1', 'geo'))
    applyShellEvent(diagnosticAdded('diag-2', 'terrain'))
    applyShellEvent(diagnosticCleared('geo'))
    const diags = useProblemsStore.getState().diagnostics
    expect(diags).toHaveLength(1)
    expect(diags[0]!.id).toBe('diag-2')
  })

  it('diagnostic.cleared without source clears all backend diagnostics but preserves shell-owned', () => {
    applyShellEvent(diagnosticAdded('diag-1', 'geo'))
    applyShellEvent(diagnosticAdded('diag-2', 'terrain'))
    // Shell-owned diagnostics from useProblemDiagnostics
    useProblemsStore.getState().upsert({
      id: 'engine-session:failed',
      severity: 'error',
      message: 'engine failed',
      source: 'engine',
    })
    useProblemsStore.getState().upsert({
      id: 'viewport:crashed',
      severity: 'error',
      message: 'viewport crashed',
      source: 'viewport',
    })
    applyShellEvent(diagnosticCleared())
    const diags = useProblemsStore.getState().diagnostics
    // Shell-owned diagnostics survive the clear
    expect(diags).toHaveLength(2)
    expect(diags.find((d) => d.source === 'engine')).toBeDefined()
    expect(diags.find((d) => d.source === 'viewport')).toBeDefined()
  })

  it('duplicate diagnostic.added is idempotent (upsert does not duplicate)', () => {
    applyShellEvent(diagnosticAdded('diag-1', 'geo', DiagnosticSeverity.WARNING, 'first'))
    applyShellEvent(diagnosticAdded('diag-1', 'geo', DiagnosticSeverity.ERROR, 'updated'))
    const diags = useProblemsStore.getState().diagnostics
    expect(diags).toHaveLength(1)
    expect(diags[0]!.severity).toBe('error')
    expect(diags[0]!.message).toBe('updated')
  })

  it('diagnostic.added with targetId preserves canonical target', () => {
    applyShellEvent(diagnosticAdded('diag-1', 'geo', DiagnosticSeverity.WARNING, 'msg', 'proj-uuid-123'))
    const diag = useProblemsStore.getState().diagnostics[0]!
    expect(diag.targetId).toBe('proj-uuid-123')
  })
})

describe('shellEventProjector — protocol serialization', () => {
  it('job events round-trip through protobuf serialization', () => {
    const event = makeEvent({
      case: 'jobProgress',
      value: create(JobProgressEventSchema, {
        jobId: 'job-serial',
        progress: 0.75,
        processed: 750n,
        total: 1000n,
        message: 'processing',
      }),
    })
    const frame = create(FrameSchema, {
      requestId: 'req-1',
      payload: { case: 'event', value: event },
    })
    const bytes = toBinary(FrameSchema, frame)
    const decoded = fromBinary(FrameSchema, bytes)
    expect(decoded.payload.case).toBe('event')
    if (decoded.payload.case === 'event') {
      expect(decoded.payload.value.event.case).toBe('jobProgress')
      if (decoded.payload.value.event.case === 'jobProgress') {
        const progress = decoded.payload.value.event.value
        expect(progress.jobId).toBe('job-serial')
        expect(progress.progress).toBe(0.75)
        expect(progress.processed).toBe(750n)
        expect(progress.total).toBe(1000n)
      }
    }
  })

  it('diagnostic events round-trip through protobuf serialization', () => {
    const event = makeEvent({
      case: 'diagnosticAdded',
      value: create(DiagnosticAddedEventSchema, {
        diagnosticId: 'diag-serial',
        source: 'geo',
        severity: DiagnosticSeverity.ERROR,
        message: 'vertical CRS missing',
        targetId: 'proj-123',
      }),
    })
    const frame = create(FrameSchema, {
      requestId: 'req-1',
      payload: { case: 'event', value: event },
    })
    const bytes = toBinary(FrameSchema, frame)
    const decoded = fromBinary(FrameSchema, bytes)
    expect(decoded.payload.case).toBe('event')
    if (decoded.payload.case === 'event') {
      expect(decoded.payload.value.event.case).toBe('diagnosticAdded')
      if (decoded.payload.value.event.case === 'diagnosticAdded') {
        const diag = decoded.payload.value.event.value
        expect(diag.diagnosticId).toBe('diag-serial')
        expect(diag.source).toBe('geo')
        expect(diag.severity).toBe(DiagnosticSeverity.ERROR)
        expect(diag.message).toBe('vertical CRS missing')
        expect(diag.targetId).toBe('proj-123')
      }
    }
  })

  it('optional progress fields distinguish unset from zero', () => {
    // progress absent
    const noProgress = create(JobProgressEventSchema, {
      jobId: 'job-1',
      message: '',
    })
    expect(noProgress.progress).toBeUndefined()
    expect(noProgress.processed).toBeUndefined()
    expect(noProgress.total).toBeUndefined()

    // progress = 0 (legitimate zero, not absent)
    const zeroProgress = create(JobProgressEventSchema, {
      jobId: 'job-2',
      progress: 0,
      processed: 0n,
      total: 100n,
      message: '',
    })
    expect(zeroProgress.progress).toBe(0)
    expect(zeroProgress.processed).toBe(0n)
    expect(zeroProgress.total).toBe(100n)

    // Round-trip preserves the distinction
    const bytes = toBinary(JobProgressEventSchema, zeroProgress)
    const decoded = fromBinary(JobProgressEventSchema, bytes)
    expect(decoded.progress).toBe(0)
    expect(decoded.processed).toBe(0n)
    expect(decoded.total).toBe(100n)
  })
})
