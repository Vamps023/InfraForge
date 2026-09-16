import { create } from 'zustand'

export interface RoadDraftPoint { easting: number; northing: number }

interface RoadToolState {
  mode: 'idle' | 'drawing' | 'move-control' | 'insert-control'
  name: string
  positionTolerance: number | null
  maxCurvature: number | null
  points: RoadDraftPoint[]
  editRoadId: string | null
  controlIndex: number | null
  begin(name: string, positionTolerance: number, maxCurvature: number | null): void
  append(point: RoadDraftPoint): void
  beginMove(roadId: string, controlIndex: number): void
  beginInsert(roadId: string, insertBeforeIndex: number): void
  cancel(): void
}

const initial = { mode: 'idle' as const, name: '', positionTolerance: null,
  maxCurvature: null, points: [] as RoadDraftPoint[], editRoadId: null, controlIndex: null }

export const useRoadToolStore = create<RoadToolState>((set) => ({
  ...initial,
  begin: (name, positionTolerance, maxCurvature) =>
    set({ mode: 'drawing', name, positionTolerance, maxCurvature, points: [] }),
  append: (point) => set((state) => state.mode === 'drawing'
    ? { points: [...state.points, point] } : state),
  beginMove: (editRoadId, controlIndex) => set({ ...initial, mode: 'move-control', editRoadId, controlIndex }),
  beginInsert: (editRoadId, controlIndex) => set({ ...initial, mode: 'insert-control', editRoadId, controlIndex }),
  cancel: () => set(initial),
}))
