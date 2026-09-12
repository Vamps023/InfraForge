import type { EventEnvelope } from '@infraforge/protocol'
import { useValidationStore } from './validationStore'
import type { EngineClient } from '../../lib/engineSession'

// Applies engine-originated diagnostic events to the UI projection. Events
// are facts from the canonical owner; the frontend never derives diagnostics
// independently. This handles the incremental diagnostic.added/removed/cleared
// events that the engine broadcasts after each validation run and on
// project lifecycle changes.
//
// Identity: removal uses (code, entities), not code alone, so multi-entity
// diagnostics with the same code can be removed individually.
export function applyValidationEvent(event: EventEnvelope) {
  const store = useValidationStore.getState()
  switch (event.event.case) {
    case 'diagnosticAdded': {
      const diagnostic = event.event.value.diagnostic
      if (diagnostic) {
        store.addDiagnostic(diagnostic)
      }
      break
    }
    case 'diagnosticRemoved': {
      const removed = event.event.value
      store.removeDiagnostic(removed.code, removed.entities)
      break
    }
    case 'diagnosticCleared':
      // The engine cleared diagnostics (project closed or revision changed).
      // The frontend projection must clear too — it never owns this state.
      store.clear()
      break
    default:
      // Unknown events are ignored by this projection.
      break
  }
}

export function subscribeValidationEvents(client: EngineClient): () => void {
  return client.onEvent(applyValidationEvent)
}
