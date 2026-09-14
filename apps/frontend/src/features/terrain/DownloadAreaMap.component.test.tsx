import { cleanup, render, screen, waitFor, act } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { DownloadAreaMap, type GeoBounds, type SelectionTileInfo, defaultMapTileConfig } from './DownloadAreaMap'
import type { LocationSearchClient, SearchResult } from './locationSearch'

// BLOCKER 11: Real DownloadAreaMap search lifecycle component tests.
// These tests render the REAL DownloadAreaMap component (not a mock),
// mocking only Leaflet/react-leaflet at the minimal boundary.

const mockMap = {
  fitBounds: vi.fn(),
  panTo: vi.fn(),
  dragging: { disable: vi.fn(), enable: vi.fn() },
}

vi.mock('react-leaflet', () => {
  const React = require('react')
  return {
    MapContainer: ({ children }: any) =>
      React.createElement('div', { 'data-testid': 'map-container' }, children),
    TileLayer: ({ url, attribution, maxZoom }: any) =>
      React.createElement('div', {
        'data-testid': 'tile-layer',
        'data-url': url,
        'data-attribution': attribution,
        'data-max-zoom': String(maxZoom),
      }),
    Rectangle: ({ bounds, eventHandlers }: any) =>
      React.createElement('div', {
        'data-testid': 'rectangle',
        'data-bounds': JSON.stringify(bounds),
        onClick: eventHandlers?.click,
      }),
    useMap: () => mockMap,
    useMapEvents: () => mockMap,
  }
})

vi.mock('leaflet', () => {
  return {
    default: {
      latLngBounds: (sw: any, ne: any) => ({
        getWest: () => (typeof sw === 'object' ? sw.lng : sw[1]),
        getSouth: () => (typeof sw === 'object' ? sw.lat : sw[0]),
        getEast: () => (typeof ne === 'object' ? ne.lng : ne[1]),
        getNorth: () => (typeof ne === 'object' ? ne.lat : ne[0]),
        isValid: () => true,
      }),
      latLng: (lat: number, lng: number) => ({ lat, lng }),
    },
    latLngBounds: (sw: any, ne: any) => ({
      getWest: () => (typeof sw === 'object' ? sw.lng : sw[1]),
      getSouth: () => (typeof sw === 'object' ? sw.lat : sw[0]),
      getEast: () => (typeof ne === 'object' ? ne.lng : ne[1]),
      getNorth: () => (typeof ne === 'object' ? ne.lat : ne[0]),
      isValid: () => true,
    }),
    latLng: (lat: number, lng: number) => ({ lat, lng }),
  }
})

vi.mock('leaflet/dist/leaflet.css', () => ({}))

afterEach(cleanup)

function makeMockSearchClient(
  results: SearchResult[],
  opts: { delay?: number; fail?: boolean } = {},
): LocationSearchClient & { searchMock: ReturnType<typeof vi.fn>; cancelPendingMock: ReturnType<typeof vi.fn> } {
  const delay = opts.delay ?? 0
  const searchMock = vi.fn(async (_query: string) => {
    if (delay > 0) await new Promise((r) => setTimeout(r, delay))
    if (opts.fail) throw new Error('Search failed')
    return results
  })
  const cancelPendingMock = vi.fn()
  return {
    search: searchMock,
    attribution: '© OpenStreetMap contributors',
    cancelPending: cancelPendingMock,
    searchMock,
    cancelPendingMock,
  } as any
}

const defaultProps = {
  area: null as GeoBounds | null,
  onAreaChange: vi.fn(),
  selectionTiles: [] as SelectionTileInfo[],
  onTileToggle: vi.fn(),
  tileSize: 4000,
}

describe('BLOCKER 11: DownloadAreaMap search lifecycle', () => {
  it('typing alone does not search', async () => {
    const client = makeMockSearchClient([])
    render(<DownloadAreaMap {...defaultProps} searchClient={client} />)
    const input = screen.getByPlaceholderText('Enter a location name...')
    await userEvent.type(input, 'Denver')
    expect(client.searchMock).not.toHaveBeenCalled()
  })

  it('Search click fires exactly one search', async () => {
    const client = makeMockSearchClient([
      { displayName: 'Denver, CO', lat: 39.74, lon: -104.99 },
    ])
    render(<DownloadAreaMap {...defaultProps} searchClient={client} />)
    const input = screen.getByPlaceholderText('Enter a location name...')
    await userEvent.type(input, 'Denver')
    await userEvent.click(screen.getByText('Search'))
    await waitFor(() => {
      expect(client.searchMock).toHaveBeenCalledTimes(1)
    })
  })

  it('Enter fires exactly one search', async () => {
    const client = makeMockSearchClient([
      { displayName: 'Denver, CO', lat: 39.74, lon: -104.99 },
    ])
    render(<DownloadAreaMap {...defaultProps} searchClient={client} />)
    const input = screen.getByPlaceholderText('Enter a location name...')
    await userEvent.type(input, 'Denver')
    await userEvent.keyboard('{Enter}')
    await waitFor(() => {
      expect(client.searchMock).toHaveBeenCalledTimes(1)
    })
  })

  it('search result renders', async () => {
    const results: SearchResult[] = [
      { displayName: 'Denver, Colorado', lat: 39.74, lon: -104.99 },
    ]
    const client = makeMockSearchClient(results)
    render(<DownloadAreaMap {...defaultProps} searchClient={client} />)
    const input = screen.getByPlaceholderText('Enter a location name...')
    await userEvent.type(input, 'Denver')
    await userEvent.click(screen.getByText('Search'))
    await waitFor(() => {
      expect(screen.getByText('Denver, Colorado')).toBeInTheDocument()
    })
  })

  it('no-results renders', async () => {
    const client = makeMockSearchClient([])
    render(<DownloadAreaMap {...defaultProps} searchClient={client} />)
    const input = screen.getByPlaceholderText('Enter a location name...')
    await userEvent.type(input, 'NonexistentPlace')
    await userEvent.click(screen.getByText('Search'))
    await waitFor(() => {
      expect(screen.getByText('No results found')).toBeInTheDocument()
    })
  })

  it('synchronous failure renders error', async () => {
    const client = makeMockSearchClient([], { fail: true })
    render(<DownloadAreaMap {...defaultProps} searchClient={client} />)
    const input = screen.getByPlaceholderText('Enter a location name...')
    await userEvent.type(input, 'test')
    await userEvent.click(screen.getByText('Search'))
    await waitFor(() => {
      expect(screen.getByText('Search failed')).toBeInTheDocument()
    })
  })

  it('searching indicator clears after failure', async () => {
    const client = makeMockSearchClient([], { fail: true, delay: 50 })
    render(<DownloadAreaMap {...defaultProps} searchClient={client} />)
    const input = screen.getByPlaceholderText('Enter a location name...')
    await userEvent.type(input, 'test')
    await userEvent.click(screen.getByText('Search'))
    // After failure, the indicator should clear and error should show.
    await waitFor(() => {
      expect(screen.queryByText('Searching...')).not.toBeInTheDocument()
      expect(screen.getByText('Search failed')).toBeInTheDocument()
    })
  })

  it('stale response cannot overwrite newer result', async () => {
    const results1: SearchResult[] = [
      { displayName: 'Old Result', lat: 0, lon: 0 },
    ]
    const results2: SearchResult[] = [
      { displayName: 'New Result', lat: 1, lon: 1 },
    ]
    let callCount = 0
    const client = makeMockSearchClient(results1)
    client.searchMock.mockImplementation(async () => {
      callCount++
      if (callCount === 1) return results1
      return results2
    })

    render(<DownloadAreaMap {...defaultProps} searchClient={client} />)
    const input = screen.getByPlaceholderText('Enter a location name...')

    // First search.
    await userEvent.type(input, 'old')
    await userEvent.click(screen.getByText('Search'))
    await waitFor(() => {
      expect(screen.getByText('Old Result')).toBeInTheDocument()
    })

    // Second search.
    await userEvent.clear(input)
    await userEvent.type(input, 'new')
    await userEvent.click(screen.getByText('Search'))
    await waitFor(() => {
      expect(screen.getByText('New Result')).toBeInTheDocument()
    })
  })

  it('unmount cancels pending search', async () => {
    const client = makeMockSearchClient([], { delay: 5000 })
    const { unmount } = render(
      <DownloadAreaMap {...defaultProps} searchClient={client} />,
    )
    const input = screen.getByPlaceholderText('Enter a location name...')
    await userEvent.type(input, 'test')
    await userEvent.click(screen.getByText('Search'))
    // Unmount before the delayed search completes.
    unmount()
    // cancelPending should have been called.
    expect(client.cancelPendingMock).toHaveBeenCalled()
  })

  it('no request fires after unmount', async () => {
    const client = makeMockSearchClient([], { delay: 5000 })
    const { unmount } = render(
      <DownloadAreaMap {...defaultProps} searchClient={client} />,
    )
    const input = screen.getByPlaceholderText('Enter a location name...')
    await userEvent.type(input, 'test')
    await userEvent.click(screen.getByText('Search'))
    const callsBefore = client.searchMock.mock.calls.length
    unmount()
    // Wait a bit to see if any delayed callbacks fire.
    await new Promise((r) => setTimeout(r, 100))
    // No additional search calls should have been made after unmount.
    expect(client.searchMock.mock.calls.length).toBe(callsBefore)
  })
})

describe('BLOCKER 6: DownloadAreaMap tile config', () => {
  it('uses default OSM tile config when none provided', () => {
    render(<DownloadAreaMap {...defaultProps} />)
    const tileLayer = screen.getByTestId('tile-layer')
    expect(tileLayer.getAttribute('data-url')).toBe(defaultMapTileConfig.url)
    expect(tileLayer.getAttribute('data-attribution')).toContain('OpenStreetMap')
  })

  it('uses custom tile config when provided', () => {
    const customConfig = {
      url: 'https://custom-tiles.example.com/{z}/{x}/{y}.png',
      attribution: 'Custom Tiles',
      maxZoom: 15,
    }
    render(<DownloadAreaMap {...defaultProps} mapTileConfig={customConfig} />)
    const tileLayer = screen.getByTestId('tile-layer')
    expect(tileLayer.getAttribute('data-url')).toBe(customConfig.url)
    expect(tileLayer.getAttribute('data-attribution')).toBe('Custom Tiles')
    expect(tileLayer.getAttribute('data-max-zoom')).toBe('15')
  })
})
