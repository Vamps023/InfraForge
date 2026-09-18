import { create } from 'zustand'
import type { AuthoringToolId } from './authoringToolTypes'
import {
  type Point2D,
  type SnappingConfig,
  type DraftMetrics,
  DEFAULT_SNAPPING_CONFIG,
  resolveSnapping,
  calculateDraftMetrics,
} from './snapService'

export interface ClothoidToolParams {
  startHeadingDeg: number
  startCurvature: number
  endCurvature: number
  length: number
}

export interface RoadConstructionParams {
  name: string
  stickToTerrain: boolean
  datasetId: string
  stationInterval: number
  verticalOffset: number
  positionTolerance: number
  maxCurvature: number | null
}

interface AuthoringDraftState {
  activeTool: AuthoringToolId
  draftPoints: Point2D[]
  hoverPoint: Point2D | null
  snappedHoverPoint: Point2D | null
  snappedTo: 'none' | 'grid' | 'angle' | 'endpoint'
  snappingConfig: SnappingConfig
  clothoidParams: ClothoidToolParams
  roadParams: RoadConstructionParams
  metrics: DraftMetrics

  setTool: (tool: AuthoringToolId) => void
  addDraftPoint: (point: Point2D) => void
  setHoverPoint: (point: Point2D | null, existingEndpoints?: Point2D[]) => void
  removeLastDraftPoint: () => void
  clearDraft: () => void
  updateSnappingConfig: (partial: Partial<SnappingConfig>) => void
  updateClothoidParams: (partial: Partial<ClothoidToolParams>) => void
  updateRoadParams: (partial: Partial<RoadConstructionParams>) => void
}

const initialClothoidParams: ClothoidToolParams = {
  startHeadingDeg: 0,
  startCurvature: 0.0,
  endCurvature: 0.005,
  length: 100.0,
}

const initialRoadParams: RoadConstructionParams = {
  name: '',
  stickToTerrain: false,
  datasetId: '',
  stationInterval: 10.0,
  verticalOffset: 0.1,
  positionTolerance: 0.5,
  maxCurvature: null,
}

const initialMetrics: DraftMetrics = {
  totalLength: 0,
  segmentLength: 0,
  headingDeg: 0,
  deltaE: 0,
  deltaN: 0,
}

export const useAuthoringDraftStore = create<AuthoringDraftState>((set, get) => ({
  activeTool: 'select',
  draftPoints: [],
  hoverPoint: null,
  snappedHoverPoint: null,
  snappedTo: 'none',
  snappingConfig: { ...DEFAULT_SNAPPING_CONFIG },
  clothoidParams: { ...initialClothoidParams },
  roadParams: { ...initialRoadParams },
  metrics: { ...initialMetrics },

  setTool: (tool) => {
    set({
      activeTool: tool,
      draftPoints: [],
      hoverPoint: null,
      snappedHoverPoint: null,
      snappedTo: 'none',
      metrics: { ...initialMetrics },
    })
  },

  addDraftPoint: (point) => {
    const { draftPoints, activeTool, snappingConfig } = get()
    // When adding point, if snapping is active, resolve with previous point as origin
    const prev = draftPoints.length > 0 ? draftPoints[draftPoints.length - 1] : null
    const res = resolveSnapping(point, { origin: prev, config: snappingConfig })
    const newPoints = [...draftPoints, res.point]
    const metrics = calculateDraftMetrics(newPoints, null)

    set({
      draftPoints: newPoints,
      metrics,
    })
  },

  setHoverPoint: (rawPoint, existingEndpoints = []) => {
    if (!rawPoint) {
      set({ hoverPoint: null, snappedHoverPoint: null, snappedTo: 'none' })
      return
    }

    const { draftPoints, snappingConfig } = get()
    const origin = draftPoints.length > 0 ? draftPoints[draftPoints.length - 1] : null
    const res = resolveSnapping(rawPoint, {
      origin,
      endpoints: existingEndpoints,
      config: snappingConfig,
    })

    const metrics = calculateDraftMetrics(draftPoints, res.point)

    set({
      hoverPoint: rawPoint,
      snappedHoverPoint: res.point,
      snappedTo: res.snappedTo,
      metrics,
    })
  },

  removeLastDraftPoint: () => {
    const { draftPoints, hoverPoint } = get()
    if (draftPoints.length === 0) return
    const newPoints = draftPoints.slice(0, -1)
    const metrics = calculateDraftMetrics(newPoints, hoverPoint)
    set({
      draftPoints: newPoints,
      metrics,
    })
  },

  clearDraft: () => {
    set({
      draftPoints: [],
      hoverPoint: null,
      snappedHoverPoint: null,
      snappedTo: 'none',
      metrics: { ...initialMetrics },
    })
  },

  updateSnappingConfig: (partial) => {
    set((state) => ({
      snappingConfig: { ...state.snappingConfig, ...partial },
    }))
  },

  updateClothoidParams: (partial) => {
    set((state) => ({
      clothoidParams: { ...state.clothoidParams, ...partial },
    }))
  },

  updateRoadParams: (partial) => {
    set((state) => ({
      roadParams: { ...state.roadParams, ...partial },
    }))
  },
}))
