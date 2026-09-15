import { useProjectStore } from './projectStore'
import { useGeoStore } from '../geo/geoStore'
import { useSelectionStore } from '../../editor/selection/selectionStore'
import { useTerrainStore } from '../terrain/terrainStore'
import { fetchTerrainScene, refreshTerrainDatasets, refreshTerrainJobs } from '../terrain/terrainApi'
import { useRoadStore } from '../road/roadStore'
import { listRoads, fetchRoadScene } from '../road/roadApi'
import type { EngineClient } from '../../lib/engineSession'
import type { EventEnvelope } from '@infraforge/protocol'

// Applies engine-originated project events to the UI projection. Events are
// facts from the canonical owner; the frontend never derives project state
// independently.
export function applyProjectEvent(client: EngineClient, event: EventEnvelope) {
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
      // BLOCKER 3: refresh terrain projection state for the newly opened
      // project. Datasets, jobs, and the renderer scene must be fetched
      // immediately so terrain renders after reopen without requiring a
      // terrain job to settle first.
      // BLOCKER 21: beginSession mints a new session token and clears
      // state atomically. The async refresh captures the token and checks
      // it before applying results, so a stale response from project A
      // cannot populate project B's store.
      const sessionToken = useTerrainStore.getState().beginSession()
      const roadSessionToken = useRoadStore.getState().beginSession()
      void (async () => {
        await refreshTerrainDatasets(client).catch(() => undefined)
        // Check if the project changed during the async refresh.
        if (useTerrainStore.getState().sessionToken !== sessionToken) return
        await refreshTerrainJobs(client).catch(() => undefined)
        if (useTerrainStore.getState().sessionToken !== sessionToken) return
        const scene = await fetchTerrainScene(client).catch(() => null)
        if (useTerrainStore.getState().sessionToken !== sessionToken) return
        if (scene) {
          window.infraforgeDesktop?.setViewportScene?.(scene as Record<string, unknown>)
        }
      })()
      // Refresh road projection state for the newly opened project.
      void (async () => {
        await listRoads(client).catch(() => undefined)
        if (useRoadStore.getState().sessionToken !== roadSessionToken) return
        const roadScene = await fetchRoadScene(client).catch(() => null)
        if (useRoadStore.getState().sessionToken !== roadSessionToken) return
        if (roadScene) {
          window.infraforgeDesktop?.setViewportScene?.(roadScene as Record<string, unknown>)
        }
      })()
      break
    case 'projectClosed':
      store.clearProject()
      useGeoStore.getState().reset()
      // Project closed: canonical selections are no longer valid. Clear
      // selection so stale IDs do not resolve against a non-existent world.
      useSelectionStore.getState().clear()
      // Clear terrain projection state: datasets belong to the closed project.
      useTerrainStore.getState().reset()
      // Clear road projection state: roads belong to the closed project.
      useRoadStore.getState().reset()
      // BLOCKER 3: send an empty scene to the viewport so the previous
      // project's GPU terrain is released immediately. BLOCKER 7: use the
      // typed adapter so the empty scene has the full viewport schema
      // (missingTiles, revision) rather than a hand-built partial object.
      window.infraforgeDesktop?.setViewportScene?.({
        originEasting: 0,
        originNorthing: 0,
        originHeight: 0,
        tiles: [],
        missingTiles: 0,
        revision: 0,
      })
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
  return client.onEvent((event) => applyProjectEvent(client, event))
}
