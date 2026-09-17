import { create } from 'zustand'
import { useToolStore } from '../../editor/tools/toolStore'

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
  beginMove(roadId: string, controlIndex: number, execute?: (easting: number, northing: number) => Promise<void>): void
  beginInsert(roadId: string, insertBeforeIndex: number, execute?: (easting: number, northing: number) => Promise<void>): void
  cancel(): void
}

const initial = { mode: 'idle' as const, name: '', positionTolerance: null,
  maxCurvature: null, points: [] as RoadDraftPoint[], editRoadId: null, controlIndex: null }

export const useRoadToolStore = create<RoadToolState>((set) => ({
  ...initial,
  begin: (name, positionTolerance, maxCurvature) => {
    useToolStore.getState().activateTool({
      id: 'road.drawing',
      workspaceId: 'roads',
      statusHint: 'Click in viewport to place alignment control points. Finish or Cancel in toolbar.',
      cancel: () => useRoadToolStore.getState().cancel(),
      onViewportInteraction: (interaction) => {
        if (interaction.kind === 'primary-click') {
          useRoadToolStore.getState().append({
            easting: interaction.easting,
            northing: interaction.northing,
          })
        }
      },
    })
    set({ mode: 'drawing', name, positionTolerance, maxCurvature, points: [] })
  },
  append: (point) => set((state) => state.mode === 'drawing'
    ? { points: [...state.points, point] } : state),
  beginMove: (editRoadId, controlIndex, execute) => {
    useToolStore.getState().activateTool({
      id: 'road.move-control',
      workspaceId: 'roads',
      statusHint: 'Click in viewport to place selected control point.',
      cancel: () => useRoadToolStore.getState().cancel(),
      onViewportInteraction: (interaction) => {
        if (interaction.kind === 'primary-click') {
          if (execute) {
            void execute(interaction.easting, interaction.northing)
              .finally(() => useRoadToolStore.getState().cancel())
          }
        }
      },
    })
    set({ ...initial, mode: 'move-control', editRoadId, controlIndex })
  },
  beginInsert: (editRoadId, controlIndex, execute) => {
    useToolStore.getState().activateTool({
      id: 'road.insert-control',
      workspaceId: 'roads',
      statusHint: 'Click in viewport to insert new control point.',
      cancel: () => useRoadToolStore.getState().cancel(),
      onViewportInteraction: (interaction) => {
        if (interaction.kind === 'primary-click') {
          if (execute) {
            void execute(interaction.easting, interaction.northing)
              .finally(() => useRoadToolStore.getState().cancel())
          }
        }
      },
    })
    set({ ...initial, mode: 'insert-control', editRoadId, controlIndex })
  },
  cancel: () => {
    if (useToolStore.getState().activeToolId?.startsWith('road.')) {
      useToolStore.getState().clearTool()
    }
    set(initial)
  },
}))
