import type { EventEnvelope } from '@infraforge/protocol'
import { useValidationStore } from './validationStore'
import type { EngineClient } from '../../lib/engineSession'

// Applies engine-originated diagnostic events to the UI projection. Events
// are facts from the canonical owner; the frontend never derives diagnostics
// independently. This handles the incremental diagnostic.added/removed/cleared
// events that future revision-aware validation will emit.
export function applyValidationEvent(event: EventEnvelope) {
  const store = useValidationStore.getState()
  switch (event.event.case) {
    case 'diagnosticAdded': {
      const diagnostic = event.event.value.diagnostic
      if (diagnostic) {
        store.setDiagnostics(
          [...store.diagnostics, diagnostic],
          Number(diagnostic.revision),
          false,
        )
      }
      break
    }
    case 'diagnosticRemoved': {
      const removed = event.event.value
      store.setDiagnostics(
        store.diagnostics.filter((d) => d.code !== removed.code),
        Number(removed.revision),
        false,
      )
      break
    }
    case 'diagnosticCleared':
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
