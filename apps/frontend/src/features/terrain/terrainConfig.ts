// Runtime configuration layer for terrain search and basemap services (Issue #6).
// Loads configuration from the desktop bridge (Electron main process), environment
// variables, or fallbacks without requiring rebuilds.
// Canonical project truth is never stored here; this is runtime network/service configuration.

export interface TerrainSearchConfig {
  endpoint: string
  minQueryLength: number
  maxResults: number
  minIntervalMs: number
  maxCacheEntries: number
  attribution: string
}

export interface TerrainMapTileConfig {
  url: string
  attribution: string
  maxZoom: number
}

export const defaultTerrainSearchConfig: TerrainSearchConfig = {
  endpoint: (typeof import.meta !== 'undefined' && (import.meta as any).env?.VITE_GEOCODER_ENDPOINT) || 'https://nominatim.openstreetmap.org/search',
  minQueryLength: 2,
  maxResults: 5,
  minIntervalMs: 1000, // Nominatim policy: max 1 request per second
  maxCacheEntries: 32,
  attribution: '© OpenStreetMap contributors',
}

export const defaultTerrainMapTileConfig: TerrainMapTileConfig = {
  url: (typeof import.meta !== 'undefined' && (import.meta as any).env?.VITE_TILE_URL) || 'https://tile.openstreetmap.org/{z}/{x}/{y}.png',
  attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors',
  maxZoom: 19,
}

let activeSearchConfig: TerrainSearchConfig = { ...defaultTerrainSearchConfig }
let activeMapTileConfig: TerrainMapTileConfig = { ...defaultTerrainMapTileConfig }
let initialized = false

export function getTerrainSearchConfig(): TerrainSearchConfig {
  return activeSearchConfig
}

export function setTerrainSearchConfig(config: Partial<TerrainSearchConfig>): void {
  activeSearchConfig = { ...activeSearchConfig, ...config }
}

export function getTerrainMapTileConfig(): TerrainMapTileConfig {
  return activeMapTileConfig
}

export function setTerrainMapTileConfig(config: Partial<TerrainMapTileConfig>): void {
  activeMapTileConfig = { ...activeMapTileConfig, ...config }
}

export function resetTerrainConfig(): void {
  activeSearchConfig = { ...defaultTerrainSearchConfig }
  activeMapTileConfig = { ...defaultTerrainMapTileConfig }
  initialized = false
}

export async function initTerrainConfig(): Promise<void> {
  if (initialized) return
  if (typeof window !== 'undefined' && window.infraforgeDesktop?.getRuntimeConfig) {
    try {
      const desktopConfig = await window.infraforgeDesktop.getRuntimeConfig()
      if (desktopConfig?.geocoder) {
        activeSearchConfig = {
          ...activeSearchConfig,
          endpoint: desktopConfig.geocoder.endpoint || activeSearchConfig.endpoint,
          minIntervalMs: desktopConfig.geocoder.minIntervalMs ?? activeSearchConfig.minIntervalMs,
          maxCacheEntries: desktopConfig.geocoder.maxCacheEntries ?? activeSearchConfig.maxCacheEntries,
          attribution: desktopConfig.geocoder.attribution || activeSearchConfig.attribution,
        }
      }
      if (desktopConfig?.mapTile) {
        activeMapTileConfig = {
          ...activeMapTileConfig,
          url: desktopConfig.mapTile.url || activeMapTileConfig.url,
          attribution: desktopConfig.mapTile.attribution || activeMapTileConfig.attribution,
          maxZoom: desktopConfig.mapTile.maxZoom ?? activeMapTileConfig.maxZoom,
        }
      }
    } catch {
      // Fallback configuration remains active if desktop bridge fails
    }
  }
  initialized = true
}
