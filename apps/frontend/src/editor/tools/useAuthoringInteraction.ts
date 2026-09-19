import { useEffect, useRef, useCallback } from 'react'
import type { EngineClient } from '../../lib/engineSession'
import { useAuthoringDraftStore } from './authoringDraftStore'
import type { ViewportInteraction } from './toolStore'
import { useSelectionStore } from '../selection/selectionStore'
import { useRoadStore } from '../../features/road/roadStore'
import { useWorkspaceStore } from '../shell/workspaceStore'
import {
  createStraightRoad,
  createArcRoad,
  createClothoidRoad,
  createRoad,
} from '../../features/road/roadApi'
import { sampleArcPreview, sampleClothoidPreview } from './previewSampler'
import {
  resolveAuthoringPoint,
  extractEndpointCandidates,
} from './snapService'

export interface AuthoringInteractionDeps {
  getClient: () => EngineClient | null
}

export function useAuthoringInteraction(deps: AuthoringInteractionDeps) {
  const activeTool = useAuthoringDraftStore((state) => state.activeTool)
  const draftPoints = useAuthoringDraftStore((state) => state.draftPoints)
  const hoverPoint = useAuthoringDraftStore((state) => state.snappedHoverPoint)
  const clothoidParams = useAuthoringDraftStore((state) => state.clothoidParams)
  const roadParams = useAuthoringDraftStore((state) => state.roadParams)
  const addDraftPoint = useAuthoringDraftStore((state) => state.addDraftPoint)
  const clearDraft = useAuthoringDraftStore((state) => state.clearDraft)
  const setTool = useAuthoringDraftStore((state) => state.setTool)

  const activeToolRef = useRef(activeTool)
  activeToolRef.current = activeTool
  const draftPointsRef = useRef(draftPoints)
  draftPointsRef.current = draftPoints
  const clothoidParamsRef = useRef(clothoidParams)
  clothoidParamsRef.current = clothoidParams
  const roadParamsRef = useRef(roadParams)
  roadParamsRef.current = roadParams

  // Stream preview geometry to native viewport whenever draft points change
  useEffect(() => {
    const tool = activeTool
    const points = draftPoints

    if (tool === 'select' || points.length === 0) {
      window.infraforgeDesktop?.setRoadPreview?.([])
      return
    }

    if (tool === 'road.straight') {
      const all = hoverPoint ? [...points, hoverPoint] : points
      window.infraforgeDesktop?.setRoadPreview?.(all)
    } else if (tool === 'road.arc') {
      const p0 = points[0]
      const p1 = points[1]
      const p2 = points[2]
      if (points.length === 2 && hoverPoint && p0 && p1) {
        const preview = sampleArcPreview(p0, p1, hoverPoint)
        window.infraforgeDesktop?.setRoadPreview?.(preview)
      } else if (points.length >= 3 && p0 && p1 && p2) {
        const preview = sampleArcPreview(p0, p1, p2)
        window.infraforgeDesktop?.setRoadPreview?.(preview)
      } else {
        const all = hoverPoint ? [...points, hoverPoint] : points
        window.infraforgeDesktop?.setRoadPreview?.(all)
      }
    } else if (tool === 'road.clothoid') {
      const start = points[0]
      if (points.length >= 1 && start) {
        let headingRad = (clothoidParams.startHeadingDeg * Math.PI) / 180
        let len = clothoidParams.length

        const p1 = points[1]
        const target = p1 ?? hoverPoint
        if (target) {
          const de = target.easting - start.easting
          const dn = target.northing - start.northing
          headingRad = Math.atan2(dn, de)
          if (clothoidParams.mode === 'interactive') {
            len = Math.hypot(de, dn)
          } else {
            len = clothoidParams.length
          }
        }

        const preview = sampleClothoidPreview(
          start,
          headingRad,
          clothoidParams.startCurvature,
          clothoidParams.endCurvature,
          len,
        )
        window.infraforgeDesktop?.setRoadPreview?.(preview)
      }
    } else if (tool === 'road.polyline') {
      const all = hoverPoint ? [...points, hoverPoint] : points
      window.infraforgeDesktop?.setRoadPreview?.(all)
    }
  }, [activeTool, draftPoints, hoverPoint, clothoidParams])

  // Clear draft on workspace switch away from roads
  useEffect(() => {
    let lastWorkspace = useWorkspaceStore.getState().activeWorkspace
    return useWorkspaceStore.subscribe((state) => {
      if (state.activeWorkspace !== 'roads' && lastWorkspace === 'roads') {
        clearDraft()
        setTool('select')
      }
      lastWorkspace = state.activeWorkspace
    })
  }, [clearDraft, setTool])

  // Reset viewport preview on hook unmount
  useEffect(() => {
    return () => {
      clearDraft()
      window.infraforgeDesktop?.setRoadPreview?.([])
    }
  }, [clearDraft])

  // Viewport interaction handler (primary-click)
  const handleViewportInteraction = useCallback(async (interaction: ViewportInteraction) => {
    if (interaction.kind !== 'primary-click') return

    const tool = activeToolRef.current
    const client = deps.getClient()
    const rawPoint = { easting: interaction.easting, northing: interaction.northing }

    if (tool === 'select') {
      const selectionId = interaction.roadId ? `road:${interaction.roadId}` : null
      useSelectionStore.getState().select(selectionId ? [selectionId] : [])
      return
    }

    if (!client) {
      useRoadStore.getState().setLastError('Engine is not connected')
      return
    }

    const currentPoints = draftPointsRef.current
    const params = roadParamsRef.current
    const details = useRoadStore.getState().details
    const endpointCandidates = extractEndpointCandidates(details, currentPoints)
    const snappingConfig = useAuthoringDraftStore.getState().snappingConfig
    const prevPoint = currentPoints.length > 0 ? currentPoints[currentPoints.length - 1] : null

    // Authoritative resolution: primary click uses the exact same resolution logic as hover
    const resolved = resolveAuthoringPoint(rawPoint, {
      previousDraftPoint: prevPoint,
      endpointCandidates,
      snappingConfig,
    })
    const resolvedPoint = resolved.point

    if (tool === 'road.straight') {
      if (currentPoints.length === 0) {
        addDraftPoint(resolvedPoint, endpointCandidates)
      } else {
        const start = currentPoints[0]
        if (!start) return
        const end = resolvedPoint
        const name = params.name.trim() || `Road ${new Date().toLocaleTimeString()}`
        try {
          await createStraightRoad(client, {
            name,
            startEasting: start.easting,
            startNorthing: start.northing,
            endEasting: end.easting,
            endNorthing: end.northing,
            stickToTerrain: params.stickToTerrain,
            datasetId: params.datasetId,
            stationInterval: params.stationInterval,
            verticalOffset: params.verticalOffset,
          })
          useRoadStore.getState().setLastError(null)
          clearDraft()
        } catch (err) {
          const msg = err instanceof Error ? err.message : String(err)
          useRoadStore.getState().setLastError(msg)
          console.error('Failed to create straight road:', err)
        }
      }
    } else if (tool === 'road.arc') {
      if (currentPoints.length < 2) {
        addDraftPoint(resolvedPoint, endpointCandidates)
      } else {
        const p0 = currentPoints[0]
        const p1 = currentPoints[1]
        if (!p0 || !p1) return
        const p2 = resolvedPoint
        const name = params.name.trim() || `Arc Road ${new Date().toLocaleTimeString()}`
        try {
          await createArcRoad(client, {
            name,
            p0Easting: p0.easting,
            p0Northing: p0.northing,
            p1Easting: p1.easting,
            p1Northing: p1.northing,
            p2Easting: p2.easting,
            p2Northing: p2.northing,
            stickToTerrain: params.stickToTerrain,
            datasetId: params.datasetId,
            stationInterval: params.stationInterval,
            verticalOffset: params.verticalOffset,
          })
          useRoadStore.getState().setLastError(null)
          clearDraft()
        } catch (err) {
          const msg = err instanceof Error ? err.message : String(err)
          useRoadStore.getState().setLastError(msg)
          console.error('Failed to create arc road:', err)
        }
      }
    } else if (tool === 'road.clothoid') {
      const cp = clothoidParamsRef.current
      if (currentPoints.length === 0) {
        addDraftPoint(resolvedPoint, endpointCandidates)
      } else {
        const start = currentPoints[0]
        if (!start) return
        const de = resolvedPoint.easting - start.easting
        const dn = resolvedPoint.northing - start.northing
        const dist = Math.hypot(de, dn)
        const headingRad = dist >= 1e-5 ? Math.atan2(dn, de) : (cp.startHeadingDeg * Math.PI) / 180
        const len = cp.mode === 'interactive' ? (dist > 1e-3 ? dist : cp.length) : cp.length
        const name = params.name.trim() || `Clothoid ${new Date().toLocaleTimeString()}`
        try {
          await createClothoidRoad(client, {
            name,
            startEasting: start.easting,
            startNorthing: start.northing,
            startHeading: headingRad,
            startCurvature: cp.startCurvature,
            endCurvature: cp.endCurvature,
            length: len,
            stickToTerrain: params.stickToTerrain,
            datasetId: params.datasetId,
            stationInterval: params.stationInterval,
            verticalOffset: params.verticalOffset,
          })
          useRoadStore.getState().setLastError(null)
          clearDraft()
        } catch (err) {
          const msg = err instanceof Error ? err.message : String(err)
          useRoadStore.getState().setLastError(msg)
          console.error('Failed to create clothoid road:', err)
        }
      }
    } else if (tool === 'road.polyline') {
      addDraftPoint(resolvedPoint, endpointCandidates)
    }
  }, [deps, addDraftPoint, clearDraft])

  // Explicit commit for polyline (or Enter key)
  const commitCurrentDraft = useCallback(async () => {
    const tool = activeToolRef.current
    const client = deps.getClient()
    const points = draftPointsRef.current
    const params = roadParamsRef.current

    if (!client) {
      useRoadStore.getState().setLastError('Engine is not connected')
      return
    }
    if (points.length < 2) return

    if (tool === 'road.polyline') {
      const name = params.name.trim() || `Polyline Road ${new Date().toLocaleTimeString()}`
      try {
        await createRoad(
          client,
          name,
          points.map((p) => p.easting),
          points.map((p) => p.northing),
          params.positionTolerance,
          [],
          [],
          params.maxCurvature ?? undefined,
        )
        useRoadStore.getState().setLastError(null)
        clearDraft()
      } catch (err) {
        const msg = err instanceof Error ? err.message : String(err)
        useRoadStore.getState().setLastError(msg)
        console.error('Failed to commit polyline road:', err)
      }
    }
  }, [deps, clearDraft])

  return {
    activeTool,
    draftPoints,
    handleViewportInteraction,
    commitCurrentDraft,
    cancelDraft: clearDraft,
  }
}

