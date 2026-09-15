import type { EventEnvelope } from '@infraforge/protocol'
import type { EngineClient } from '../../lib/engineSession'
import { listRoads, getRoad } from './roadApi'
import { useRoadStore } from './roadStore'

// Applies engine-originated road events to the UI projection and
// refreshes the road scene for the viewport. The scene payload is
// forwarded to the native viewport by the App-level subscriber; the
// frontend itself never interprets mesh content.
//
// Blocker 18: events use targeted projection refresh by RoadId where
// possible instead of always calling listRoads(). Full list refresh is
// only used for Created events (where a new road appears) and Removed
// events (where the list needs reordering). Updated and GeometryChanged
// events trigger a targeted getRoad() refresh for the affected road.

// Set after subscribeRoadEvents by the shell so event handlers can push
// the updated scene to the viewport without re-subscribing.
let scenePublisher: (() => void) | null = null

export function setRoadScenePublisher(publisher: (() => void) | null) {
  scenePublisher = publisher
}

async function refreshScene(): Promise<void> {
  if (scenePublisher) {
    scenePublisher()
  }
}

async function refreshRoadById(client: EngineClient, roadId: string): Promise<void> {
  const sessionToken = useRoadStore.getState().sessionToken
  try {
    await getRoad(client, roadId)
  } catch {
    // Road may have been removed; the next event handles cleanup.
  }
  if (useRoadStore.getState().sessionToken !== sessionToken) {
    return  // Stale response — project has changed
  }
  await refreshScene()
}

async function refreshRoadList(client: EngineClient): Promise<void> {
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
  await refreshScene()
}

export function applyRoadEvent(client: EngineClient, event: EventEnvelope) {
  switch (event.event.case) {
    case 'roadCreated': {
      const created = event.event.value
      // New road appeared — refresh the full list to get the summary.
      void refreshRoadList(client)
      break
    }
    case 'roadUpdated': {
      const updated = event.event.value
      // Blocker 18: targeted refresh by RoadId instead of full list.
      if (updated.roadId) {
        void refreshRoadById(client, updated.roadId)
      } else {
        void refreshRoadList(client)
      }
      break
    }
    case 'roadRemoved': {
      const removed = event.event.value
      useRoadStore.getState().removeRoad(removed.roadId)
      // No need to refresh the full list — the road is already removed
      // from the store. Just refresh the scene.
      void refreshScene()
      break
    }
    case 'roadGeometryChanged': {
      const changed = event.event.value
      // Blocker 18: targeted refresh by RoadId instead of full list.
      if (changed.roadId) {
        void refreshRoadById(client, changed.roadId)
      } else {
        void refreshRoadList(client)
      }
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
