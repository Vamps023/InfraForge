// Location search service for the Download Area map (Issue #6).
// Uses a configurable geocoder (default: OpenStreetMap Nominatim) with
// proper attribution. Search results are frontend UX only — they
// reposition the map and are NOT terrain truth.
//
// Nominatim usage policy (https://operations.osmfoundation.org/policies/nominatim/):
// - Must send an identifiable HTTP User-Agent.
// - In Electron desktop execution, search requests route through the main process
//   via IPC (GeocoderService) which sends the official application User-Agent:
//   InfraForge/0.3.0 (https://infraforge.app; contact@infraforge.app).
// - In browser fallback (non-desktop execution), standard fetch is used. Fake Referer
//   headers are not used to bypass User-Agent requirements.
// - Max 1 request per second (enforced centrally in desktop and per-client in web).
// - Results are licensed under ODbL 1.0 (Open Data Commons Open Database License 1.0)
//   by the OpenStreetMap Foundation (OSMF).
//
// Endpoints and rate limits are runtime-configurable via terrainConfig.ts.

import {
  getTerrainSearchConfig,
  setTerrainSearchConfig,
  defaultTerrainSearchConfig,
  type TerrainSearchConfig,
} from './terrainConfig'

export interface SearchResult {
  displayName: string
  lat: number
  lon: number
  boundingBox?: { south: number; north: number; west: number; east: number }
}

export type LocationSearchConfig = TerrainSearchConfig
export const defaultLocationSearchConfig = defaultTerrainSearchConfig
export const getActiveSearchConfig = getTerrainSearchConfig
export const setLocationSearchConfig = setTerrainSearchConfig

export class SearchCancelledError extends Error {
  constructor(message = 'Search request was cancelled.') {
    super(message)
    this.name = 'SearchCancelledError'
    Object.setPrototypeOf(this, SearchCancelledError.prototype)
  }
}

export interface LocationSearchClient {
  search(query: string): Promise<SearchResult[]>
  readonly attribution: string
  cancelPending(): void
}

// Normalize a query for cache lookup: trim, collapse whitespace, lowercase.
function normalizeQuery(query: string): string {
  return query.trim().toLowerCase().replace(/\s+/g, ' ')
}

// Bounded LRU cache for search results.
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
      this.cache.delete(key)
      this.cache.set(key, value)
    }
    return value
  }

  set(key: string, value: SearchResult[]): void {
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

// Browser/Direct implementation using the Nominatim public API.
// Respects usage policy: max 1 req/sec (enforced here),
// proper ODbL 1.0 attribution, bounded cache for identical queries.
// On cancellation or when superseded by a new search, pending requests
// cleanly reject with SearchCancelledError.
export class NominatimLocationSearchClient implements LocationSearchClient {
  private readonly config: LocationSearchConfig
  private readonly cache: BoundedCache
  private lastRequestTime = 0
  private pendingTimer: ReturnType<typeof setTimeout> | null = null
  private pendingReject: ((err: Error) => void) | null = null
  private pendingAbort: AbortController | null = null

  constructor(config: LocationSearchConfig = defaultLocationSearchConfig) {
    this.config = config
    this.cache = new BoundedCache(config.maxCacheEntries)
  }

  get attribution(): string {
    return this.config.attribution
  }

  cancelPending(): void {
    if (this.pendingTimer !== null) {
      clearTimeout(this.pendingTimer)
      this.pendingTimer = null
    }
    if (this.pendingAbort !== null) {
      this.pendingAbort.abort()
      this.pendingAbort = null
    }
    if (this.pendingReject !== null) {
      const reject = this.pendingReject
      this.pendingReject = null
      reject(new SearchCancelledError())
    }
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

    // Cancel any previous pending request before queuing or starting a new one.
    this.cancelPending()

    const now = Date.now()
    const elapsed = now - this.lastRequestTime
    if (elapsed < this.config.minIntervalMs) {
      const waitMs = this.config.minIntervalMs - elapsed
      return new Promise<SearchResult[]>((resolve, reject) => {
        this.pendingReject = reject
        this.pendingTimer = setTimeout(() => {
          this.pendingTimer = null
          this.pendingReject = null
          void this.executeSearch(trimmed, normalized).then(resolve, reject)
        }, waitMs)
      })
    }

    return this.executeSearch(trimmed, normalized)
  }

  private async executeSearch(
    trimmed: string,
    normalized: string,
  ): Promise<SearchResult[]> {
    return new Promise<SearchResult[]>((resolve, reject) => {
      this.pendingReject = reject
      const abort = new AbortController()
      this.pendingAbort = abort

      this.doSearch(trimmed, normalized, abort.signal)
        .then((results) => {
          this.pendingReject = null
          this.pendingAbort = null
          this.lastRequestTime = Date.now()
          resolve(results)
        })
        .catch((err) => {
          this.pendingReject = null
          this.pendingAbort = null
          if (abort.signal.aborted) {
            reject(new SearchCancelledError())
          } else {
            reject(err instanceof Error ? err : new Error(String(err)))
          }
        })
    })
  }

  private async doSearch(
    trimmed: string,
    normalized: string,
    signal?: AbortSignal,
  ): Promise<SearchResult[]> {
    const url = new URL(this.config.endpoint)
    url.searchParams.set('q', trimmed)
    url.searchParams.set('format', 'json')
    url.searchParams.set('limit', String(this.config.maxResults))
    url.searchParams.set('addressdetails', '0')

    const headers: Record<string, string> = {
      Accept: 'application/json',
    }

    const resp = await fetch(url.toString(), { headers, signal })
    if (!resp.ok) {
      throw new Error(`Search failed: HTTP ${resp.status}`)
    }
    const data = (await resp.json()) as Array<{
      display_name: string
      lat: string
      lon: string
      boundingbox?: [string, string, string, string]
    }>
    const results = data.map((item) => ({
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

// Desktop IPC implementation.
// Routes queries to the Electron main process via window.infraforgeDesktop.searchLocation,
// where requests are centralized, throttled across the entire app, and sent with
// the official identifiable User-Agent.
export class DesktopLocationSearchClient implements LocationSearchClient {
  private readonly searchBridge: (query: string) => Promise<unknown>
  private readonly _attribution: string
  private pendingReject: ((err: Error) => void) | null = null
  private currentGen = 0

  constructor(
    attribution: string,
    searchBridge: (query: string) => Promise<unknown>,
  ) {
    this._attribution = attribution
    this.searchBridge = searchBridge
  }

  get attribution(): string {
    return this._attribution
  }

  cancelPending(): void {
    if (this.pendingReject !== null) {
      const reject = this.pendingReject
      this.pendingReject = null
      this.currentGen++
      reject(new SearchCancelledError())
    }
  }

  async search(query: string): Promise<SearchResult[]> {
    const trimmed = query.trim()
    if (trimmed.length < 2) return []

    this.cancelPending()

    const gen = ++this.currentGen
    return new Promise<SearchResult[]>((resolve, reject) => {
      this.pendingReject = reject
      this.searchBridge(trimmed)
        .then((res) => {
          if (gen !== this.currentGen) return
          this.pendingReject = null
          const items = Array.isArray(res) ? (res as SearchResult[]) : []
          resolve(items)
        })
        .catch((err) => {
          if (gen !== this.currentGen) return
          this.pendingReject = null
          reject(err instanceof Error ? err : new Error(String(err)))
        })
    })
  }
}

// Factory: create location search client.
// In desktop environment (Electron), routes through the desktop IPC bridge so
// an identifiable User-Agent is sent according to Nominatim usage policy.
// In browser fallback or tests, uses NominatimLocationSearchClient.
export function createLocationSearchClient(
  config?: LocationSearchConfig,
  desktopBridge?: { searchLocation?: (q: string) => Promise<unknown> },
): LocationSearchClient {
  const cfg = config ?? getActiveSearchConfig()
  const bridge =
    desktopBridge ?? (typeof window !== 'undefined' ? window.infraforgeDesktop : undefined)
  if (bridge && typeof bridge.searchLocation === 'function') {
    return new DesktopLocationSearchClient(cfg.attribution, bridge.searchLocation)
  }
  return new NominatimLocationSearchClient(cfg)
}
