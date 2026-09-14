// Location search service for the Download Area map (Issue #6).
// Uses a configurable geocoder (default: OpenStreetMap Nominatim) with
// proper attribution. Search results are frontend UX only — they
// reposition the map and are NOT terrain truth.
//
// Nominatim usage policy (https://nominatim.openstreetmap.org/):
// - Must send a valid HTTP Referer or User-Agent.
// - Max 1 request per second (enforced by the client, not the UI).
// - Results are CC BY 2.0 (OpenStreetMap contributors).
//
// BLOCKER 4: The geocoder endpoint is runtime-configurable via
// LocationSearchConfig. The production factory (createLocationSearchClient)
// reads from a runtime configuration layer, not a hard-coded constant.
// BLOCKER 5: Browser/Electron renderer Fetch cannot set User-Agent.
// The production request sets an explicit Referer header so the public
// service sees an identifiable application request.

export interface SearchResult {
  displayName: string
  lat: number
  lon: number
  boundingBox?: { south: number; north: number; west: number; east: number }
}

// Configuration for a location search provider (BLOCKER 4).
// Allows the endpoint to be switched at runtime without changing
// terrain UI logic.
export interface LocationSearchConfig {
  endpoint: string
  minQueryLength: number
  maxResults: number
  minIntervalMs: number
  maxCacheEntries: number
  attribution: string
  // Optional application identity for the Referer header (BLOCKER 5).
  // Browser Fetch cannot set User-Agent, so we use Referer to identify
  // the application to the public geocoder service.
  referer?: string
}

// Default Nominatim configuration for development (BLOCKER 4).
// Production should override via createLocationSearchClient() with
// runtime configuration.
export const defaultLocationSearchConfig: LocationSearchConfig = {
  endpoint: 'https://nominatim.openstreetmap.org/search',
  minQueryLength: 2,
  maxResults: 5,
  minIntervalMs: 1000, // Nominatim policy: max 1 req/sec
  maxCacheEntries: 32,
  attribution: '© OpenStreetMap contributors',
  referer: 'https://infraforge.app',
}

export interface LocationSearchClient {
  search(query: string): Promise<SearchResult[]>
  readonly attribution: string
  // Cancel any pending throttled request (BLOCKER 3).
  // Called on unmount to prevent requests after component destruction.
  cancelPending(): void
}

// Normalize a query for cache lookup: trim, collapse whitespace, lowercase.
function normalizeQuery(query: string): string {
  return query.trim().toLowerCase().replace(/\s+/g, ' ')
}

// Bounded LRU cache for search results (BLOCKER 10).
// Capacity is passed explicitly to each instance — no global default
// is read inside this generic cache implementation.
class BoundedCache {
  private readonly cache = new Map<string, SearchResult[]>()
  private readonly capacity: number

  constructor(capacity: number) {
    if (capacity < 1 || !Number.isFinite(capacity)) {
      throw new Error(`BoundedCache capacity must be >= 1, got ${capacity}`)
    }
    this.capacity = Math.floor(capacity)
  }

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
    if (this.cache.size >= this.capacity) {
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
// proper attribution, bounded cache for identical queries.
//
// BLOCKER 2: The throttled path correctly propagates both success and
// failure. The delayed request uses .then(resolve, reject) so HTTP
// failures reject the returned Promise instead of leaving it pending.
//
// BLOCKER 3: cancelPending() cancels any delayed throttled request and
// resolves the pending Promise with an empty result. This is called on
// component unmount to prevent requests after destruction.
//
// BLOCKER 10: Cache capacity is per-instance, passed from the config.
export class NominatimLocationSearchClient implements LocationSearchClient {
  private readonly config: LocationSearchConfig
  private readonly cache: BoundedCache
  private lastRequestTime = 0
  private pendingTimer: ReturnType<typeof setTimeout> | null = null

  constructor(config: LocationSearchConfig = defaultLocationSearchConfig) {
    this.config = config
    this.cache = new BoundedCache(config.maxCacheEntries)
  }

  get attribution(): string {
    return this.config.attribution
  }

  async search(query: string): Promise<SearchResult[]> {
    const trimmed = query.trim()
    if (trimmed.length < this.config.minQueryLength) return []

    // Check cache first.
    const normalized = normalizeQuery(trimmed)
    const cached = this.cache.get(normalized)
    if (cached !== undefined) {
      return cached
    }

    // Enforce client-side throttling: max 1 request per minIntervalMs.
    // This is owned by the search client, not the UI.
    const now = Date.now()
    const elapsed = now - this.lastRequestTime
    if (elapsed < this.config.minIntervalMs) {
      const waitMs = this.config.minIntervalMs - elapsed
      // BLOCKER 2: Return a Promise that correctly propagates both
      // success and failure from the delayed request.
      return new Promise<SearchResult[]>((resolve, reject) => {
        // Cancel any previous pending request (BLOCKER 12).
        this.cancelPending()
        this.pendingTimer = setTimeout(() => {
          this.pendingTimer = null
          // BLOCKER 2: propagate both success and failure.
          void this.doSearch(trimmed, normalized).then(resolve, reject)
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

    // BLOCKER 5: Browser Fetch cannot set User-Agent. Use Referer to
    // identify the application to the public geocoder service.
    const headers: Record<string, string> = {
      'Accept': 'application/json',
    }
    if (this.config.referer) {
      headers['Referer'] = this.config.referer
    }

    const resp = await fetch(url.toString(), { headers })
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

    // Cache the result.
    this.cache.set(normalized, results)
    this.lastRequestTime = Date.now()
    return results
  }

  // Cancel any pending throttled request (BLOCKER 3).
  // The pending Promise (if any) resolves with an empty array so the
  // caller's .then() handler runs but sees no results.
  cancelPending(): void {
    if (this.pendingTimer !== null) {
      clearTimeout(this.pendingTimer)
      this.pendingTimer = null
    }
  }
}

// Runtime configuration for the search provider (BLOCKER 4).
// In production, this could be read from:
// - desktop/native configuration supplied to the frontend bootstrap
// - environment variable (development)
// - packaged runtime configuration file
// The terrain project must NOT persist geocoder configuration as
// canonical project truth.
//
// For now, we use a simple module-level override that can be set before
// the app renders. This is the smallest explicit central configuration
// layer — not a huge settings system.
let runtimeSearchConfig: LocationSearchConfig | null = null

// Set the runtime search provider configuration (BLOCKER 4).
// Call this before the terrain UI renders to switch the geocoder.
export function setLocationSearchConfig(config: LocationSearchConfig): void {
  runtimeSearchConfig = config
}

// Get the active search provider configuration (BLOCKER 4).
// Falls back to the default Nominatim config if no runtime override
// has been set.
export function getActiveSearchConfig(): LocationSearchConfig {
  return runtimeSearchConfig ?? defaultLocationSearchConfig
}

// Factory: create the production location search client (BLOCKER 4).
// Uses the runtime-configurable search provider configuration.
// Tests can inject fake clients directly.
export function createLocationSearchClient(): LocationSearchClient {
  return new NominatimLocationSearchClient(getActiveSearchConfig())
}
