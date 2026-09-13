import type { EventEnvelope, DiagnosticSeverity as ProtoDiagnosticSeverity } from '@infraforge/protocol'
import { useOperationsStore, type OperationProjection, type OperationState } from '../operations/operationsStore'
import { useProblemsStore, type DiagnosticSeverity, type DiagnosticProjection } from '../problems/problemsStore'
import type { EngineClient } from '../../lib/engineSession'

// Shell event projector — consumes real backend job.* and diagnostic.*
// lifecycle events and projects them into the Operations and Problems stores.
//
// This module does NOT fabricate operations or diagnostics. It only translates
// canonical server events into the existing frontend projection stores.
//
// Idempotency: the event stream does not guarantee exactly-once delivery.
// Each event carries a stable job/diagnostic ID, so upserts are idempotent —
// a replayed event produces the same projection state. Terminal states
// (completed/failed/cancelled) are never overwritten by non-terminal events
// to prevent out-of-order regressions.

// Terminal operation states — once reached, a later non-terminal event for
// the same job must not regress the projection (e.g. a late job.started
// arriving after job.completed).
const TERMINAL_OPERATION_STATES: ReadonlySet<OperationState> = new Set([
  'completed',
  'failed',
  'cancelled',
])

function mapSeverity(severity: ProtoDiagnosticSeverity): DiagnosticSeverity {
  switch (severity) {
    case 3: // DIAGNOSTIC_SEVERITY_ERROR
      return 'error'
    case 2: // DIAGNOSTIC_SEVERITY_WARNING
      return 'warning'
    case 1: // DIAGNOSTIC_SEVERITY_INFO
      return 'info'
    default:
      return 'info'
  }
}

function applyJobEvent(event: EventEnvelope): void {
  const operations = useOperationsStore.getState()
  switch (event.event.case) {
    case 'jobQueued': {
      const value = event.event.value
      // Idempotent: upsert by job ID. If the job already exists (replayed
      // event), the upsert merges without losing newer state.
      const existing = operations.operations.find((op) => op.id === value.jobId)
      if (existing && TERMINAL_OPERATION_STATES.has(existing.state)) {
        // Terminal state already reached; do not regress to pending.
        return
      }
      const projection: OperationProjection = {
        id: value.jobId,
        name: value.label || value.operation,
        type: value.operation,
        state: 'pending',
        progress: null,
        cancellable: value.cancellable,
        source: value.operation.split('.')[0] || 'engine',
        targetId: value.targetId || undefined,
      }
      operations.upsert(projection)
      break
    }
    case 'jobStarted': {
      const value = event.event.value
      const existing = operations.operations.find((op) => op.id === value.jobId)
      if (existing && TERMINAL_OPERATION_STATES.has(existing.state)) {
        return
      }
      operations.patch(value.jobId, { state: 'running' })
      break
    }
    case 'jobProgress': {
      const value = event.event.value
      const existing = operations.operations.find((op) => op.id === value.jobId)
      if (existing && TERMINAL_OPERATION_STATES.has(existing.state)) {
        return
      }
      // Preserve optional presence semantics: progress/processed/total are
      // optional in the protocol. Only update when the backend actually
      // supplied them. bigint counts are preserved as-is (no Number() round
      // trip) — the store keeps them as bigint to avoid precision loss.
      operations.patch(value.jobId, {
        progress: value.progress ?? existing?.progress ?? null,
        processed: value.processed !== undefined ? value.processed : existing?.processed,
        total: value.total !== undefined ? value.total : existing?.total,
        ...(value.message ? { message: value.message } : {}),
      })
      break
    }
    case 'jobCompleted': {
      const value = event.event.value
      operations.patch(value.jobId, { state: 'completed', progress: 1 })
      break
    }
    case 'jobFailed': {
      const value = event.event.value
      operations.patch(value.jobId, {
        state: 'failed',
        message: `${value.errorCode}: ${value.errorMessage}`,
      })
      break
    }
    case 'jobCancelled': {
      const value = event.event.value
      operations.patch(value.jobId, { state: 'cancelled' })
      break
    }
    default:
      break
  }
}

function applyDiagnosticEvent(event: EventEnvelope): void {
  const problems = useProblemsStore.getState()
  switch (event.event.case) {
    case 'diagnosticAdded': {
      const value = event.event.value
      // Idempotent: upsert by stable diagnostic ID. A replayed event
      // produces the same projection.
      const projection: DiagnosticProjection = {
        id: value.diagnosticId,
        severity: mapSeverity(value.severity),
        message: value.message,
        source: value.source,
        targetId: value.targetId || undefined,
      }
      problems.upsert(projection)
      break
    }
    case 'diagnosticRemoved': {
      const value = event.event.value
      problems.remove(value.diagnosticId)
      break
    }
    case 'diagnosticCleared': {
      const value = event.event.value
      if (value.source) {
        problems.clearSource(value.source)
      } else {
        // Clear only backend-sourced diagnostics. Shell-owned diagnostics
        // (engine-session, viewport) are managed by useProblemDiagnostics
        // and must survive a backend clear-all.
        const shellSources = new Set(['engine', 'viewport'])
        for (const diag of problems.diagnostics) {
          if (!shellSources.has(diag.source)) {
            problems.remove(diag.id)
          }
        }
      }
      break
    }
    default:
      break
  }
}

// Applies a single engine event to the shell projection stores. Unknown
// events are ignored — other feature projections subscribe independently.
export function applyShellEvent(event: EventEnvelope): void {
  switch (event.event.case) {
    case 'jobQueued':
    case 'jobStarted':
    case 'jobProgress':
    case 'jobCompleted':
    case 'jobFailed':
    case 'jobCancelled':
      applyJobEvent(event)
      break
    case 'diagnosticAdded':
    case 'diagnosticRemoved':
    case 'diagnosticCleared':
      applyDiagnosticEvent(event)
      break
    default:
      break
  }
}

// Subscribes to the engine event stream and projects job/diagnostic events
// into the Operations and Problems stores. Returns an unsubscribe function
// that cleans up the subscription when the engine session is disposed.
export function subscribeShellEvents(client: EngineClient): () => void {
  return client.onEvent(applyShellEvent)
}
