import { useEffect, useRef, useState, type FormEvent } from 'react'
import {
  Crosshair,
  Download,
  Grid3x3,
  Globe,
  Layers,
  Mountain,
  Search,
} from 'lucide-react'
import { JobState } from '@infraforge/protocol'
import type { EngineClient } from '../../lib/engineSession'
import {
  cancelTerrainJob,
  downloadSelectedTerrain,
  listTerrainSources,
  planTerrainDownload,
} from './terrainApi'
import { useTerrainStore } from './terrainStore'
import { DownloadAreaMap, type GeoBounds, type SelectionTileInfo } from './DownloadAreaMap'
import { createLocationSearchClient } from './locationSearch'
import { initTerrainConfig } from './terrainConfig'

export interface TerrainWorkspaceProps {
  client: EngineClient | null
  viewMode: 'map' | '3d'
  viewportHost?: React.ReactNode
}

const TILE_SIZES = [
  { value: 1000, label: '1 km' },
  { value: 2000, label: '2 km' },
  { value: 4000, label: '4 km' },
  { value: 8000, label: '8 km' },
  { value: 16000, label: '16 km' },
]

export function TerrainWorkspace({
  client,
  viewMode,
  viewportHost,
}: TerrainWorkspaceProps) {
  const jobs = useTerrainStore((state) => state.jobs)
  const activeJob = jobs.find((j) => j.state === JobState.RUNNING)

  // Search & coordinates toolbar
  const [searchText, setSearchText] = useState('')
  const [coordLat, setCoordLat] = useState('')
  const [coordLng, setCoordLng] = useState('')

  // Download Area State
  const [drawnBounds, setDrawnBounds] = useState<GeoBounds | null>(null)
  const [selectedProvider, setSelectedProvider] = useState('aws-terrarium')
  const [tileSizeMeters, setTileSizeMeters] = useState(2000)
  const [selectionGrid, setSelectionGrid] = useState<SelectionTileInfo[]>([])
  const [planning, setPlanning] = useState(false)
  const [planError, setPlanError] = useState<string | null>(null)

  const searchClientRef = useRef(createLocationSearchClient())

  useEffect(() => {
    void initTerrainConfig()
  }, [])

  useEffect(() => {
    if (client) {
      void listTerrainSources(client).catch(() => undefined)
    }
  }, [client])

  // When drawnBounds or tileSizeMeters changes, plan the download
  useEffect(() => {
    if (!client || !drawnBounds) {
      setSelectionGrid([])
      return
    }

    let cancelled = false
    setPlanning(true)
    setPlanError(null)

    void (async () => {
      try {
        const planResult = await planTerrainDownload(
          client,
          selectedProvider,
          drawnBounds,
          tileSizeMeters,
          [],
        )
        if (cancelled) return

        const plan = planResult.plan
        if (plan && plan.selectionTiles) {
          const tiles: SelectionTileInfo[] = plan.selectionTiles.map((t, idx) => ({
            index: idx,
            bounds: {
              west: t.bounds?.west ?? 0,
              south: t.bounds?.south ?? 0,
              east: t.bounds?.east ?? 0,
              north: t.bounds?.north ?? 0,
            },
            selected: true,
          }))
          setSelectionGrid(tiles)
        }
      } catch (err: unknown) {
        if (!cancelled) {
          setPlanError(err instanceof Error ? err.message : 'Failed to plan download area.')
        }
      } finally {
        if (!cancelled) setPlanning(false)
      }
    })()

    return () => {
      cancelled = true
    }
  }, [client, drawnBounds, selectedProvider, tileSizeMeters])

  const handleLocationSearch = async (e: FormEvent) => {
    e.preventDefault()
    if (!searchText.trim()) return
    const results = await searchClientRef.current.search(searchText)
    if (results.length > 0 && results[0]) {
      const top = results[0]
      setCoordLat(top.lat.toFixed(4))
      setCoordLng(top.lon.toFixed(4))
      setDrawnBounds({
        west: top.lon - 0.02,
        east: top.lon + 0.02,
        south: top.lat - 0.02,
        north: top.lat + 0.02,
      })
    }
  }

  const handleApplyCoordinates = () => {
    const lat = Number.parseFloat(coordLat)
    const lng = Number.parseFloat(coordLng)
    if (!Number.isFinite(lat) || !Number.isFinite(lng)) return
    setDrawnBounds({
      west: lng - 0.02,
      east: lng + 0.02,
      south: lat - 0.02,
      north: lat + 0.02,
    })
  }

  const toggleTile = (index: number) => {
    setSelectionGrid((prev) =>
      prev.map((t) => (t.index === index ? { ...t, selected: !t.selected } : t)),
    )
  }

  const selectAllTiles = () => {
    setSelectionGrid((prev) => prev.map((t) => ({ ...t, selected: true })))
  }

  const clearAllTiles = () => {
    setSelectionGrid((prev) => prev.map((t) => ({ ...t, selected: false })))
  }

  const selectedCount = selectionGrid.filter((t) => t.selected).length

  const handleStartDownload = async () => {
    if (!client || !drawnBounds || selectedCount === 0) return
    const selectedIndices = selectionGrid
      .filter((t) => t.selected)
      .map((t) => t.index)

    await downloadSelectedTerrain(
      client,
      selectedProvider,
      drawnBounds,
      tileSizeMeters,
      selectedIndices,
      `Terrain ${new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })}`,
    ).catch(() => undefined)
  }

  const handleCancelJob = async () => {
    if (!client || !activeJob) return
    await cancelTerrainJob(client, activeJob.jobId).catch(() => undefined)
  }

  return (
    <div className="terrain-workspace" role="region" aria-label="Terrain workspace">
      {/* Location Toolbar when in Map view */}
      {viewMode === 'map' && (
        <div className="terrain-location-toolbar">
          <div className="toolbar-pill-label">1 · LOCATION</div>

          <form className="location-search-form" onSubmit={handleLocationSearch}>
            <Search size={14} className="search-icon" />
            <input
              type="text"
              className="toolbar-text-input"
              placeholder="Search location (e.g. Zurich, Switzerland)…"
              value={searchText}
              onChange={(e) => setSearchText(e.target.value)}
            />
            <button type="submit" className="button compact secondary">
              Search
            </button>
          </form>

          <div className="toolbar-divider" />

          <div className="coords-input-group">
            <Crosshair size={14} className="crosshair-icon" />
            <input
              type="text"
              className="toolbar-compact-input"
              placeholder="lat"
              value={coordLat}
              onChange={(e) => setCoordLat(e.target.value)}
            />
            <input
              type="text"
              className="toolbar-compact-input"
              placeholder="lng"
              value={coordLng}
              onChange={(e) => setCoordLng(e.target.value)}
            />
            <button
              type="button"
              className="button compact outline"
              onClick={handleApplyCoordinates}
            >
              Go
            </button>
          </div>

          <div className="toolbar-spacer" />

          <span className="toolbar-hint-text">
            Drag a rectangle on the map to define the terrain download area
          </span>
        </div>
      )}

      {/* Main Area: Map / 3D Canvas + Settings Sidebar */}
      <div className="terrain-main-content">
        <div className="terrain-canvas-area">
          {viewMode === 'map' ? (
            <DownloadAreaMap
              area={drawnBounds}
              onAreaChange={setDrawnBounds}
              selectionTiles={selectionGrid}
              onTileToggle={toggleTile}
              tileSize={tileSizeMeters}
              searchClient={searchClientRef.current}
            />
          ) : (
            <div className="terrain-3d-viewport-host">
              {viewportHost}
            </div>
          )}
        </div>

        {/* Right Settings Sidebar */}
        <aside className="terrain-settings-sidebar">
          {/* Section 2: Tile Grid */}
          <section className="terrain-sidebar-section">
            <h3 className="section-label-row">
              <Grid3x3 size={14} />
              <span>2 · Select Tiles</span>
            </h3>

            <div className="tile-size-button-group">
              {TILE_SIZES.map((size) => (
                <button
                  key={size.value}
                  type="button"
                  className={`tile-size-btn${tileSizeMeters === size.value ? ' active' : ''}`}
                  onClick={() => setTileSizeMeters(size.value)}
                >
                  {size.label}
                </button>
              ))}
            </div>

            {selectionGrid.length > 0 && (
              <div className="tile-selection-summary">
                <div className="summary-numbers">
                  <span>{selectedCount} of {selectionGrid.length} tiles selected</span>
                </div>
                <div className="tile-action-links">
                  <button type="button" onClick={selectAllTiles}>Select All</button>
                  <span>·</span>
                  <button type="button" onClick={clearAllTiles}>Clear</button>
                </div>
              </div>
            )}
          </section>

          {/* Section 3: Elevation Source */}
          <section className="terrain-sidebar-section">
            <h3 className="section-label-row">
              <Mountain size={14} />
              <span>3 · Elevation Source</span>
            </h3>

            <div className="form-field-vertical">
              <label>Provider</label>
              <select
                className="sidebar-select"
                value={selectedProvider}
                onChange={(e) => setSelectedProvider(e.target.value)}
              >
                <option value="aws-terrarium">AWS Terrarium (Free / Global)</option>
                <option value="opentopo-cop30">Copernicus GLO-30 DEM (30m)</option>
                <option value="opentopo-srtmgl1">SRTM GL1 (30m)</option>
                <option value="opentopo-usgs10m">USGS 3DEP (10m - US only)</option>
              </select>
            </div>

            {drawnBounds && (
              <div className="bounds-card">
                <div className="bounds-row">
                  <span>West: {drawnBounds.west.toFixed(4)}°</span>
                  <span>East: {drawnBounds.east.toFixed(4)}°</span>
                </div>
                <div className="bounds-row">
                  <span>South: {drawnBounds.south.toFixed(4)}°</span>
                  <span>North: {drawnBounds.north.toFixed(4)}°</span>
                </div>
              </div>
            )}
          </section>

          {/* Section 4: Download & Preview */}
          <section className="terrain-sidebar-section">
            <h3 className="section-label-row">
              <Globe size={14} />
              <span>4 · Download & Preview</span>
            </h3>

            {activeJob && activeJob.state === JobState.RUNNING ? (
              <div className="job-progress-card">
                <div className="progress-header">
                  <span className="spinner" />
                  <span>
                    Downloading tiles ({Math.round((activeJob.progress ?? 0) * 100)}%)…
                  </span>
                </div>
                <div className="progress-bar-track">
                  <div
                    className="progress-bar-fill"
                    style={{ width: `${Math.round((activeJob.progress ?? 0) * 100)}%` }}
                  />
                </div>
                <button
                  type="button"
                  className="button outline compact"
                  onClick={() => void handleCancelJob()}
                >
                  Cancel Download
                </button>
              </div>
            ) : (
              <button
                type="button"
                className="button primary full-width"
                disabled={!drawnBounds || selectedCount === 0 || planning}
                onClick={() => void handleStartDownload()}
              >
                <Download size={14} />
                <span>
                  {planning
                    ? 'Planning…'
                    : selectedCount > 0
                      ? `Download ${selectedCount} Tile${selectedCount !== 1 ? 's' : ''}`
                      : 'Download & Preview'}
                </span>
              </button>
            )}

            {planError && (
              <p className="sidebar-error-text">{planError}</p>
            )}
          </section>

          {/* Section 5: Export / Local File */}
          <section className="terrain-sidebar-section">
            <h3 className="section-label-row">
              <Layers size={14} />
              <span>5 · Datasets</span>
            </h3>

            <p className="sidebar-help-text">
              Downloaded datasets are automatically conformed and available to the Vulkan renderer and road alignment tools. Switch to 3D to inspect conformed surface.
            </p>
          </section>
        </aside>
      </div>
    </div>
  )
}
