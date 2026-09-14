import { cleanup, render, screen, waitFor, act } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { JobState } from '@infraforge/protocol'
import { ImportTerrainDialog } from './ImportTerrainDialog'
import * as terrainApi from './terrainApi'
import { useTerrainStore } from './terrainStore'
import type { EngineClient } from '../../lib/engineSession'
import type { GeoBounds, SelectionTileInfo } from './DownloadAreaMap'

// Component tests for the Download Area workflow (Finding 6).
// These tests render the real ImportTerrainDialog and exercise the
// actual Download Area logic through user interaction. The terrain API
// and Leaflet map are mocked at the minimal boundary necessary.

vi.mock('./terrainApi')

// Mock the DownloadAreaMap component to avoid Leaflet DOM complexity.
// The mock captures the props and allows tests to simulate area drawing
// and tile toggling through the onAreaChange/onTileToggle callbacks.
vi.mock('./DownloadAreaMap', () => ({
  DownloadAreaMap: ({
    area,
    onAreaChange,
    selectionTiles,
    onTileToggle,
    tileSize,
    searchClient,
  }: {
    area: GeoBounds | null
    onAreaChange: (area: GeoBounds) => void
    selectionTiles: SelectionTileInfo[]
    onTileToggle: (index: number) => void
    tileSize: number
    searchClient?: unknown
  }) => {
    return (
      <div data-testid="download-area-map-mock">
        <span data-testid="tile-size">{tileSize}</span>
        <span data-testid="area-display">
          {area ? `${area.west},${area.south},${area.east},${area.north}` : 'no-area'}
        </span>
        <span data-testid="tile-count">{selectionTiles.length}</span>
        <button
          data-testid="draw-area-a"
          onClick={() => onAreaChange({ west: -105.5, south: 39.5, east: -105.0, north: 40.0 })}
        >
          Draw Area A
        </button>
        <button
          data-testid="draw-area-b"
          onClick={() => onAreaChange({ west: -74.1, south: 40.6, east: -73.9, north: 40.8 })}
        >
          Draw Area B
        </button>
        {selectionTiles.map((tile) => (
          <button
            key={tile.index}
            data-testid={`tile-${tile.index}`}
            onClick={() => onTileToggle(tile.index)}
          >
            Tile {tile.index} {tile.selected ? '✓' : '○'}
          </button>
        ))}
        <span data-testid="search-attribution">
          {searchClient && typeof searchClient === 'object' && 'attribution' in searchClient
            ? (searchClient as { attribution: string }).attribution
            : ''}
        </span>
      </div>
    )
  },
}))

const client = { request: vi.fn() } as unknown as EngineClient

afterEach(cleanup)

beforeEach(() => {
  vi.restoreAllMocks()
  useTerrainStore.setState({
    jobs: [],
    probe: null,
    probeError: null,
    lastError: null,
    importing: false,
  })
  // Default mock for listTerrainSources.
  vi.mocked(terrainApi.listTerrainSources).mockResolvedValue({
    providers: [
      {
        providerId: 'terrarium-aws',
        displayName: 'AWS Terrain Tiles (Terrarium)',
        attribution: 'Elevation data © Mapzen, USGS, NASA',
        requiresAuth: false,
        maxResolutionMpp: 0,
      },
    ],
  } as unknown as Awaited<ReturnType<typeof terrainApi.listTerrainSources>>)
  // Default mock for planTerrainDownload — cast to avoid protobuf $typeName.
  vi.mocked(terrainApi.planTerrainDownload).mockImplementation(
    async (_client, providerId, area, tileSize, selectedIndices) => {
      const tiles = []
      for (let row = 0; row < 2; row++) {
        for (let col = 0; col < 2; col++) {
          tiles.push({
            col,
            row,
            bounds: {
              west: area.west + col * 0.25,
              south: area.south + row * 0.25,
              east: area.west + (col + 1) * 0.25,
              north: area.south + (row + 1) * 0.25,
            },
            areaSqm: 1_000_000,
          })
        }
      }
      return {
        plan: {
          providerId,
          selectionTiles: tiles,
          providerRequests: [],
          requestCount: 0,
          deduplicatedRequestCount: 0,
          effectiveResolutionMpp: 76.4,
          estimatedBytes: 0n,
          warnings: [],
          fullCoverage: true,
          totalTileCount: 4,
          selectedTileCount: selectedIndices.length,
          selectedAreaSqm: selectedIndices.length * 1_000_000,
          selectedIndices,
        },
      } as unknown as Awaited<ReturnType<typeof terrainApi.planTerrainDownload>>
    },
  )
  // Default mock for downloadSelectedTerrain.
  vi.mocked(terrainApi.downloadSelectedTerrain).mockResolvedValue('job-1')
})

describe('Download Area - component tests', () => {
  it('switches to Download Area mode and loads providers', async () => {
    const user = userEvent.setup()
    render(<ImportTerrainDialog client={client} busy={false} onClose={vi.fn()} />)

    await user.click(screen.getByText('Download Area'))

    await waitFor(() => {
      expect(terrainApi.listTerrainSources).toHaveBeenCalledTimes(1)
    })

    // Provider should be loaded and visible.
    expect(screen.getByText('AWS Terrain Tiles (Terrarium)')).toBeInTheDocument()
    // Attribution should be visible.
    expect(screen.getByText(/Elevation data/)).toBeInTheDocument()
  })

  it('draws area A, loads plan, and allows tile selection', async () => {
    const user = userEvent.setup()
    render(<ImportTerrainDialog client={client} busy={false} onClose={vi.fn()} />)

    await user.click(screen.getByText('Download Area'))
    await waitFor(() => {
      expect(terrainApi.listTerrainSources).toHaveBeenCalled()
    })

    // Draw Area A.
    await user.click(screen.getByTestId('draw-area-a'))

    // Plan should be fetched.
    await waitFor(() => {
      expect(terrainApi.planTerrainDownload).toHaveBeenCalled()
    })

    // Tiles should be rendered.
    await waitFor(() => {
      expect(screen.getByTestId('tile-0')).toBeInTheDocument()
      expect(screen.getByTestId('tile-1')).toBeInTheDocument()
      expect(screen.getByTestId('tile-2')).toBeInTheDocument()
      expect(screen.getByTestId('tile-3')).toBeInTheDocument()
    })

    // Select tile 0.
    await user.click(screen.getByTestId('tile-0'))

    // Download Selected should be enabled after selecting a tile and entering a name.
    await user.type(screen.getByPlaceholderText('Downloaded area DEM'), 'Test DEM')
    expect(screen.getByRole('button', { name: 'Download Selected' })).not.toBeDisabled()
  })

  it('drawing Area B immediately clears plan and selection from Area A', async () => {
    const user = userEvent.setup()
    // Add a delay to planTerrainDownload so we can observe the
    // intermediate state where the plan is null (Finding 1).
    let planDelay = 50
    vi.mocked(terrainApi.planTerrainDownload).mockImplementation(
      async (_client, providerId, area, tileSize, selectedIndices) => {
        await new Promise((r) => setTimeout(r, planDelay))
        const tiles = []
        for (let row = 0; row < 2; row++) {
          for (let col = 0; col < 2; col++) {
            tiles.push({
              col,
              row,
              bounds: {
                west: area.west + col * 0.25,
                south: area.south + row * 0.25,
                east: area.west + (col + 1) * 0.25,
                north: area.south + (row + 1) * 0.25,
              },
              areaSqm: 1_000_000,
            })
          }
        }
        return {
          plan: {
            providerId,
            selectionTiles: tiles,
            providerRequests: [],
            requestCount: 0,
            deduplicatedRequestCount: 0,
            effectiveResolutionMpp: 76.4,
            estimatedBytes: 0n,
            warnings: [],
            fullCoverage: true,
            totalTileCount: 4,
            selectedTileCount: selectedIndices.length,
            selectedAreaSqm: selectedIndices.length * 1_000_000,
            selectedIndices,
          },
        } as unknown as Awaited<ReturnType<typeof terrainApi.planTerrainDownload>>
      },
    )

    render(<ImportTerrainDialog client={client} busy={false} onClose={vi.fn()} />)

    await user.click(screen.getByText('Download Area'))
    await waitFor(() => {
      expect(terrainApi.listTerrainSources).toHaveBeenCalled()
    })

    // Draw Area A.
    await user.click(screen.getByTestId('draw-area-a'))
    await waitFor(() => {
      expect(screen.getByTestId('tile-0')).toBeInTheDocument()
    })

    // Select tile 0.
    await user.click(screen.getByTestId('tile-0'))

    // Draw Area B — should immediately clear plan and selection.
    await user.click(screen.getByTestId('draw-area-b'))

    // Plan A tiles should disappear immediately while the new plan loads.
    await waitFor(() => {
      expect(screen.queryByTestId('tile-0')).toBeNull()
    })

    // After Area B plan loads, tiles should appear.
    await waitFor(() => {
      expect(screen.getByTestId('tile-0')).toBeInTheDocument()
    })

    // Download Selected should be disabled (no tiles selected).
    expect(screen.getByRole('button', { name: 'Download Selected' })).toBeDisabled()
  })

  it('tile size change clears plan and selection', async () => {
    const user = userEvent.setup()
    // Add a delay to planTerrainDownload so we can observe the
    // intermediate state where the plan is null (Finding 1).
    let planDelay = 50
    vi.mocked(terrainApi.planTerrainDownload).mockImplementation(
      async (_client, providerId, area, tileSize, selectedIndices) => {
        await new Promise((r) => setTimeout(r, planDelay))
        const tiles = []
        for (let row = 0; row < 2; row++) {
          for (let col = 0; col < 2; col++) {
            tiles.push({
              col,
              row,
              bounds: {
                west: area.west + col * 0.25,
                south: area.south + row * 0.25,
                east: area.west + (col + 1) * 0.25,
                north: area.south + (row + 1) * 0.25,
              },
              areaSqm: 1_000_000,
            })
          }
        }
        return {
          plan: {
            providerId,
            selectionTiles: tiles,
            providerRequests: [],
            requestCount: 0,
            deduplicatedRequestCount: 0,
            effectiveResolutionMpp: 76.4,
            estimatedBytes: 0n,
            warnings: [],
            fullCoverage: true,
            totalTileCount: 4,
            selectedTileCount: selectedIndices.length,
            selectedAreaSqm: selectedIndices.length * 1_000_000,
            selectedIndices,
          },
        } as unknown as Awaited<ReturnType<typeof terrainApi.planTerrainDownload>>
      },
    )

    render(<ImportTerrainDialog client={client} busy={false} onClose={vi.fn()} />)

    await user.click(screen.getByText('Download Area'))
    await waitFor(() => {
      expect(terrainApi.listTerrainSources).toHaveBeenCalled()
    })

    await user.click(screen.getByTestId('draw-area-a'))
    await waitFor(() => {
      expect(screen.getByTestId('tile-0')).toBeInTheDocument()
    })

    await user.click(screen.getByTestId('tile-0'))

    // Change tile size.
    await user.selectOptions(screen.getByDisplayValue('4 km'), '16 km')

    // Tiles should disappear while new plan loads.
    await waitFor(() => {
      expect(screen.queryByTestId('tile-0')).toBeNull()
    })

    // After new plan loads, tiles should reappear.
    await waitFor(() => {
      expect(screen.getByTestId('tile-0')).toBeInTheDocument()
    })

    // Download Selected should be disabled (selection cleared).
    expect(screen.getByRole('button', { name: 'Download Selected' })).toBeDisabled()
  })

  it('Download Selected sends exact current provider/area/tileSize/indices', async () => {
    const user = userEvent.setup()
    render(<ImportTerrainDialog client={client} busy={false} onClose={vi.fn()} />)

    await user.click(screen.getByText('Download Area'))
    await waitFor(() => {
      expect(terrainApi.listTerrainSources).toHaveBeenCalled()
    })

    await user.click(screen.getByTestId('draw-area-a'))
    await waitFor(() => {
      expect(screen.getByTestId('tile-0')).toBeInTheDocument()
    })

    await user.click(screen.getByTestId('tile-0'))
    await user.click(screen.getByTestId('tile-2'))
    await user.type(screen.getByPlaceholderText('Downloaded area DEM'), 'My DEM')

    await user.click(screen.getByRole('button', { name: 'Download Selected' }))

    await waitFor(() => {
      expect(terrainApi.downloadSelectedTerrain).toHaveBeenCalledTimes(1)
    })

    const call = vi.mocked(terrainApi.downloadSelectedTerrain).mock.calls[0]!
    expect(call[1]).toBe('terrarium-aws') // providerId
    expect(call[2]).toEqual({ west: -105.5, south: 39.5, east: -105.0, north: 40.0 }) // area
    expect(call[3]).toBe(4000) // tileSize
    expect(call[4]).toEqual([0, 2]) // selectedIndices
    expect(call[5]).toBe('My DEM') // displayName
  })

  it('command-start failure is visible to the user', async () => {
    const user = userEvent.setup()
    vi.mocked(terrainApi.downloadSelectedTerrain).mockRejectedValueOnce(
      new Error('selection_too_large: grid exceeds maximum'),
    )

    render(<ImportTerrainDialog client={client} busy={false} onClose={vi.fn()} />)

    await user.click(screen.getByText('Download Area'))
    await waitFor(() => {
      expect(terrainApi.listTerrainSources).toHaveBeenCalled()
    })

    await user.click(screen.getByTestId('draw-area-a'))
    await waitFor(() => {
      expect(screen.getByTestId('tile-0')).toBeInTheDocument()
    })

    await user.click(screen.getByTestId('tile-0'))
    await user.type(screen.getByPlaceholderText('Downloaded area DEM'), 'Test')

    await user.click(screen.getByRole('button', { name: 'Download Selected' }))

    await waitFor(() => {
      expect(screen.getByText(/selection_too_large/)).toBeInTheDocument()
    })
  })

  it('search attribution is visible in the map area', async () => {
    const user = userEvent.setup()
    render(<ImportTerrainDialog client={client} busy={false} onClose={vi.fn()} />)

    await user.click(screen.getByText('Download Area'))
    await waitFor(() => {
      expect(terrainApi.listTerrainSources).toHaveBeenCalled()
    })

    // The search attribution should be rendered in the mock map.
    expect(screen.getByTestId('search-attribution')).toBeInTheDocument()
    expect(screen.getByTestId('search-attribution').textContent).toContain('OpenStreetMap')
  })

  it('estimated size shows unknown when bytes is 0', async () => {
    const user = userEvent.setup()
    render(<ImportTerrainDialog client={client} busy={false} onClose={vi.fn()} />)

    await user.click(screen.getByText('Download Area'))
    await waitFor(() => {
      expect(terrainApi.listTerrainSources).toHaveBeenCalled()
    })

    await user.click(screen.getByTestId('draw-area-a'))
    await waitFor(() => {
      expect(screen.getByTestId('tile-0')).toBeInTheDocument()
    })

    await user.click(screen.getByTestId('tile-0'))

    // Estimated size should show "—" (unknown) since estimatedBytes is 0.
    await waitFor(() => {
      expect(screen.getByText(/Estimated size:/)).toBeInTheDocument()
    })
    const sizeLine = screen.getByText(/Estimated size:/).parentElement ?? screen.getByText(/Estimated size:/)
    expect(sizeLine.textContent).toContain('—')
  })
})
