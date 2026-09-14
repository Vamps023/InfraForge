import { describe, expect, it, beforeEach, afterEach } from 'vitest'
import { mkdtemp, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { loadAppRuntimeConfig, defaultAppRuntimeConfig } from '../src/AppConfig.js'

describe('AppConfig', () => {
  const originalEnv = { ...process.env }
  let tempDir: string | null = null

  beforeEach(() => {
    delete process.env.INFRAFORGE_CONFIG_PATH
    delete process.env.INFRAFORGE_GEOCODER_PROVIDER
    delete process.env.INFRAFORGE_GEOCODER_ENDPOINT
    delete process.env.INFRAFORGE_GEOCODER_ATTRIBUTION
    delete process.env.INFRAFORGE_GEOCODER_USER_AGENT
    delete process.env.INFRAFORGE_TILE_PROVIDER
    delete process.env.INFRAFORGE_TILE_URL
    delete process.env.INFRAFORGE_TILE_ATTRIBUTION
    delete process.env.INFRAFORGE_TILE_USER_AGENT
  })

  afterEach(async () => {
    process.env = { ...originalEnv }
    if (tempDir) {
      await rm(tempDir, { recursive: true, force: true }).catch(() => {})
      tempDir = null
    }
  })

  it('loads default configuration when no overrides are present', () => {
    const config = loadAppRuntimeConfig('/non-existent-config.json')
    expect(config.geocoder.provider).toBe('nominatim')
    expect(config.geocoder.endpoint).toBe('https://nominatim.openstreetmap.org/search')
    expect(config.geocoder.userAgent).toContain('InfraForge/')
    expect(config.mapTiles.provider).toBe('osm-public')
    expect(config.mapTiles.url).toBe('https://tile.openstreetmap.org/{z}/{x}/{y}.png')
  })

  it('overrides geocoder and tile config from environment variables', () => {
    process.env.INFRAFORGE_GEOCODER_PROVIDER = 'custom-pelias'
    process.env.INFRAFORGE_GEOCODER_ENDPOINT = 'https://geocoder.internal.corp/v1/search'
    process.env.INFRAFORGE_TILE_PROVIDER = 'custom-raster'
    process.env.INFRAFORGE_TILE_URL = 'https://tiles.internal.corp/{z}/{x}/{y}.webp'

    const config = loadAppRuntimeConfig('/non-existent-config.json')
    expect(config.geocoder.provider).toBe('custom-pelias')
    expect(config.geocoder.endpoint).toBe('https://geocoder.internal.corp/v1/search')
    expect(config.mapTiles.provider).toBe('custom-raster')
    expect(config.mapTiles.url).toBe('https://tiles.internal.corp/{z}/{x}/{y}.webp')
  })

  it('loads configuration from a packaged JSON file', async () => {
    tempDir = await mkdtemp(path.join(tmpdir(), 'infraforge-config-test-'))
    const configPath = path.join(tempDir, 'infraforge.config.json')
    await writeFile(
      configPath,
      JSON.stringify({
        geocoder: {
          provider: 'production-proxy',
          endpoint: 'https://proxy.infraforge.app/geocoder',
        },
        mapTiles: {
          provider: 'production-tiles',
          url: 'https://proxy.infraforge.app/tiles/{z}/{x}/{y}.png',
          maxZoom: 20,
        },
      }),
      'utf-8',
    )

    const config = loadAppRuntimeConfig(configPath)
    expect(config.geocoder.provider).toBe('production-proxy')
    expect(config.geocoder.endpoint).toBe('https://proxy.infraforge.app/geocoder')
    expect(config.mapTiles.provider).toBe('production-tiles')
    expect(config.mapTiles.url).toBe('https://proxy.infraforge.app/tiles/{z}/{x}/{y}.png')
    expect(config.mapTiles.maxZoom).toBe(20)
  })
})
