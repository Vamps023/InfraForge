import fs from 'node:fs'
import path from 'node:path'

// Runtime configuration for location search provider (geocoder).
export interface LocationSearchRuntimeConfig {
  provider: string
  endpoint: string
  minQueryLength: number
  maxResults: number
  minIntervalMs: number
  maxCacheEntries: number
  attribution: string
  userAgent: string
}

// Runtime configuration for map tile provider.
export interface MapTileRuntimeConfig {
  provider: string
  url: string
  attribution: string
  maxZoom: number
  userAgent: string
}

// Complete application runtime configuration.
export interface AppRuntimeConfig {
  geocoder: LocationSearchRuntimeConfig
  mapTiles: MapTileRuntimeConfig
  diagnostics: {
    configSource: 'default' | 'config' | 'env'
    buildMarker: string
  }
}

// Default development configuration.
// Public Nominatim and public OSM tiles are development/default fallbacks.
// Production deployments must configure dedicated services via runtime config JSON
// or environment overrides.
export const defaultAppRuntimeConfig: AppRuntimeConfig = Object.freeze({
  geocoder: Object.freeze({
    provider: 'nominatim',
    endpoint: 'https://nominatim.openstreetmap.org/search',
    minQueryLength: 2,
    maxResults: 5,
    minIntervalMs: 1000,
    maxCacheEntries: 32,
    attribution: '© OpenStreetMap contributors',
    userAgent: 'InfraForge/0.3.0 (https://infraforge.app; contact@infraforge.app)',
  }),
  mapTiles: Object.freeze({
    provider: 'osm-public',
    url: 'https://tile.openstreetmap.org/{z}/{x}/{y}.png',
    attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors',
    maxZoom: 19,
    userAgent: 'InfraForge/0.3.0 (https://infraforge.app; contact@infraforge.app)',
  }),
  diagnostics: Object.freeze({
    configSource: 'default',
    buildMarker: 'development',
  }),
})

export function loadAppRuntimeConfig(explicitPath?: string): AppRuntimeConfig {
  const config: AppRuntimeConfig = {
    geocoder: { ...defaultAppRuntimeConfig.geocoder },
    mapTiles: { ...defaultAppRuntimeConfig.mapTiles },
    diagnostics: {
      configSource: 'default',
      buildMarker: process.env.INFRAFORGE_BUILD_SHA || process.env.GITHUB_SHA || 'development',
    },
  }

  // 1. Load from config file if present
  const configFilePath =
    explicitPath ||
    process.env.INFRAFORGE_CONFIG_PATH ||
    path.resolve(process.cwd(), 'infraforge.config.json')

  if (fs.existsSync(configFilePath)) {
    try {
      const raw = fs.readFileSync(configFilePath, 'utf-8')
      const parsed = JSON.parse(raw) as Partial<AppRuntimeConfig>
      if (parsed.geocoder) {
        config.geocoder = { ...config.geocoder, ...parsed.geocoder }
      }
      if (parsed.mapTiles) {
        config.mapTiles = { ...config.mapTiles, ...parsed.mapTiles }
      }
      config.diagnostics.configSource = 'config'
    } catch {
      // If file exists but fails to parse, preserve defaults
    }
  }

  // 2. Environment variable overrides (useful in containerized, CI, or dev environments)
  if (process.env.INFRAFORGE_GEOCODER_PROVIDER) {
    config.geocoder.provider = process.env.INFRAFORGE_GEOCODER_PROVIDER
  }
  if (process.env.INFRAFORGE_GEOCODER_ENDPOINT) {
    config.geocoder.endpoint = process.env.INFRAFORGE_GEOCODER_ENDPOINT
  }
  if (process.env.INFRAFORGE_GEOCODER_ATTRIBUTION) {
    config.geocoder.attribution = process.env.INFRAFORGE_GEOCODER_ATTRIBUTION
  }
  if (process.env.INFRAFORGE_GEOCODER_USER_AGENT) {
    config.geocoder.userAgent = process.env.INFRAFORGE_GEOCODER_USER_AGENT
  }

  if (process.env.INFRAFORGE_TILE_PROVIDER) {
    config.mapTiles.provider = process.env.INFRAFORGE_TILE_PROVIDER
  }
  if (process.env.INFRAFORGE_TILE_URL) {
    config.mapTiles.url = process.env.INFRAFORGE_TILE_URL
  }
  if (process.env.INFRAFORGE_TILE_ATTRIBUTION) {
    config.mapTiles.attribution = process.env.INFRAFORGE_TILE_ATTRIBUTION
  }
  if (process.env.INFRAFORGE_TILE_USER_AGENT) {
    config.mapTiles.userAgent = process.env.INFRAFORGE_TILE_USER_AGENT
  }

  if (Object.keys(process.env).some((key) => key.startsWith('INFRAFORGE_GEOCODER_') || key.startsWith('INFRAFORGE_TILE_'))) {
    config.diagnostics.configSource = 'env'
  }

  return config
}
