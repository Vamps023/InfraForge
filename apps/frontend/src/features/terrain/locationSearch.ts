// Location search service for the Download Area map (IMPORTANT 6).
// Uses the OpenStreetMap Nominatim geocoder with proper attribution.
// Search results are frontend UX only — they reposition the map and are
// NOT terrain truth.
//
// Usage requirements (https://nominatim.openstreetmap.org/):
// - Must send a valid HTTP Referer or User-Agent.
// - Max 1 request per second (enforced by the client, not the UI).
// - Results are CC BY 2.0 (OpenStreetMap contributors).

export interface SearchResult {
  displayName: string
  lat: number
  lon: number
  boundingBox?: { south: number; north: number; west: number; east: number }
}

// Configuration for a location search provider (Finding 2).
// Allows the endpoint to be switched without changing terrain UI logic.
export interface LocationSearchConfig {
  endpoint: string
  minQueryLength: number
  maxResults: number
  minIntervalMs: number
  maxCacheEntries: number
  attribution: string
}

// Default Nominatim configuration (Finding 2).
export const nominatimConfig: LocationSearchConfig = {
  endpoint: 'https://nominatim.openstreetmap.org/search',
  minQueryLength: 2,
  maxResults: 5,
  minIntervalMs: 1000, // Nominatim policy: max 1 req/sec
  maxCacheEntries: 32,
  attribution: '© OpenStreetMap contributors',
}

export interface LocationSearchClient {
  search(query: string): Promise<SearchResult[]>
  readonly attribution: string
}

// Normalize a query for cache lookup: trim, collapse whitespace, lowercase.
function normalizeQuery(query: string): string {
  return query.trim().toLowerCase().replace(/\s+/g, ' ')
}

// Bounded LRU cache for search results (Finding 2).
class BoundedCache {
  private cache = new Map<string, SearchResult[]>()

  get(key: string): SearchResult[] | undefined {
    const value = this.cache.get(key)
    if (value !== undefined) {
      // Move to end (most recently used) by re-inserting.
      this.cache.delete(key)
      this.cache.set(key, value)
    }
    return value
  }

  set(key: string, value: SearchResult[]): void {
    if (this.cache.size >= nominatimConfig.maxCacheEntries) {
      // Evict oldest (first) entry.
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

// Production implementation using the Nominatim public API.
// Respects usage policy: max 1 req/sec (enforced here, not in the UI),
// proper attribution, bounded cache for identical queries (Finding 2).
export class NominatimLocationSearchClient implements LocationSearchClient {
  private readonly config: LocationSearchConfig
  private readonly cache: BoundedCache
  private lastRequestTime = 0
  private pendingTimer: ReturnType<typeof setTimeout> | null = null
  private pendingResolve: ((value: SearchResult[]) => void) | null = null

  constructor(config: LocationSearchConfig = nominatimConfig) {
    this.config = config
    this.cache = new BoundedCache()
  }

  get attribution(): string {
    return this.config.attribution
  }

  async search(query: string): Promise<SearchResult[]> {
    const trimmed = query.trim()
    if (trimmed.length < this.config.minQueryLength) return []

    // Check cache first (Finding 2).
    const normalized = normalizeQuery(trimmed)
    const cached = this.cache.get(normalized)
    if (cached !== undefined) {
      return cached
    }

    // Enforce client-side throttling: max 1 request per minIntervalMs
    // (Finding 2). This is owned by the search client, not the UI.
    const now = Date.now()
    const elapsed = now - this.lastRequestTime
    if (elapsed < this.config.minIntervalMs) {
      const waitMs = this.config.minIntervalMs - elapsed
      return new Promise<SearchResult[]>((resolve) => {
        // Cancel any previous pending request (Finding 12).
        if (this.pendingTimer !== null) {
          clearTimeout(this.pendingTimer)
        }
        this.pendingResolve = resolve
        this.pendingTimer = setTimeout(() => {
          this.pendingTimer = null
          this.pendingResolve = null
          void this.doSearch(trimmed, normalized).then(resolve)
        }, waitMs)
      })
    }

    this.lastRequestTime = Date.now()
    return this.doSearch(trimmed, normalized)
  }

  private async doSearch(
    trimmed: string,
    normalized: string,
  ): Promise<SearchResult[]> {
    const url = new URL(this.config.endpoint)
    url.searchParams.set('q', trimmed)
    url.searchParams.set('format', 'json')
    url.searchParams.set('limit', String(this.config.maxResults))
    url.searchParams.set('addressdetails', '0')

    const resp = await fetch(url.toString(), {
      headers: {
        'Accept': 'application/json',
      },
    })
    if (!resp.ok) {
      throw new Error(`Search failed: HTTP ${resp.status}`)
    }
    const data = await resp.json() as Array<{
      display_name: string
      lat: string
      lon: string
      boundingbox?: [string, string, string, string]
    }>
    const results = data.map((item) => ({
      displayName: item.display_name,
      lat: parseFloat(item.lat),
      lon: parseFloat(item.lon),
      boundingBox: item.boundingbox ? {
        south: parseFloat(item.boundingbox[0]),
        north: parseFloat(item.boundingbox[1]),
        west: parseFloat(item.boundingbox[2]),
        east: parseFloat(item.boundingbox[3]),
      } : undefined,
    }))

    // Cache the result (Finding 2).
    this.cache.set(normalized, results)
    this.lastRequestTime = Date.now()
    return results
  }

  // Cancel any pending throttled request (Finding 12).
  cancelPending(): void {
    if (this.pendingTimer !== null) {
      clearTimeout(this.pendingTimer)
      this.pendingTimer = null
    }
    if (this.pendingResolve !== null) {
      this.pendingResolve([])
      this.pendingResolve = null
    }
  }
}
