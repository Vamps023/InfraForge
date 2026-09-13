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

// Real interactive map for the Download Area UX (BLOCKER 5).
// Provides pan, zoom, rectangular drawing, go-to lat/lon, and a selection
// grid overlay. The frontend owns only interaction/projection — it does
// NOT decode DEMs or create terrain truth.
//
// Map tiles: OpenStreetMap (attribution required, CC BY-SA).
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

interface DownloadAreaMapProps {
  area: GeoBounds | null
  onAreaChange: (area: GeoBounds) => void
  selectionTiles: SelectionTileInfo[]
  onTileToggle: (index: number) => void
  tileSize: number
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

// Component that handles map click-and-drag to draw a rectangle.
function DrawHandler({
  onDraw,
}: {
  onDraw: (bounds: GeoBounds) => void
}) {
  const [drawing, setDrawing] = useState(false)
  const [start, setStart] = useState<L.LatLng | null>(null)
  const [current, setCurrent] = useState<L.LatLng | null>(null)
  const map = useMapEvents({
    mousedown: (e) => {
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
}: DownloadAreaMapProps) {
  const [goToLat, setGoToLat] = useState('')
  const [goToLon, setGoToLon] = useState('')
  const fitRef = useRef<L.Map | null>(null)

  const handleGoTo = useCallback(() => {
    const lat = parseFloat(goToLat)
    const lon = parseFloat(goToLon)
    if (Number.isFinite(lat) && Number.isFinite(lon)) {
      fitRef.current?.panTo([lat, lon])
    }
  }, [goToLat, goToLon])

  return (
    <div className="download-area-map">
      <div className="map-controls">
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
        <div className="map-hint">
          Click and drag on the map to draw a rectangular area.
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
          attribution='&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors'
          url="https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png"
          maxZoom={19}
        />
        <DrawHandler onDraw={onAreaChange} />
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
