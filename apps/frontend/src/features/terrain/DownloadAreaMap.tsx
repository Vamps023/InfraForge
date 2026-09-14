import { useEffect, useRef, useState, useCallback } from 'react'
import {
  MapContainer,
  TileLayer,
  Rectangle,
  useMap,
  useMapEvents,
} from 'react-leaflet'
import L from 'leaflet'
import 'leaflet/dist/leaflet.css'
import { Hand, Square, Search, Crosshair, RotateCw, AlertTriangle } from 'lucide-react'
import {
  type LocationSearchClient,
  type SearchResult,
  SearchCancelledError,
} from './locationSearch'

// Real interactive map for the Download Area UX (Issue #6).
// Provides pan, zoom, rectangular drawing, go-to lat/lon, and a selection
// grid overlay. The frontend owns only interaction/projection — it does
// NOT decode DEMs or create terrain truth.
//
// Map tile provider is configurable via MapTileConfig.
// Default: OpenStreetMap public tiles (ODbL 1.0).
// The map library (Leaflet) is lightweight (~150KB) and well-suited for
// this use case.

export interface GeoBounds {
  west: number
  south: number
  east: number
  north: number
}

export interface SelectionTileInfo {
  index: number
  bounds: GeoBounds
  selected: boolean
}

// Map tile provider configuration (BLOCKER 6).
export interface MapTileConfig {
  url: string
  attribution: string
  maxZoom: number
  provider?: string
  configSource?: 'default' | 'config' | 'env'
  buildMarker?: string
}

// Default OSM public tiles configuration.
// Canonical endpoint: https://tile.openstreetmap.org/{z}/{x}/{y}.png
// Attribution required: © OpenStreetMap contributors (ODbL 1.0).
// Policy: https://operations.osmfoundation.org/policies/tiles/
export const defaultMapTileConfig: MapTileConfig = {
  url: 'https://tile.openstreetmap.org/{z}/{x}/{y}.png',
  attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors',
  maxZoom: 19,
}

interface DownloadAreaMapProps {
  area: GeoBounds | null
  onAreaChange: (area: GeoBounds) => void
  selectionTiles: SelectionTileInfo[]
  onTileToggle: (index: number) => void
  tileSize: number
  searchClient?: LocationSearchClient
  mapTileConfig?: MapTileConfig
  // Token that changes when the host dialog/tab becomes visible so the
  // map can refresh its size (Leaflet needs invalidateSize when mounted
  // in hidden tabs or resized dialogs).
  resizeToken?: number
}

// Convert WebMercator lat/lon to leaflet LatLngBounds.
function toLeafletBounds(b: GeoBounds): L.LatLngBounds {
  return L.latLngBounds(
    L.latLng(b.south, b.west),
    L.latLng(b.north, b.east),
  )
}

function fromLeafletBounds(b: L.LatLngBounds): GeoBounds {
  return {
    west: b.getWest(),
    south: b.getSouth(),
    east: b.getEast(),
    north: b.getNorth(),
  }
}

type MapMode = 'navigate' | 'draw'

// Component that handles map click-and-drag to draw a rectangle.
// Only active in 'draw' mode (separate pan and draw modes).
function DrawHandler({
  onDraw,
  mode,
}: {
  onDraw: (bounds: GeoBounds) => void
  mode: MapMode
}) {
  const [drawing, setDrawing] = useState(false)
  const [start, setStart] = useState<L.LatLng | null>(null)
  const [current, setCurrent] = useState<L.LatLng | null>(null)
  const map = useMapEvents({
    mousedown: (e) => {
      if (mode !== 'draw') return
      setDrawing(true)
      setStart(e.latlng)
      setCurrent(e.latlng)
      map.dragging.disable()
    },
    mousemove: (e) => {
      if (drawing) {
        setCurrent(e.latlng)
      }
    },
    mouseup: (e) => {
      if (drawing && start) {
        const end = e.latlng
        const bounds = L.latLngBounds(start, end)
        if (bounds.isValid() && bounds.getNorth() !== bounds.getSouth()) {
          onDraw(fromLeafletBounds(bounds))
        }
      }
      setDrawing(false)
      setStart(null)
      setCurrent(null)
      map.dragging.enable()
    },
  })
  return drawing && start && current ? (
    <Rectangle
      bounds={L.latLngBounds(start, current)}
      pathOptions={{ color: '#2563eb', fillOpacity: 0.1, dashArray: '5,5' }}
    />
  ) : null
}

// Component that fits the map to given bounds when they change.
function FitBounds({ bounds }: { bounds: GeoBounds | null }) {
  const map = useMap()
  useEffect(() => {
    if (bounds) {
      map.fitBounds(toLeafletBounds(bounds), { padding: [20, 20] })
    }
  }, [map, bounds])
  return null
}

// Refresh Leaflet's internal size when the host container resizes or
// becomes visible. Leaflet caches the container size at init; without an
// explicit invalidateSize() the tile pane can render blank or clipped
// when the map is mounted inside a dialog/hidden tab.
function MapResizer({ token }: { token?: number }) {
  const map = useMap()
  const refresh = useCallback(() => {
    map.invalidateSize()
    requestAnimationFrame(() => map.invalidateSize())
  }, [map])
  useEffect(() => {
    refresh()
  }, [refresh, token])
  useEffect(() => {
    const container = map.getContainer()
    const observer = new ResizeObserver(() => {
      refresh()
    })
    observer.observe(container)
    return () => observer.disconnect()
  }, [map, refresh])
  return null
}

function MapDiagnostics({ onSnapshot }: { onSnapshot: (snapshot: MapSnapshot) => void }) {
  const map = useMap()
  useEffect(() => {
    const report = () => {
      const container = map.getContainer()
      const center = typeof map.getCenter === 'function' ? map.getCenter() : { lat: 0, lng: 0 }
      const zoom = typeof map.getZoom === 'function' ? map.getZoom() : 0
      onSnapshot({
        width: container.clientWidth,
        height: container.clientHeight,
        tileDomCount: container.querySelectorAll('.leaflet-tile').length,
        center: `${center.lat.toFixed(4)},${center.lng.toFixed(4)}`,
        zoom,
      })
    }
    report()
    if (typeof map.whenReady === 'function') map.whenReady(report)
    if (typeof map.on === 'function') map.on('load moveend zoomend resize', report)
    return () => {
      if (typeof map.off === 'function') map.off('load moveend zoomend resize', report)
    }
  }, [map, onSnapshot])
  return null
}

interface MapSnapshot {
  width: number
  height: number
  tileDomCount: number
  center: string
  zoom: number
}

export function DownloadAreaMap({
  area,
  onAreaChange,
  selectionTiles,
  onTileToggle,
  tileSize,
  searchClient,
  mapTileConfig = defaultMapTileConfig,
  resizeToken,
}: DownloadAreaMapProps) {
  const [goToLat, setGoToLat] = useState('')
  const [goToLon, setGoToLon] = useState('')
  const [goToError, setGoToError] = useState<string | null>(null)
  const [goToExpanded, setGoToExpanded] = useState(false)
  const [mode, setMode] = useState<MapMode>('navigate')
  const fitRef = useRef<L.Map | null>(null)

  // Tile load-error state. Leaflet fires `tileerror` when a remote tile
  // image cannot be loaded (network failure, CSP block, bad provider).
  // We surface a visible overlay with a Retry action instead of leaving
  // the user staring at a blank gray pane.
  const [tileErrorCount, setTileErrorCount] = useState(0)
  const [tileLayerKey, setTileLayerKey] = useState(0)
  const [tileLifecycle, setTileLifecycle] = useState<'idle' | 'started' | 'completed' | 'failed'>('idle')
  const [tileFailureReason, setTileFailureReason] = useState<string | null>(null)
  const [tileRequestCount, setTileRequestCount] = useState(0)
  const [tileCompletedCount, setTileCompletedCount] = useState(0)
  const [tileFailedCount, setTileFailedCount] = useState(0)
  const [lastTileDiagnostic, setLastTileDiagnostic] = useState<string | null>(null)
  const [mapSnapshot, setMapSnapshot] = useState<MapSnapshot | null>(null)
  const tileUrl = mapTileConfig.url

  // Reset error state when the tile provider URL changes.
  useEffect(() => {
    setTileErrorCount(0)
    setTileLifecycle('idle')
    setTileFailureReason(null)
    setTileRequestCount(0)
    setTileCompletedCount(0)
    setTileFailedCount(0)
    setLastTileDiagnostic(null)
  }, [tileUrl])

  useEffect(() => window.infraforgeDesktop?.onMapTileDiagnostic?.((diagnostic) => {
    const diagnosticUrl = diagnostic.url
    const configuredPrefix = tileUrl.split('{')[0] ?? tileUrl
    if (diagnosticUrl && !diagnosticUrl.startsWith(configuredPrefix)) return
    setLastTileDiagnostic(`${diagnostic.state} ${diagnostic.statusCode ?? diagnostic.error ?? ''}`.trim())
    console.info('[InfraForge map tile]', diagnostic)
    if (diagnostic.state === 'started') {
      setTileLifecycle('started')
      setTileRequestCount((count) => count + 1)
    }
    if (diagnostic.state === 'completed') {
      setTileCompletedCount((count) => count + 1)
      setTileLifecycle('completed')
      setTileFailureReason(diagnostic.statusCode && diagnostic.statusCode >= 400
        ? `HTTP ${diagnostic.statusCode}` : null)
    }
    if (diagnostic.state === 'failed') {
      setTileFailedCount((count) => count + 1)
      setTileLifecycle('failed')
      setTileFailureReason(diagnostic.error || 'network unavailable')
      setTileErrorCount((count) => count + 1)
    }
  }), [tileUrl])

  // Location search state (Issue #6).
  // Explicit search: user enters a location, presses Search or Enter,
  // and exactly one request is made. No autocomplete on every keystroke.
  // Throttling is owned by the search client, not the UI.
  // Explicit UX states: searching, no-results, error, results.
  const [searchQuery, setSearchQuery] = useState('')
  const [searchResults, setSearchResults] = useState<SearchResult[]>([])
  const [searching, setSearching] = useState(false)
  const [searchError, setSearchError] = useState<string | null>(null)
  const [hasSearched, setHasSearched] = useState(false)
  const searchGenRef = useRef(0)

  // BLOCKER 3: Cancel pending search on unmount to prevent requests
  // after component destruction and avoid React state updates after
  // unmount.
  useEffect(() => {
    return () => {
      searchClient?.cancelPending()
    }
  }, [searchClient])

  const handleSearch = useCallback(() => {
    const query = searchQuery.trim()
    if (query.length < 2 || !searchClient) return

    // Stale response guard: increment generation so old responses are ignored.
    const gen = ++searchGenRef.current
    setSearching(true)
    setSearchError(null)
    setHasSearched(true)
    searchClient.search(query)
      .then((results) => {
        // Stale response guard: ignore if a newer search started.
        if (gen !== searchGenRef.current) return
        setSearchResults(results)
      })
      .catch((err) => {
        if (gen !== searchGenRef.current) return
        if (err instanceof SearchCancelledError) return
        setSearchResults([])
        setSearchError(err instanceof Error ? err.message : 'Search failed')
      })
      .finally(() => {
        if (gen === searchGenRef.current) setSearching(false)
      })
  }, [searchQuery, searchClient])

  const handleSearchResultClick = useCallback((result: SearchResult) => {
    if (result.boundingBox) {
      // Fit the map to the result's bounding box.
      fitRef.current?.fitBounds(
        L.latLngBounds(
          L.latLng(result.boundingBox.south, result.boundingBox.west),
          L.latLng(result.boundingBox.north, result.boundingBox.east),
        ),
        { padding: [20, 20] },
      )
    } else {
      fitRef.current?.panTo([result.lat, result.lon])
    }
    setSearchResults([])
    setSearchQuery('')
  }, [])

  const handleGoTo = useCallback(() => {
    const lat = parseFloat(goToLat)
    const lon = parseFloat(goToLon)
    if (!Number.isFinite(lat) || !Number.isFinite(lon)) {
      setGoToError('Enter numeric latitude and longitude.')
      return
    }
    if (lat < -90 || lat > 90) {
      setGoToError('Latitude must be between -90 and 90.')
      return
    }
    if (lon < -180 || lon > 180) {
      setGoToError('Longitude must be between -180 and 180.')
      return
    }
    setGoToError(null)
    fitRef.current?.panTo([lat, lon])
  }, [goToLat, goToLon])

  const handleDraw = useCallback((bounds: GeoBounds) => {
    onAreaChange(bounds)
    // Automatically return to navigate mode after drawing.
    setMode('navigate')
  }, [onAreaChange])

  const retryTiles = useCallback(() => {
    setTileErrorCount(0)
    setTileLifecycle('idle')
    setTileFailureReason(null)
    setTileLayerKey((k) => k + 1)
  }, [])

  const reportMapSnapshot = useCallback((snapshot: MapSnapshot) => {
    setMapSnapshot(snapshot)
    console.info('[InfraForge map state]', snapshot)
  }, [])

  const hasTileError = tileErrorCount > 0

  return (
    <div className={`download-area-map ${mode === 'draw' ? 'mode-draw' : ''}`}>
      <div className="map-toolbar" role="toolbar" aria-label="Map controls">
        {searchClient && (
          <div className="map-search">
            <input
              type="text"
              className="map-search-input"
              placeholder="Enter a location name..."
              value={searchQuery}
              onChange={(e) => setSearchQuery(e.target.value)}
              onKeyDown={(e) => {
                if (e.key === 'Enter') {
                  e.preventDefault()
                  handleSearch()
                }
              }}
              aria-label="Search a location"
            />
            <button
              type="button"
              className="button secondary map-search-btn"
              onClick={handleSearch}
              disabled={searching || searchQuery.trim().length < 2}
            >
              <Search size={14} /> Search
            </button>
            {searching && <span className="map-search-status" aria-live="polite">Searching...</span>}
            {!searching && hasSearched && !searchError && searchResults.length === 0 && (
              <span className="map-search-no-results" aria-live="polite">No results found</span>
            )}
            {searchError && <span className="map-search-error" aria-live="polite">{searchError}</span>}
            {searchResults.length > 0 && (
              <ul className="map-search-results" role="listbox" aria-label="Search results">
                {searchResults.map((result, i) => (
                  <li key={i} role="option">
                    <button
                      type="button"
                      onClick={() => handleSearchResultClick(result)}
                    >
                      {result.displayName}
                    </button>
                  </li>
                ))}
              </ul>
            )}
          </div>
        )}
        <div className="map-tools" role="group" aria-label="Map mode">
          <button
            type="button"
            className={`map-tool ${mode === 'navigate' ? 'active' : ''}`}
            aria-pressed={mode === 'navigate'}
            onClick={() => setMode('navigate')}
          >
            <Hand size={14} /> Navigate
          </button>
          <button
            type="button"
            className={`map-tool ${mode === 'draw' ? 'active' : ''}`}
            aria-pressed={mode === 'draw'}
            onClick={() => setMode('draw')}
          >
            <Square size={14} /> Draw Area
          </button>
        </div>
        <div className="map-goto">
          <button
            type="button"
            className="map-goto-toggle"
            aria-expanded={goToExpanded}
            aria-controls="map-goto-fields"
            onClick={() => setGoToExpanded((v) => !v)}
          >
            <Crosshair size={12} /> Go to coordinates
          </button>
          {goToExpanded && (
            <div className="map-goto-fields" id="map-goto-fields">
              <input
                type="number"
                placeholder="Lat"
                value={goToLat}
                onChange={(e) => setGoToLat(e.target.value)}
                step={0.01}
                aria-label="Latitude"
              />
              <input
                type="number"
                placeholder="Lon"
                value={goToLon}
                onChange={(e) => setGoToLon(e.target.value)}
                step={0.01}
                aria-label="Longitude"
              />
              <button type="button" className="button secondary" onClick={handleGoTo}>
                Go To
              </button>
              {goToError && <span className="map-goto-error" role="alert">{goToError}</span>}
            </div>
          )}
        </div>
      </div>

      <div className="map-stage">
        <MapContainer
          center={[39.7, -105.2]}
          zoom={10}
          style={{ height: '100%', width: '100%' }}
          ref={(m) => {
            fitRef.current = m
          }}
        >
          <MapResizer token={resizeToken} />
          <MapDiagnostics onSnapshot={reportMapSnapshot} />
          <TileLayer
            key={tileLayerKey}
            attribution={mapTileConfig.attribution}
            url={mapTileConfig.url}
            maxZoom={mapTileConfig.maxZoom}
            eventHandlers={{
              tileloadstart: (event: { tile?: HTMLImageElement }) => {
                setTileLifecycle((state) => state === 'idle' ? 'started' : state)
                setTileRequestCount((count) => count + 1)
                if (event.tile?.src) setLastTileDiagnostic(`leaflet started ${event.tile.src}`)
              },
              tileload: (event: { tile?: HTMLImageElement }) => {
                setTileLifecycle('completed')
                setTileCompletedCount((count) => count + 1)
                if (event.tile?.src) setLastTileDiagnostic(`leaflet loaded ${event.tile.src}`)
              },
              tileerror: (event: { tile?: HTMLImageElement; error?: unknown } = {}) => {
                setTileLifecycle('failed')
                setTileErrorCount((c) => c + 1)
                setTileFailedCount((count) => count + 1)
                const detail = event.error instanceof Error ? event.error.message : 'tileerror'
                if (event.tile?.src) setLastTileDiagnostic(`leaflet failed ${detail}: ${event.tile.src}`)
              },
            }}
          />
          <DrawHandler onDraw={handleDraw} mode={mode} />
          <FitBounds bounds={area} />
          {area && (
            <Rectangle
              bounds={toLeafletBounds(area)}
              pathOptions={{ color: '#2563eb', fillOpacity: 0.05 }}
            />
          )}
          {selectionTiles.map((tile) => (
            <Rectangle
              key={tile.index}
              bounds={toLeafletBounds(tile.bounds)}
              pathOptions={{
                color: tile.selected ? '#16a34a' : '#94a3b8',
                fillColor: tile.selected ? '#16a34a' : '#cbd5e1',
                fillOpacity: tile.selected ? 0.3 : 0.1,
                weight: 1,
              }}
              eventHandlers={{
                click: () => onTileToggle(tile.index),
              }}
            />
          ))}
        </MapContainer>
        {hasTileError && (
          <div className="map-error-overlay" role="alert">
            <div className="map-error-content">
              <AlertTriangle size={20} />
              <div className="map-error-text">
                <strong>Map tiles could not be loaded</strong>
                <span>{tileFailureReason || 'Check your internet connection or map provider configuration.'}</span>
              </div>
              <button type="button" className="button secondary" onClick={retryTiles}>
                <RotateCw size={14} /> Retry
              </button>
            </div>
          </div>
        )}
        {mode === 'draw' && !hasTileError && (
          <div className="map-helper" aria-live="polite">
            Drag on the map to define the working area.
          </div>
        )}
      </div>

      <div className="map-footer">
        <span className="map-footer-item">
          Tile size: {tileSize >= 1000 ? `${tileSize / 1000} km` : `${tileSize} m`}
        </span>
        {searchClient && (
          <span className="map-footer-attribution">{searchClient.attribution}</span>
        )}
        <span className="map-footer-item" title={mapTileConfig.url}>
          Map: {mapTileConfig.provider || 'configured provider'} · {tileLifecycle}
          {mapTileConfig.configSource ? ` · ${mapTileConfig.configSource}` : ''}
          {mapTileConfig.buildMarker ? ` · build ${mapTileConfig.buildMarker}` : ''}
        </span>
        <span className="map-footer-item map-debug" title={lastTileDiagnostic || undefined}>
          Debug: {mapSnapshot ? `${mapSnapshot.width}×${mapSnapshot.height}, ${mapSnapshot.tileDomCount} tiles` : 'map pending'} · {tileRequestCount} req / {tileCompletedCount} ok / {tileFailedCount} err
        </span>
      </div>
    </div>
  )
}
