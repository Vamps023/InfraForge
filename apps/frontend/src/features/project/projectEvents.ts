import { useProjectStore } from './projectStore'
import { useGeoStore } from '../geo/geoStore'
import { useSelectionStore } from '../../editor/selection/selectionStore'
import type { EngineClient } from '../../lib/engineSession'
import type { EventEnvelope } from '@infraforge/protocol'

// Applies engine-originated project events to the UI projection. Events are
// facts from the canonical owner; the frontend never derives project state
// independently.
export function applyProjectEvent(event: EventEnvelope) {
  const store = useProjectStore.getState()
  switch (event.event.case) {
    case 'projectOpened':
      if (event.event.value.summary) {
        store.setSummary(event.event.value.summary)
      }
      // A different project opened: canonical selections from the previous
      // project must not survive into the new one. Clear selection here so
      // the outliner/inspector resolve against the new canonical world.
      useSelectionStore.getState().clear()
      break
    case 'projectClosed':
      store.clearProject()
      useGeoStore.getState().reset()
      // Project closed: canonical selections are no longer valid. Clear
      // selection so stale IDs do not resolve against a non-existent world.
      useSelectionStore.getState().clear()
      break
    case 'projectRevisionChanged':
      store.patchSummary({ revision: event.event.value.revision })
      break
    case 'projectDirtyStateChanged':
      store.patchSummary({
        dirty: event.event.value.dirty,
        revision: event.event.value.revision,
      })
      break
    case 'georeferenceChanged': {
      const georeference = event.event.value.georeference
      if (georeference?.config) {
        store.patchSummary({
          georeference: georeference.config,
          revision: event.event.value.revision,
        })
        useGeoStore.getState().setInfo(georeference, event.event.value.revision)
      }
      break
    }
    default:
      // Unknown events are ignored by this projection; other feature
      // projections subscribe independently.
      break
  }
}

export function subscribeProjectEvents(client: EngineClient): () => void {
  return client.onEvent(applyProjectEvent)
}
