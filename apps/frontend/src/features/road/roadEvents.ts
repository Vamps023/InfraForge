import type { EventEnvelope } from '@infraforge/protocol'
import type { EngineClient } from '../../lib/engineSession'
import { listRoads } from './roadApi'
import { useRoadStore } from './roadStore'

// Applies engine-originated road events to the UI projection and
// refreshes the road list after road work settles. The scene payload is
// forwarded to the native viewport by the App-level subscriber; the
// frontend itself never interprets mesh content.

// Set after subscribeRoadEvents by the shell so event handlers can push
// the updated scene to the viewport without re-subscribing.
let scenePublisher: (() => void) | null = null

export function setRoadScenePublisher(publisher: (() => void) | null) {
  scenePublisher = publisher
}

async function refreshAfterRoadWork(client: EngineClient): Promise<void> {
  const sessionToken = useRoadStore.getState().sessionToken
  try {
    await listRoads(client)
  } catch {
    // Road list refresh is a projection nicety; failures surface through
    // the next command instead of masking the event that triggered this.
  }
  if (useRoadStore.getState().sessionToken !== sessionToken) {
    return  // Stale response — project has changed
  }
  if (scenePublisher) {
    scenePublisher()
  }
}

export function applyRoadEvent(client: EngineClient, event: EventEnvelope) {
  switch (event.event.case) {
    case 'roadCreated': {
      const created = event.event.value
      // Refresh the full list to get the summary projection.
      void refreshAfterRoadWork(client)
      break
    }
    case 'roadUpdated': {
      void refreshAfterRoadWork(client)
      break
    }
    case 'roadRemoved': {
      const removed = event.event.value
      useRoadStore.getState().removeRoad(removed.roadId)
      void refreshAfterRoadWork(client)
      break
    }
    case 'roadGeometryChanged': {
      void refreshAfterRoadWork(client)
      break
    }
    default:
      // Unknown events are ignored by this projection.
      break
  }
}

export function subscribeRoadEvents(client: EngineClient): () => void {
  const listener = (event: EventEnvelope) => applyRoadEvent(client, event)
  return client.onEvent(listener)
}
