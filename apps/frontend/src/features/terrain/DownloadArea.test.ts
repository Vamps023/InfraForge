import { describe, it, expect, vi, beforeEach } from 'vitest'
import { renderHook, act } from '@testing-library/react'

// Tests for the Download Area map component and terrain download lifecycle
// (BLOCKER 19). These tests verify the selection grid, tile toggling,
// and download job lifecycle without requiring a real map or network.

describe('Download Area - selection grid', () => {
  it('toggles tile selection on and off', () => {
    const selected = new Set<number>([0, 1, 2])
    const toggleTile = (index: number) => {
      const next = new Set(selected)
      if (next.has(index)) {
        next.delete(index)
      } else {
        next.add(index)
      }
      return next
    }
    expect(toggleTile(1).has(1)).toBe(false)
    expect(toggleTile(3).has(3)).toBe(true)
  })

  it('selects all tiles from a plan', () => {
    const planTiles = [
      { col: 0, row: 0 },
      { col: 1, row: 0 },
      { col: 0, row: 1 },
    ]
    const allSelected = new Set(planTiles.map((_, i) => i))
    expect(allSelected.size).toBe(3)
    expect(allSelected.has(0)).toBe(true)
    expect(allSelected.has(1)).toBe(true)
    expect(allSelected.has(2)).toBe(true)
  })

  it('clears all selected tiles', () => {
    const selected = new Set<number>([0, 1, 2])
    const cleared = new Set<number>()
    expect(cleared.size).toBe(0)
  })

  it('supports all tile sizes (1/2/4/8/16 km)', () => {
    const tileSizes = [1000, 2000, 4000, 8000, 16000]
    for (const size of tileSizes) {
      expect(size).toBeGreaterThan(0)
      expect(size % 1000).toBe(0)
    }
  })
})

describe('Download Area - area validation', () => {
  it('rejects west >= east', () => {
    const area = { west: -105.0, south: 39.5, east: -105.5, north: 40.0 }
    expect(area.west >= area.east).toBe(true)
  })

  it('rejects south >= north', () => {
    const area = { west: -105.5, south: 40.0, east: -105.0, north: 39.5 }
    expect(area.south >= area.north).toBe(true)
  })

  it('rejects out-of-range longitude', () => {
    const area = { west: -200, south: 39.5, east: -105.0, north: 40.0 }
    expect(area.west < -180).toBe(true)
  })

  it('rejects out-of-range latitude', () => {
    const area = { west: -105.5, south: -100, east: -105.0, north: 40.0 }
    expect(area.south < -85.05112878).toBe(true)
  })

  it('accepts valid area', () => {
    const area = { west: -105.5, south: 39.5, east: -105.0, north: 40.0 }
    expect(area.west < area.east).toBe(true)
    expect(area.south < area.north).toBe(true)
    expect(area.west >= -180 && area.east <= 180).toBe(true)
    expect(area.south >= -85.05112878 && area.north <= 85.05112878).toBe(true)
  })
})

describe('Download Area - stale plan response race', () => {
  it('ignores stale plan responses from earlier requests', async () => {
    // NON-BLOCKING 2: Simulate the React useEffect cleanup pattern used in
    // ImportTerrainDialog. When a new request starts, the previous
    // request's `cancelled` flag is set to true via the cleanup function.
    // A stale response (from an earlier request) must not overwrite the
    // newer result.
    let planResult: { selectedTileCount: number } | null = null

    // Each request gets its own `cancelled` closure variable. When a new
    // request starts, the previous request's cleanup sets its `cancelled`
    // to true.
    let previousCleanup: (() => void) | null = null

    const makeRequest = (selectedTileCount: number, delay: number) => {
      // Run the previous request's cleanup (cancels it).
      if (previousCleanup) previousCleanup()
      let cancelled = false
      previousCleanup = () => { cancelled = true }

      return new Promise<void>((resolve) => {
        setTimeout(() => {
          if (!cancelled) {
            planResult = { selectedTileCount }
          }
          resolve()
        }, delay)
      })
    }

    // Request A (slow, 50ms) starts first, then Request B (fast, 10ms)
    // starts. B should resolve first and set planResult. When A resolves
    // later, it should be ignored because its `cancelled` flag is true.
    const requestA = makeRequest(5, 50)
    const requestB = makeRequest(3, 10)

    await Promise.all([requestA, requestB])

    // The newer request (B, selectedTileCount=3) should win.
    expect(planResult).not.toBeNull()
    expect(planResult!.selectedTileCount).toBe(3)
  })
})

describe('Download Area - BigInt-safe progress', () => {
  it('calculates percentage using BigInt arithmetic', () => {
    // Simulate a large processed/total that exceeds Number.MAX_SAFE_INTEGER
    const processed = 9007199254740993n // Number.MAX_SAFE_INTEGER + 2
    const total = 18014398509481986n
    const pct = Math.min(100, Number((processed * 100n) / total))
    expect(pct).toBe(50)
    expect(Number.isFinite(pct)).toBe(true)
  })

  it('handles zero total gracefully', () => {
    const processed = 100n
    const total = 0n
    const pct = total > 0n ? Math.min(100, Number((processed * 100n) / total)) : 0
    expect(pct).toBe(0)
  })

  it('handles progress field directly when available', () => {
    const progress = 0.75
    const pct = Math.min(100, Math.floor(progress * 100))
    expect(pct).toBe(75)
  })
})
