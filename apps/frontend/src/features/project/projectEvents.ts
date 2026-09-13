import { useProjectStore } from './projectStore'
import { useGeoStore } from '../geo/geoStore'
import { useSelectionStore } from '../../editor/selection/selectionStore'
import { useTerrainStore } from '../terrain/terrainStore'
import { fetchTerrainScene, refreshTerrainDatasets, refreshTerrainJobs } from '../terrain/terrainApi'
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
      useTerrainStore.getState().reset()
      void (async () => {
        await refreshTerrainDatasets(client).catch(() => undefined)
        await refreshTerrainJobs(client).catch(() => undefined)
        const scene = await fetchTerrainScene(client).catch(() => null)
        if (scene) {
          window.infraforgeDesktop?.setViewportScene?.(scene as Record<string, unknown>)
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
      // BLOCKER 3: send an empty scene to the viewport so the previous
      // project's GPU terrain is released immediately.
      window.infraforgeDesktop?.setViewportScene?.({
        originEasting: 0,
        originNorthing: 0,
        originHeight: 0,
        tiles: [],
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
