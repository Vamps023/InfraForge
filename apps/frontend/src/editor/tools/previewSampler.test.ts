import { describe, it, expect } from 'vitest'
import { sampleArcPreview, sampleClothoidPreview } from './previewSampler'

describe('previewSampler', () => {
  describe('sampleArcPreview', () => {
    it('accurately samples a CCW left-turning arc within 1e-6 radial error bound', () => {
      const p0 = { easting: 0, northing: 0 }
      const p1 = { easting: 10, northing: 5 }
      const p2 = { easting: 20, northing: 0 }

      const steps = 32
      const points = sampleArcPreview(p0, p1, p2, steps)
      expect(points.length).toBe(steps + 1)

      // Start and end points must match input exactly
      expect(points[0]?.easting).toBeCloseTo(p0.easting, 6)
      expect(points[0]?.northing).toBeCloseTo(p0.northing, 6)
      expect(points[points.length - 1]?.easting).toBeCloseTo(p2.easting, 6)
      expect(points[points.length - 1]?.northing).toBeCloseTo(p2.northing, 6)

      // Circumcenter for (0,0), (10,5), (20,0):
      // Chord from (0,0) to (20,0) has midpoint (10,0), perpendicular bisector x = 10.
      // At x = 10, y = 5: r^2 = 10^2 + (5 - cy)^2 = 10^2 + cy^2 => 25 - 10cy = 0 => cy = -7.5.
      // cx = 10, cy = -7.5, r = sqrt(100 + 56.25) = 12.5.
      const cx = 10
      const cy = -7.5
      const expectedRadius = 12.5

      for (const pt of points) {
        const dist = Math.hypot(pt.easting - cx, pt.northing - cy)
        expect(Math.abs(dist - expectedRadius)).toBeLessThan(1e-6)
      }
    })

    it('accurately samples a CW right-turning arc within 1e-6 radial error bound', () => {
      const p0 = { easting: 0, northing: 0 }
      const p1 = { easting: 10, northing: -5 }
      const p2 = { easting: 20, northing: 0 }

      const steps = 32
      const points = sampleArcPreview(p0, p1, p2, steps)
      expect(points.length).toBe(steps + 1)

      const cx = 10
      const cy = 7.5
      const expectedRadius = 12.5

      for (const pt of points) {
        const dist = Math.hypot(pt.easting - cx, pt.northing - cy)
        expect(Math.abs(dist - expectedRadius)).toBeLessThan(1e-6)
      }
    })

    it('accurately samples a 180-degree semicircle', () => {
      const p0 = { easting: 10, northing: 0 }
      const p1 = { easting: 0, northing: 10 }
      const p2 = { easting: -10, northing: 0 }

      const steps = 32
      const points = sampleArcPreview(p0, p1, p2, steps)
      expect(points.length).toBe(steps + 1)

      // Center is (0,0), radius is 10
      for (const pt of points) {
        const dist = Math.hypot(pt.easting, pt.northing)
        expect(Math.abs(dist - 10)).toBeLessThan(1e-6)
      }

      // Midpoint at index 16 should be (0, 10)
      const mid = points[16]
      expect(mid?.easting).toBeCloseTo(0, 5)
      expect(mid?.northing).toBeCloseTo(10, 5)
    })

    it('accurately samples a shallow large-radius curve', () => {
      const p0 = { easting: 0, northing: 0 }
      const p1 = { easting: 1000, northing: 2 }
      const p2 = { easting: 2000, northing: 0 }

      const points = sampleArcPreview(p0, p1, p2, 64)
      expect(points.length).toBe(65)

      // Center x = 1000, 2cy = 2^2 - 1000^2 / 2 => cy approx -250000
      const cx = 1000
      const cy = (4 - 1000000) / 4
      const r = Math.hypot(p0.easting - cx, p0.northing - cy)

      for (const pt of points) {
        const dist = Math.hypot(pt.easting - cx, pt.northing - cy)
        expect(Math.abs(dist - r)).toBeLessThan(1e-5)
      }
    })

    it('falls back to input points when points are collinear', () => {
      const p0 = { easting: 0, northing: 0 }
      const p1 = { easting: 50, northing: 50 }
      const p2 = { easting: 100, northing: 100 }

      const points = sampleArcPreview(p0, p1, p2)
      expect(points).toEqual([p0, p1, p2])
    })
  })

  describe('sampleClothoidPreview', () => {
    it('returns start point when length <= 0', () => {
      const start = { easting: 10, northing: 20 }
      const res = sampleClothoidPreview(start, 0, 0, 0.01, 0)
      expect(res).toEqual([start])
    })

    it('evaluates a straight line exactly when curvatures are zero', () => {
      const start = { easting: 100, northing: 50 }
      const length = 200
      const heading = Math.PI / 4 // 45 deg

      const points = sampleClothoidPreview(start, heading, 0, 0, length, 32)
      expect(points.length).toBe(33)

      const end = points[points.length - 1]!
      const expectedEndEasting = 100 + length * Math.cos(heading)
      const expectedEndNorthing = 50 + length * Math.sin(heading)

      expect(end.easting).toBeCloseTo(expectedEndEasting, 8)
      expect(end.northing).toBeCloseTo(expectedEndNorthing, 8)
    })

    it('evaluates constant curvature close to analytical circular arc (< 0.005 error bound)', () => {
      const start = { easting: 0, northing: 0 }
      const heading = 0
      const kappa = 0.01 // R = 100
      const length = 50
      const steps = 32

      const points = sampleClothoidPreview(start, heading, kappa, kappa, length, steps)
      const end = points[points.length - 1]!

      // Analytical circle end point:
      // delta_theta = kappa * length = 0.5 rad
      // x = R * sin(delta_theta) = 100 * sin(0.5) = 47.94255386
      // y = R * (1 - cos(delta_theta)) = 100 * (1 - cos(0.5)) = 12.24174381
      const analyticalX = 100 * Math.sin(0.5)
      const analyticalY = 100 * (1 - Math.cos(0.5))

      expect(Math.abs(end.easting - analyticalX)).toBeLessThan(0.005)
      expect(Math.abs(end.northing - analyticalY)).toBeLessThan(0.005)
    })

    it('correctly deflects left for positive end curvature (CCW spiral)', () => {
      const start = { easting: 0, northing: 0 }
      const points = sampleClothoidPreview(start, 0, 0, 0.02, 100, 32)
      const end = points[points.length - 1]!

      expect(end.easting).toBeGreaterThan(0)
      expect(end.northing).toBeGreaterThan(0) // Leftward deflection
    })

    it('correctly deflects right for negative end curvature (CW spiral)', () => {
      const start = { easting: 0, northing: 0 }
      const points = sampleClothoidPreview(start, 0, 0, -0.02, 100, 32)
      const end = points[points.length - 1]!

      expect(end.easting).toBeGreaterThan(0)
      expect(end.northing).toBeLessThan(0) // Rightward deflection
    })

    it('maintains uniform step distances between adjacent preview points', () => {
      const start = { easting: 50, northing: 50 }
      const length = 120
      const steps = 24
      const points = sampleClothoidPreview(start, 0.2, 0.001, 0.005, length, steps)

      const expectedStep = length / steps
      for (let i = 0; i < points.length - 1; i++) {
        const pA = points[i]!
        const pB = points[i + 1]!
        const stepDist = Math.hypot(pB.easting - pA.easting, pB.northing - pA.northing)
        expect(stepDist).toBeCloseTo(expectedStep, 6)
      }
    })
  })
})
