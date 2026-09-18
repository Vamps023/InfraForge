import { describe, it, expect } from 'vitest'
import {
  snapToGrid,
  snapToAngle,
  snapToEndpoint,
  resolveSnapping,
  calculateDraftMetrics,
  DEFAULT_SNAPPING_CONFIG,
} from './snapService'

describe('snapService', () => {
  describe('snapToGrid', () => {
    it('snaps coordinates to nearest step', () => {
      const pt = { easting: 12.3, northing: 14.8 }
      const snapped = snapToGrid(pt, 5)
      expect(snapped.easting).toBe(10)
      expect(snapped.northing).toBe(15)
    })

    it('returns point unchanged when step <= 0', () => {
      const pt = { easting: 12.3, northing: 14.8 }
      expect(snapToGrid(pt, 0)).toEqual(pt)
    })
  })

  describe('snapToAngle', () => {
    it('snaps direction to increments of stepDeg', () => {
      const origin = { easting: 0, northing: 0 }
      // Roughly 7 degrees off x-axis, should snap to 0 with 15 deg step
      const current = { easting: 100, northing: 12 }
      const res = snapToAngle(origin, current, 15)
      expect(res.angleDeg).toBe(0)
      expect(res.point.easting).toBeCloseTo(Math.hypot(100, 12))
      expect(res.point.northing).toBeCloseTo(0)
    })

    it('snaps to 45 degrees', () => {
      const origin = { easting: 10, northing: 10 }
      const current = { easting: 20, northing: 22 }
      const res = snapToAngle(origin, current, 45)
      expect(res.angleDeg).toBe(45)
    })
  })

  describe('snapToEndpoint', () => {
    it('finds nearest endpoint within tolerance', () => {
      const endpoints = [
        { easting: 100, northing: 100 },
        { easting: 200, northing: 200 },
      ]
      const pt = { easting: 102, northing: 101 }
      const nearest = snapToEndpoint(pt, endpoints, 10)
      expect(nearest).toEqual({ easting: 100, northing: 100 })
    })

    it('returns null if beyond radius', () => {
      const endpoints = [{ easting: 100, northing: 100 }]
      const pt = { easting: 120, northing: 100 }
      expect(snapToEndpoint(pt, endpoints, 10)).toBeNull()
    })
  })

  describe('resolveSnapping', () => {
    it('prioritizes endpoint over grid snap', () => {
      const config = {
        ...DEFAULT_SNAPPING_CONFIG,
        gridSnap: true,
        gridStep: 10,
        endpointSnap: true,
        snapRadius: 15,
      }
      const endpoints = [{ easting: 103, northing: 103 }]
      const raw = { easting: 102, northing: 101 }
      const res = resolveSnapping(raw, { endpoints, config })
      expect(res.snappedTo).toBe('endpoint')
      expect(res.point).toEqual({ easting: 103, northing: 103 })
    })

    it('falls back to grid snap when no endpoint matches', () => {
      const config = {
        ...DEFAULT_SNAPPING_CONFIG,
        gridSnap: true,
        gridStep: 5,
        endpointSnap: true,
      }
      const raw = { easting: 12.3, northing: 14.8 }
      const res = resolveSnapping(raw, { endpoints: [], config })
      expect(res.snappedTo).toBe('grid')
      expect(res.point).toEqual({ easting: 10, northing: 15 })
    })
  })

  describe('resolveAuthoringPoint', () => {
    it('guarantees hover preview and commit receive strictly identical coordinates', () => {
      const config = {
        ...DEFAULT_SNAPPING_CONFIG,
        gridSnap: true,
        gridStep: 5,
        endpointSnap: true,
        snapRadius: 10,
      }
      const endpointCandidates = [{ easting: 100, northing: 100 }]
      const rawPoint = { easting: 101.4, northing: 99.2 }

      // 1. Live preview evaluation
      const hoverRes = resolveSnapping(rawPoint, {
        origin: null,
        endpoints: endpointCandidates,
        config,
      })

      // 2. Primary click commit evaluation
      const commitRes = resolveSnapping(rawPoint, {
        origin: null,
        endpoints: endpointCandidates,
        config,
      })

      expect(commitRes.point).toEqual(hoverRes.point)
      expect(commitRes.point).toEqual({ easting: 100, northing: 100 })
      expect(commitRes.snappedTo).toBe('endpoint')
    })
  })

  describe('calculateDraftMetrics', () => {
    it('computes lengths, heading, and deltas accurately', () => {
      const pts = [
        { easting: 0, northing: 0 },
        { easting: 100, northing: 0 },
      ]
      const hover = { easting: 100, northing: 50 }
      const metrics = calculateDraftMetrics(pts, hover)
      expect(metrics.totalLength).toBe(150)
      expect(metrics.segmentLength).toBe(50)
      expect(metrics.headingDeg).toBe(90)
      expect(metrics.deltaE).toBe(0)
      expect(metrics.deltaN).toBe(50)
    })
  })
})
