import type { RoadDetails, RoadSummary } from '@infraforge/protocol'

export interface Point2D {
  easting: number
  northing: number
}

export interface SnappingConfig {
  gridSnap: boolean
  gridStep: number
  angleSnap: boolean
  angleStepDeg: number
  endpointSnap: boolean
  snapRadius: number
}

export const DEFAULT_SNAP_RADIUS = 15

export const DEFAULT_SNAPPING_CONFIG: SnappingConfig = {
  gridSnap: false,
  gridStep: 5,
  angleSnap: false,
  angleStepDeg: 15,
  endpointSnap: true,
  snapRadius: DEFAULT_SNAP_RADIUS,
}

export const GRID_STEPS = [1, 2, 5, 10, 20, 50, 100]
export const ANGLE_STEPS = [5, 15, 30, 45, 90]

/** Snap coordinates to the nearest grid step */
export function snapToGrid(point: Point2D, step: number): Point2D {
  if (step <= 0) return { ...point }
  return {
    easting: Math.round(point.easting / step) * step,
    northing: Math.round(point.northing / step) * step,
  }
}

/** Snap vector from origin to current point to angle increments (degrees) */
export function snapToAngle(
  origin: Point2D,
  point: Point2D,
  stepDeg: number,
): { point: Point2D; angleDeg: number } {
  if (stepDeg <= 0) return { point: { ...point }, angleDeg: 0 }

  const de = point.easting - origin.easting
  const dn = point.northing - origin.northing
  const dist = Math.hypot(de, dn)

  if (dist < 1e-6) {
    return { point: { ...point }, angleDeg: 0 }
  }

  let angleRad = Math.atan2(dn, de)
  let angleDeg = (angleRad * 180) / Math.PI
  if (angleDeg < 0) angleDeg += 360

  const snappedDeg = Math.round(angleDeg / stepDeg) * stepDeg
  const snappedRad = (snappedDeg * Math.PI) / 180

  return {
    point: {
      easting: origin.easting + dist * Math.cos(snappedRad),
      northing: origin.northing + dist * Math.sin(snappedRad),
    },
    angleDeg: snappedDeg % 360,
  }
}

export interface EndpointCandidate extends Point2D {
  roadId?: string
  endpointType?: 'start' | 'end' | 'draft'
}

/** Find nearest existing road endpoint within radius */
export function snapToEndpoint(
  point: Point2D,
  endpoints: (Point2D | EndpointCandidate)[],
  radius: number,
): Point2D | null {
  if (!endpoints || endpoints.length === 0 || radius <= 0) return null

  let best: Point2D | null = null
  let bestDist = radius

  for (const ep of endpoints) {
    const d = Math.hypot(ep.easting - point.easting, ep.northing - point.northing)
    if (d <= bestDist) {
      bestDist = d
      best = ep
    }
  }

  return best ? { easting: best.easting, northing: best.northing } : null
}

export interface SnappingResolution {
  point: Point2D
  snappedTo: 'none' | 'grid' | 'angle' | 'endpoint'
  originalPoint: Point2D
  appliedAngleDeg?: number
  snapDistance?: number
}

export interface SnappingContext {
  origin?: Point2D | null
  endpoints?: (Point2D | EndpointCandidate)[]
  config: SnappingConfig
}

/**
 * Priority resolution:
 * 1. Endpoint snap (highest priority for network connectivity)
 * 2. Angle snap (when an origin point exists)
 * 3. Grid snap
 */
export function resolveSnapping(
  rawPoint: Point2D,
  context: SnappingContext,
): SnappingResolution {
  const { config, endpoints = [], origin } = context

  // 1. Endpoint snap
  if (config.endpointSnap && endpoints.length > 0) {
    const ep = snapToEndpoint(rawPoint, endpoints, config.snapRadius)
    if (ep) {
      const dist = Math.hypot(ep.easting - rawPoint.easting, ep.northing - rawPoint.northing)
      return {
        point: ep,
        snappedTo: 'endpoint',
        originalPoint: rawPoint,
        snapDistance: dist,
      }
    }
  }

  let current = { ...rawPoint }
  let snappedTo: 'none' | 'grid' | 'angle' = 'none'
  let appliedAngleDeg: number | undefined

  // 2. Angle snap (relative to previous point)
  if (config.angleSnap && origin) {
    const res = snapToAngle(origin, current, config.angleStepDeg)
    current = res.point
    snappedTo = 'angle'
    appliedAngleDeg = res.angleDeg
  }

  // 3. Grid snap
  if (config.gridSnap) {
    current = snapToGrid(current, config.gridStep)
    snappedTo = 'grid'
  }

  return {
    point: current,
    snappedTo,
    originalPoint: rawPoint,
    appliedAngleDeg,
  }
}

export interface AuthoringPointResolutionOptions {
  previousDraftPoint?: Point2D | null
  endpointCandidates?: (Point2D | EndpointCandidate)[]
  snappingConfig: SnappingConfig
}

/**
 * Canonical draft-point resolution path.
 * Both hover preview and primary click / committed draft point MUST call
 * this exact function so preview and commit coordinates are identical.
 */
export function resolveAuthoringPoint(
  rawPoint: Point2D,
  options: AuthoringPointResolutionOptions,
): SnappingResolution {
  return resolveSnapping(rawPoint, {
    origin: options.previousDraftPoint,
    endpoints: options.endpointCandidates,
    config: options.snappingConfig,
  })
}

export interface RoadSummaryLike {
  roadId?: string
  startEasting?: number
  startNorthing?: number
  endEasting?: number
  endNorthing?: number
  length?: number
}

export interface ExtractEndpointOptions {
  roads?: readonly (RoadSummary | RoadSummaryLike)[] | null
  details?: RoadDetails | null
  draftPoints?: readonly Point2D[]
}

/** Extract candidate endpoints from all project roads and current draft */
export function extractEndpointCandidates(
  source?: readonly (RoadSummary | RoadSummaryLike)[] | RoadDetails | ExtractEndpointOptions | null,
  draftPoints: readonly Point2D[] = [],
): EndpointCandidate[] {
  const candidates: EndpointCandidate[] = []
  const seen = new Set<string>()

  const addCandidate = (c: EndpointCandidate) => {
    const key = `${c.easting.toFixed(4)},${c.northing.toFixed(4)}`
    if (!seen.has(key)) {
      seen.add(key)
      candidates.push(c)
    }
  }

  if (Array.isArray(source)) {
    for (const road of source) {
      if (typeof road.startEasting === 'number' && typeof road.startNorthing === 'number' &&
          Number.isFinite(road.startEasting) && Number.isFinite(road.startNorthing) &&
          (road.startEasting !== 0 || road.startNorthing !== 0 || (typeof road.length === 'number' && road.length > 0))) {
        addCandidate({
          easting: road.startEasting,
          northing: road.startNorthing,
          roadId: road.roadId,
          endpointType: 'start',
        })
      }
      if (typeof road.endEasting === 'number' && typeof road.endNorthing === 'number' &&
          Number.isFinite(road.endEasting) && Number.isFinite(road.endNorthing) &&
          (road.endEasting !== 0 || road.endNorthing !== 0 || (typeof road.length === 'number' && road.length > 0))) {
        addCandidate({
          easting: road.endEasting,
          northing: road.endNorthing,
          roadId: road.roadId,
          endpointType: 'end',
        })
      }
    }
  } else if (source && typeof source === 'object') {
    if ('roads' in source && source.roads) {
      return extractEndpointCandidates(source.roads, source.draftPoints ?? draftPoints)
    }
    if ('controlPoints' in source && Array.isArray((source as RoadDetails).controlPoints)) {
      const details = source as RoadDetails
      if (details.controlPoints.length > 0) {
        const first = details.controlPoints[0]
        const last = details.controlPoints[details.controlPoints.length - 1]
        if (first) addCandidate({ easting: first.easting, northing: first.northing, roadId: details.roadId, endpointType: 'start' })
        if (last) addCandidate({ easting: last.easting, northing: last.northing, roadId: details.roadId, endpointType: 'end' })
      }
    }
  }

  if (draftPoints.length > 0) {
    return candidates.filter((cand) =>
      !draftPoints.some((dp) => Math.hypot(dp.easting - cand.easting, dp.northing - cand.northing) < 1e-4)
    )
  }

  return candidates
}

export interface DraftMetrics {
  totalLength: number
  segmentLength: number
  headingDeg: number
  deltaE: number
  deltaN: number
}

/** Compute live geometric readouts for current draft */
export function calculateDraftMetrics(
  draftPoints: Point2D[],
  hoverPoint: Point2D | null,
): DraftMetrics {
  const points = hoverPoint && draftPoints.length > 0
    ? [...draftPoints, hoverPoint]
    : draftPoints

  if (points.length < 2) {
    return { totalLength: 0, segmentLength: 0, headingDeg: 0, deltaE: 0, deltaN: 0 }
  }

  let totalLength = 0
  for (let i = 0; i < points.length - 1; i++) {
    const pCurr = points[i]
    const pNext = points[i + 1]
    if (pCurr && pNext) {
      totalLength += Math.hypot(
        pNext.easting - pCurr.easting,
        pNext.northing - pCurr.northing,
      )
    }
  }

  const pLast = points[points.length - 1]
  const pPrev = points[points.length - 2]
  if (!pLast || !pPrev) {
    return { totalLength: 0, segmentLength: 0, headingDeg: 0, deltaE: 0, deltaN: 0 }
  }
  const de = pLast.easting - pPrev.easting
  const dn = pLast.northing - pPrev.northing
  const segLen = Math.hypot(de, dn)
  let deg = (Math.atan2(dn, de) * 180) / Math.PI
  if (deg < 0) deg += 360

  return {
    totalLength,
    segmentLength: segLen,
    headingDeg: deg,
    deltaE: de,
    deltaN: dn,
  }
}
