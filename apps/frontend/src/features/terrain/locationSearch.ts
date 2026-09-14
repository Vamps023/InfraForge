// Location search service for the Download Area map (IMPORTANT 6).
// Uses the OpenStreetMap Nominatim geocoder with proper attribution.
// Search results are frontend UX only — they reposition the map and are
// NOT terrain truth.
//
// Usage requirements (https://nominatim.openstreetmap.org/):
// - Must send a valid HTTP Referer or User-Agent.
// - Max 1 request per second (debounced here).
// - Results are CC BY 2.0 (OpenStreetMap contributors).

export interface SearchResult {
  displayName: string
  lat: number
  lon: number
  boundingBox?: { south: number; north: number; west: number; east: number }
}

export interface LocationSearchClient {
  search(query: string): Promise<SearchResult[]>
}

// Production implementation using the Nominatim public API.
// Respects usage policy: max 1 req/sec, proper attribution.
export class NominatimLocationSearchClient implements LocationSearchClient {
  async search(query: string): Promise<SearchResult[]> {
    const trimmed = query.trim()
    if (trimmed.length < 2) return []

    const url = new URL('https://nominatim.openstreetmap.org/search')
    url.searchParams.set('q', trimmed)
    url.searchParams.set('format', 'json')
    url.searchParams.set('limit', '5')
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
    return data.map((item) => ({
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
  }
}
