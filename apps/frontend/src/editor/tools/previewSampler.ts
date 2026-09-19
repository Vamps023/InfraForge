import type { Point2D } from './snapService'

/** Generate 3-point circular arc samples for smooth preview */
export function sampleArcPreview(p0: Point2D, p1: Point2D, p2: Point2D, numSteps = 32): Point2D[] {
  const d = 2 * (p0.easting * (p1.northing - p2.northing) +
                 p1.easting * (p2.northing - p0.northing) +
                 p2.easting * (p0.northing - p1.northing))

  if (Math.abs(d) < 1e-7) {
    // Degenerate/collinear -> fallback to line through points
    return [p0, p1, p2]
  }

  const p0Sq = p0.easting * p0.easting + p0.northing * p0.northing
  const p1Sq = p1.easting * p1.easting + p1.northing * p1.northing
  const p2Sq = p2.easting * p2.easting + p2.northing * p2.northing

  const cx = (p0Sq * (p1.northing - p2.northing) +
              p1Sq * (p2.northing - p0.northing) +
              p2Sq * (p0.northing - p1.northing)) / d

  const cy = (p0Sq * (p2.easting - p1.easting) +
              p1Sq * (p0.easting - p2.easting) +
              p2Sq * (p1.easting - p0.easting)) / d

  const r = Math.hypot(p0.easting - cx, p0.northing - cy)

  const a0 = Math.atan2(p0.northing - cy, p0.easting - cx)
  const a1 = Math.atan2(p1.northing - cy, p1.easting - cx)
  const a2 = Math.atan2(p2.northing - cy, p2.easting - cx)

  // Determine direction: does arc go CCW or CW from a0 to a2 via a1?
  const normalize = (ang: number) => {
    let a = ang % (2 * Math.PI)
    if (a < 0) a += 2 * Math.PI
    return a
  }

  const diffCcw = (from: number, to: number) => normalize(to - from)

  const d01Ccw = diffCcw(a0, a1)
  const d02Ccw = diffCcw(a0, a2)

  const isCcw = d01Ccw < d02Ccw
  const sweep = isCcw ? d02Ccw : -(2 * Math.PI - d02Ccw)

  const result: Point2D[] = []
  for (let i = 0; i <= numSteps; i++) {
    const t = i / numSteps
    const ang = a0 + t * sweep
    result.push({
      easting: cx + r * Math.cos(ang),
      northing: cy + r * Math.sin(ang),
    })
  }

  return result
}

/** Sample Euler spiral (clothoid) for smooth preview */
export function sampleClothoidPreview(
  start: Point2D,
  headingRad: number,
  startCurvature: number,
  endCurvature: number,
  length: number,
  numSteps = 32,
): Point2D[] {
  if (length <= 0 || numSteps < 2) return [start]

  const result: Point2D[] = [start]
  const ds = length / numSteps
  let currX = start.easting
  let currY = start.northing

  for (let i = 1; i <= numSteps; i++) {
    const sMid = (i - 0.5) * ds
    const t = sMid / length
    const kappa = startCurvature + t * (endCurvature - startCurvature)
    // Heading at sMid: h0 + k0*s + 0.5*((k1-k0)/L)*s^2
    const hMid = headingRad + startCurvature * sMid + 0.5 * ((endCurvature - startCurvature) / length) * sMid * sMid
    currX += Math.cos(hMid) * ds
    currY += Math.sin(hMid) * ds
    result.push({ easting: currX, northing: currY })
  }

  return result
}
