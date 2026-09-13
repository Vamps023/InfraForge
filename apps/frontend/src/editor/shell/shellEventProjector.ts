import { useOperationsStore, type OperationState, type OperationProjection } from '../operations/operationsStore'
import { useProblemsStore, type DiagnosticSeverity } from '../problems/problemsStore'
import type { EventEnvelope } from '@infraforge/protocol'

// Shell-level event projector for operation and diagnostic events. This is a
// single subscriber that parses OperationEvent and DiagnosticEvent cases from
// the EventEnvelope and projects them into the operations and problems stores.
// It does not fabricate events; the stores remain empty until the engine
// emits real operation/diagnostic events.
//
// This projector is intentionally separate from projectEvents.ts (which handles
// project lifecycle events) so each projection owns its own event parsing
// without scattering duplicate logic through components.

function mapOperationState(state: number | undefined): OperationState {
  switch (state) {
    case 1:
      return 'pending'
    case 2:
      return 'running'
    case 3:
      return 'completed'
    case 4:
      return 'failed'
    case 5:
      return 'cancelled'
    default:
      return 'pending'
  }
}

function mapDiagnosticSeverity(severity: number | undefined): DiagnosticSeverity {
  switch (severity) {
    case 1:
      return 'info'
    case 2:
      return 'warning'
    case 3:
      return 'error'
    default:
      return 'info'
  }
}

export function applyShellEvent(event: EventEnvelope): void {
  switch (event.event.case) {
    case 'operation': {
      const op = event.event.value
      const projection: OperationProjection = {
        id: op.operationId,
        name: op.name,
        type: op.source,
        state: mapOperationState(op.state),
        progress: op.progress > 0 ? op.progress : null,
        processed: op.processed > 0 ? Number(op.processed) : undefined,
        total: op.total > 0 ? Number(op.total) : undefined,
        message: op.message || undefined,
        source: op.source,
        cancellable: op.cancellable,
        targetId: op.targetId || undefined,
      }
      useOperationsStore.getState().upsert(projection)
      break
    }
    case 'diagnostic': {
      const diag = event.event.value
      useProblemsStore.getState().upsert({
        id: diag.diagnosticId,
        severity: mapDiagnosticSeverity(diag.severity),
        message: diag.message,
        source: diag.source,
        targetId: diag.targetId || undefined,
      })
      break
    }
    default:
      // Unknown events are ignored by this projection.
      break
  }
}

// Subscribes to engine events and projects operation/diagnostic events into
// the stores. Returns an unsubscribe function. The stores start empty and
// remain empty until the engine emits real events.
export function subscribeShellEvents(
  onEvent: (listener: (event: EventEnvelope) => void) => () => void,
): () => void {
  return onEvent(applyShellEvent)
}
