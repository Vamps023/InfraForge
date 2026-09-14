import type { LocationSearchRuntimeConfig } from './AppConfig.js'

export interface GeocoderSearchResult {
  displayName: string
  lat: number
  lon: number
  boundingBox?: { south: number; north: number; west: number; east: number }
}

function normalizeQuery(query: string): string {
  return query.trim().toLowerCase().replace(/\s+/g, ' ')
}

class BoundedLruCache<T> {
  private readonly cache = new Map<string, T>()
  private readonly capacity: number

  constructor(capacity: number) {
    this.capacity = Math.max(1, capacity)
  }

  get(key: string): T | undefined {
    const value = this.cache.get(key)
    if (value !== undefined) {
      this.cache.delete(key)
      this.cache.set(key, value)
    }
    return value
  }

  set(key: string, value: T): void {
    if (this.cache.size >= this.capacity) {
      const firstKey = this.cache.keys().next().value
      if (firstKey !== undefined) {
        this.cache.delete(firstKey)
      }
    }
    this.cache.set(key, value)
  }

  clear(): void {
    this.cache.clear()
  }

  get size(): number {
    return this.cache.size
  }
}

export type FetchFn = typeof fetch

export class GeocoderService {
  private readonly config: LocationSearchRuntimeConfig
  private readonly cache: BoundedLruCache<GeocoderSearchResult[]>
  private readonly fetchImpl: FetchFn
  private lastRequestTime = 0
  private requestQueue: Promise<void> = Promise.resolve()

  constructor(config: LocationSearchRuntimeConfig, fetchImpl: FetchFn = globalThis.fetch) {
    this.config = config
    this.cache = new BoundedLruCache(config.maxCacheEntries)
    this.fetchImpl = fetchImpl
  }

  get attribution(): string {
    return this.config.attribution
  }

  async search(query: unknown): Promise<GeocoderSearchResult[]> {
    if (typeof query !== 'string') {
      return []
    }
    const trimmed = query.trim()
    if (trimmed.length < this.config.minQueryLength) {
      return []
    }

    const normalized = normalizeQuery(trimmed)
    const cached = this.cache.get(normalized)
    if (cached !== undefined) {
      return cached
    }

    // Centralized application-wide throttling queue:
    // Serializes outgoing upstream requests and enforces minIntervalMs between them.
    const resultPromise = new Promise<GeocoderSearchResult[]>((resolve, reject) => {
      this.requestQueue = this.requestQueue
        .catch(() => {
          // Keep queue moving even if prior request rejected
        })
        .then(async () => {
          // Double-check cache in case another queued request populated it
          const freshCached = this.cache.get(normalized)
          if (freshCached !== undefined) {
            resolve(freshCached)
            return
          }

          const now = Date.now()
          const elapsed = now - this.lastRequestTime
          if (elapsed < this.config.minIntervalMs) {
            const waitMs = this.config.minIntervalMs - elapsed
            await new Promise((r) => setTimeout(r, waitMs))
          }

          this.lastRequestTime = Date.now()
          try {
            const results = await this.doFetch(trimmed, normalized)
            resolve(results)
          } catch (err) {
            reject(err)
          }
        })
    })

    return resultPromise
  }

  private async doFetch(trimmed: string, normalized: string): Promise<GeocoderSearchResult[]> {
    const url = new URL(this.config.endpoint)
    url.searchParams.set('q', trimmed)
    url.searchParams.set('format', 'json')
    url.searchParams.set('limit', String(this.config.maxResults))
    url.searchParams.set('addressdetails', '0')

    const headers: Record<string, string> = {
      'Accept': 'application/json',
      'User-Agent': this.config.userAgent,
    }

    const resp = await this.fetchImpl(url.toString(), {
      method: 'GET',
      headers,
    })

    if (!resp.ok) {
      if (resp.status === 429) {
        throw new Error('Search failed: HTTP 429 - Geocoder rate limited')
      }
      throw new Error(`Search failed: HTTP ${resp.status}`)
    }

    const data = (await resp.json()) as Array<{
      display_name: string
      lat: string
      lon: string
      boundingbox?: [string, string, string, string]
    }>

    if (!Array.isArray(data)) {
      return []
    }

    const results: GeocoderSearchResult[] = data.map((item) => ({
      displayName: item.display_name,
      lat: parseFloat(item.lat),
      lon: parseFloat(item.lon),
      boundingBox: item.boundingbox
        ? {
            south: parseFloat(item.boundingbox[0]),
            north: parseFloat(item.boundingbox[1]),
            west: parseFloat(item.boundingbox[2]),
            east: parseFloat(item.boundingbox[3]),
          }
        : undefined,
    }))

    this.cache.set(normalized, results)
    return results
  }
}
