import { describe, expect, it, vi } from 'vitest'
import { GeocoderService, type FetchFn } from '../src/GeocoderService.js'
import type { LocationSearchRuntimeConfig } from '../src/AppConfig.js'

describe('GeocoderService', () => {
  const testConfig: LocationSearchRuntimeConfig = {
    provider: 'nominatim-test',
    endpoint: 'https://geocoder.test/search',
    minQueryLength: 2,
    maxResults: 5,
    minIntervalMs: 50, // Small interval for fast tests
    maxCacheEntries: 4,
    attribution: '© OpenStreetMap contributors',
    userAgent: 'InfraForge/0.3.0 (https://infraforge.app; contact@infraforge.app)',
  }

  it('rejects short or non-string queries without issuing network requests', async () => {
    const mockFetch = vi.fn() as unknown as FetchFn
    const service = new GeocoderService(testConfig, mockFetch)

    expect(await service.search('')).toEqual([])
    expect(await service.search('a')).toEqual([])
    expect(await service.search(null)).toEqual([])
    expect(await service.search(123)).toEqual([])
    expect(mockFetch).not.toHaveBeenCalled()
  })

  it('sends application User-Agent header and no fake Referer', async () => {
    let capturedUrl = ''
    let capturedHeaders: Record<string, string> = {}

    const mockFetch: FetchFn = async (input, init) => {
      capturedUrl = String(input)
      capturedHeaders = (init?.headers ?? {}) as Record<string, string>
      return {
        ok: true,
        status: 200,
        json: async () => [
          {
            display_name: 'Zürich, Switzerland',
            lat: '47.3769',
            lon: '8.5417',
            boundingbox: ['47.3', '47.4', '8.5', '8.6'],
          },
        ],
      } as unknown as Response
    }

    const service = new GeocoderService(testConfig, mockFetch)
    const results = await service.search('Zurich')

    expect(results).toHaveLength(1)
    expect(results[0]?.displayName).toBe('Zürich, Switzerland')
    expect(results[0]?.lat).toBe(47.3769)
    expect(results[0]?.lon).toBe(8.5417)
    expect(results[0]?.boundingBox).toEqual({
      south: 47.3,
      north: 47.4,
      west: 8.5,
      east: 8.6,
    })

    expect(capturedUrl).toContain('https://geocoder.test/search')
    expect(capturedUrl).toContain('q=Zurich')
    expect(capturedUrl).toContain('format=json')
    expect(capturedUrl).toContain('limit=5')

    // Authentic User-Agent must be present
    expect(capturedHeaders['User-Agent']).toBe(
      'InfraForge/0.1.0 (https://infraforge.app; contact@infraforge.app)',
    )
    // No fake Referer header
    expect(capturedHeaders['Referer']).toBeUndefined()
  })

  it('caches identical normalized queries and does not issue duplicate network requests', async () => {
    const mockFetch = vi.fn(async () => ({
      ok: true,
      status: 200,
      json: async () => [
        {
          display_name: 'Tokyo, Japan',
          lat: '35.6762',
          lon: '139.6503',
        },
      ],
    })) as unknown as FetchFn

    const service = new GeocoderService(testConfig, mockFetch)

    const res1 = await service.search('Tokyo')
    const res2 = await service.search('  tokyo  ') // Case and whitespace normalized

    expect(res1).toHaveLength(1)
    expect(res2).toHaveLength(1)
    expect(mockFetch).toHaveBeenCalledTimes(1)
  })

  it('enforces centralized throttling between consecutive queries', async () => {
    const requestTimes: number[] = []

    const mockFetch: FetchFn = async () => {
      requestTimes.push(Date.now())
      return {
        ok: true,
        status: 200,
        json: async () => [],
      } as unknown as Response
    }

    const service = new GeocoderService({ ...testConfig, minIntervalMs: 60 }, mockFetch)

    const [p1, p2] = await Promise.all([
      service.search('City Alpha'),
      service.search('City Beta'),
    ])

    expect(p1).toEqual([])
    expect(p2).toEqual([])
    expect(requestTimes).toHaveLength(2)
    const gap = requestTimes[1]! - requestTimes[0]!
    expect(gap).toBeGreaterThanOrEqual(50) // Within timing precision
  })

  it('propagates typed HTTP errors cleanly', async () => {
    const mockFetchRateLimited: FetchFn = async () => ({
      ok: false,
      status: 429,
    }) as unknown as Response

    const serviceRateLimited = new GeocoderService(testConfig, mockFetchRateLimited)
    await expect(serviceRateLimited.search('London')).rejects.toThrow('HTTP 429')

    const mockFetch500: FetchFn = async () => ({
      ok: false,
      status: 500,
    }) as unknown as Response

    const service500 = new GeocoderService(testConfig, mockFetch500)
    await expect(service500.search('Paris')).rejects.toThrow('HTTP 500')
  })
})
