import type { RoadDetails } from '@infraforge/protocol'

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

export const DEFAULT_SNAPPING_CONFIG: SnappingConfig = {
  gridSnap: false,
  gridStep: 5,
  angleSnap: false,
  angleStepDeg: 15,
  endpointSnap: true,
  snapRadius: 10,
}

export const GRID_STEPS = [0.5, 1, 2, 5, 10, 25, 50] as const
export const ANGLE_STEPS = [15, 30, 45, 90] as const

/** Snap coordinates to the nearest grid step */
export function snapToGrid(point: Point2D, step: number): Point2D {
  if (step <= 0) return point
  return {
    easting: Math.round(point.easting / step) * step,
    northing: Math.round(point.northing / step) * step,
  }
}

/** Snap vector from origin to current point to angle increments (degrees) */
export function snapToAngle(
  origin: Point2D,
  current: Point2D,
  angleStepDeg: number,
): { point: Point2D; angleDeg: number; distance: number } {
  const de = current.easting - origin.easting
  const dn = current.northing - origin.northing
  const dist = Math.hypot(de, dn)
  if (dist < 1e-5 || angleStepDeg <= 0) {
    return { point: current, angleDeg: 0, distance: dist }
  }

  let rad = Math.atan2(dn, de)
  let deg = (rad * 180) / Math.PI
  if (deg < 0) deg += 360

  const snappedDeg = Math.round(deg / angleStepDeg) * angleStepDeg
  const snappedRad = (snappedDeg * Math.PI) / 180

  return {
    point: {
      easting: origin.easting + Math.cos(snappedRad) * dist,
      northing: origin.northing + Math.sin(snappedRad) * dist,
    },
    angleDeg: snappedDeg % 360,
    distance: dist,
  }
}

/** Find nearest existing road endpoint within radius */
export function snapToEndpoint(
  point: Point2D,
  endpoints: Point2D[],
  radius: number,
): Point2D | null {
  if (!endpoints.length || radius <= 0) return null

  let nearest: Point2D | null = null
  let bestDistSq = radius * radius

  for (const ep of endpoints) {
    const de = ep.easting - point.easting
    const dn = ep.northing - point.northing
    const distSq = de * de + dn * dn
    if (distSq <= bestDistSq) {
      bestDistSq = distSq
      nearest = ep
    }
  }

  return nearest
}

export interface SnappingResolution {
  point: Point2D
  snappedTo: 'none' | 'grid' | 'angle' | 'endpoint'
}

/**
 * Priority resolution:
 * 1. Endpoint snap (highest priority for network connectivity)
 * 2. Angle snap (when an origin point exists)
 * 3. Grid snap
 */
export function resolveSnapping(
  rawPoint: Point2D,
  options: {
    origin?: Point2D | null
    endpoints?: Point2D[]
    config: SnappingConfig
  },
): SnappingResolution {
  const { origin, endpoints = [], config } = options

  // 1. Endpoint snap
  if (config.endpointSnap && endpoints.length > 0) {
    const ep = snapToEndpoint(rawPoint, endpoints, config.snapRadius)
    if (ep) {
      return { point: ep, snappedTo: 'endpoint' }
    }
  }

  // 2. Angle snap (relative to previous point)
  if (config.angleSnap && origin) {
    const res = snapToAngle(origin, rawPoint, config.angleStepDeg)
    return { point: res.point, snappedTo: 'angle' }
  }

  // 3. Grid snap
  if (config.gridSnap && config.gridStep > 0) {
    return { point: snapToGrid(rawPoint, config.gridStep), snappedTo: 'grid' }
  }

  return { point: rawPoint, snappedTo: 'none' }
}

export interface AuthoringPointResolutionOptions {
  previousDraftPoint?: Point2D | null
  endpointCandidates?: Point2D[]
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

/** Extract candidate endpoints from inspected road details and current draft */
export function extractEndpointCandidates(
  details?: RoadDetails | null,
  draftPoints: Point2D[] = [],
): Point2D[] {
  const candidates: Point2D[] = []
  if (details?.controlPoints && details.controlPoints.length > 0) {
    const first = details.controlPoints[0]
    const last = details.controlPoints[details.controlPoints.length - 1]
    if (first) {
      candidates.push({ easting: first.easting, northing: first.northing })
    }
    if (last && (last.easting !== first?.easting || last.northing !== first?.northing)) {
      candidates.push({ easting: last.easting, northing: last.northing })
    }
  }
  if (draftPoints.length >= 2) {
    const firstDraft = draftPoints[0]
    if (firstDraft) {
      candidates.push(firstDraft)
    }
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
