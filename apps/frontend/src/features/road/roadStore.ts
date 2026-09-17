import { create } from 'zustand'
import type { RoadSummary, RoadDetails } from '@infraforge/protocol'

// Frontend projection of road state. Everything here mirrors engine
// truth (command results and events); the frontend never owns canonical
// road geometry and never receives render meshes through this store.
//
// BLOCKER 21: A session token guards against stale async responses from
// a previous project overwriting the current project's state. Each
// project open/switch mints a new token; async responses check the token
// before applying results.
interface RoadState {
  roads: RoadSummary[]
  selectedRoadId: string | null
  details: RoadDetails | null
  lastError: string | null
  sessionToken: number
  setRoads(roads: RoadSummary[]): void
  selectRoad(roadId: string | null): void
  setDetails(details: RoadDetails | null): void
  applyRoadProjection(details: RoadDetails, summary: RoadSummary): void
  upsertRoad(road: RoadSummary): void
  removeRoad(roadId: string): void
  setLastError(error: string | null): void
  beginSession(): number
  reset(): void
}

export const useRoadStore = create<RoadState>((set) => ({
  roads: [],
  selectedRoadId: null,
  details: null,
  lastError: null,
  sessionToken: 0,
  setRoads: (roads) =>
    set((state) => ({
      roads,
      selectedRoadId: roads.some((r) => r.roadId === state.selectedRoadId)
        ? state.selectedRoadId
        : null,
    })),
  selectRoad: (roadId) => set({ selectedRoadId: roadId }),
  setDetails: (details) => set({ details }),
  applyRoadProjection: (details, summary) =>
    set((state) => {
      const currentSummary = state.roads.find((road) => road.roadId === summary.roadId)
      const currentDetails = state.details?.roadId === details.roadId ? state.details : null
      if ((currentSummary && currentSummary.revision > summary.revision) ||
          (currentDetails && currentDetails.revision > details.revision)) {
        return {}
      }
      const index = state.roads.findIndex((road) => road.roadId === summary.roadId)
      const roads = index === -1
        ? [...state.roads, summary]
        : state.roads.map((road, i) => (i === index ? summary : road))
      return {
        roads,
        details: state.selectedRoadId === details.roadId || currentDetails ? details : state.details,
      }
    }),
  upsertRoad: (road) =>
    set((state) => {
      const index = state.roads.findIndex((r) => r.roadId === road.roadId)
      const roads =
        index === -1
          ? [...state.roads, road]
          : state.roads.map((r, i) => (i === index ? road : r))
      return { roads }
    }),
  removeRoad: (roadId) =>
    set((state) => ({
      roads: state.roads.filter((r) => r.roadId !== roadId),
      selectedRoadId: state.selectedRoadId === roadId ? null : state.selectedRoadId,
      details: state.details?.roadId === roadId ? null : state.details,
    })),
  setLastError: (lastError) => set({ lastError }),
  beginSession: () => {
    let token = 0
    set((state) => {
      token = state.sessionToken + 1
      return {
        sessionToken: token,
        roads: [],
        selectedRoadId: null,
        details: null,
        lastError: null,
      }
    })
    return token
  },
  reset: () =>
    set({
      roads: [],
      selectedRoadId: null,
      details: null,
      lastError: null,
      sessionToken: 0,
    }),
}))
