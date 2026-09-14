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

export function DownloadAreaMap({
  area,
  onAreaChange,
  selectionTiles,
  onTileToggle,
  tileSize,
  searchClient,
  mapTileConfig = defaultMapTileConfig,
}: DownloadAreaMapProps) {
  const [goToLat, setGoToLat] = useState('')
  const [goToLon, setGoToLon] = useState('')
  const [mode, setMode] = useState<MapMode>('navigate')
  const fitRef = useRef<L.Map | null>(null)

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
    if (Number.isFinite(lat) && Number.isFinite(lon)) {
      fitRef.current?.panTo([lat, lon])
    }
  }, [goToLat, goToLon])

  const handleDraw = useCallback((bounds: GeoBounds) => {
    onAreaChange(bounds)
    // Automatically return to navigate mode after drawing.
    setMode('navigate')
  }, [onAreaChange])

  return (
    <div className="download-area-map">
      <div className="map-controls">
        {searchClient && (
          <div className="search-controls">
            <input
              type="text"
              placeholder="Enter a location name..."
              value={searchQuery}
              onChange={(e) => setSearchQuery(e.target.value)}
              onKeyDown={(e) => {
                if (e.key === 'Enter') {
                  e.preventDefault()
                  handleSearch()
                }
              }}
            />
            <button
              type="button"
              onClick={handleSearch}
              disabled={searching || searchQuery.trim().length < 2}
            >
              Search
            </button>
            {searching && <span className="search-status">Searching...</span>}
            {!searching && hasSearched && !searchError && searchResults.length === 0 && (
              <span className="search-no-results">No results found</span>
            )}
            {searchError && <span className="search-error">{searchError}</span>}
            {searchResults.length > 0 && (
              <ul className="search-results">
                {searchResults.map((result, i) => (
                  <li key={i}>
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
            <span className="search-attribution">{searchClient.attribution}</span>
          </div>
        )}
        <div className="go-to-controls">
          <input
            type="number"
            placeholder="Lat"
            value={goToLat}
            onChange={(e) => setGoToLat(e.target.value)}
            step={0.01}
          />
          <input
            type="number"
            placeholder="Lon"
            value={goToLon}
            onChange={(e) => setGoToLon(e.target.value)}
            step={0.01}
          />
          <button type="button" onClick={handleGoTo}>
            Go To
          </button>
        </div>
        <div className="mode-controls">
          <button
            type="button"
            className={mode === 'navigate' ? 'mode-active' : ''}
            onClick={() => setMode('navigate')}
          >
            Navigate
          </button>
          <button
            type="button"
            className={mode === 'draw' ? 'mode-active' : ''}
            onClick={() => setMode('draw')}
          >
            Draw Area
          </button>
        </div>
        <div className="map-hint">
          {mode === 'draw'
            ? 'Click and drag on the map to draw a rectangular area.'
            : 'Pan and zoom normally. Click "Draw Area" to draw a rectangle.'}
        </div>
      </div>
      <MapContainer
        center={[39.7, -105.2]}
        zoom={10}
        style={{ height: '400px', width: '100%' }}
        ref={(m) => {
          fitRef.current = m
        }}
      >
        <TileLayer
          attribution={mapTileConfig.attribution}
          url={mapTileConfig.url}
          maxZoom={mapTileConfig.maxZoom}
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
      <div className="tile-size-info">
        Tile size: {tileSize >= 1000 ? `${tileSize / 1000} km` : `${tileSize} m`}
      </div>
    </div>
  )
}
