import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { cleanup, render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { RoadProfileEditor } from './RoadProfileEditor'
import { useRoadStore } from './roadStore'
import { useTerrainStore } from '../terrain/terrainStore'
import { useSelectionStore } from '../../editor/selection/selectionStore'
import { create } from '@bufbuild/protobuf'
import { RoadDetailsSchema, RoadSummarySchema, TerrainDatasetInfoSchema } from '@infraforge/protocol'
import * as roadApi from './roadApi'

describe('RoadProfileEditor stale details protection & selection sync', () => {
  const roadA = create(RoadSummarySchema, {
    roadId: 'road-A',
    name: 'Road Alpha',
    length: 100,
    alignmentSegmentCount: 2,
    protectedAnchorCount: 0,
    revision: 1n,
  })

  const roadB = create(RoadSummarySchema, {
    roadId: 'road-B',
    name: 'Road Beta',
    length: 250,
    alignmentSegmentCount: 3,
    protectedAnchorCount: 1,
    revision: 1n,
  })

  const detailsA = create(RoadDetailsSchema, {
    roadId: 'road-A',
    hasElevationProfile: true,
    elevationBreakpointCount: 4,
    elevationBreakpoints: [
      { station: 0, value: 10 },
      { station: 100, value: 15 },
    ],
    superelevationBreakpoints: [
      { station: 0, value: 0 },
      { station: 100, value: 0.02 },
    ],
    widthBreakpoints: [
      { station: 0, leftWidth: 3, rightWidth: 4 },
      { station: 100, leftWidth: 7, rightWidth: 2 },
    ],
    controlPoints: [
      { easting: 0, northing: 0, elevation: 10, protectedAnchor: false },
      { easting: 50, northing: 0, elevation: 12, protectedAnchor: false },
      { easting: 100, northing: 0, elevation: 15, protectedAnchor: false },
    ],
  })

  const detailsB = create(RoadDetailsSchema, {
    roadId: 'road-B',
    hasElevationProfile: false,
    elevationBreakpointCount: 0,
    controlPoints: [
      { easting: 200, northing: 100, elevation: 5, protectedAnchor: false },
      { easting: 300, northing: 100, elevation: 5, protectedAnchor: false },
      { easting: 400, northing: 100, elevation: 5, protectedAnchor: false },
      { easting: 450, northing: 100, elevation: 5, protectedAnchor: true },
    ],
  })

  beforeEach(() => {
    useRoadStore.getState().reset()
    useSelectionStore.getState().clear()
    useTerrainStore.getState().reset()
    useRoadStore.getState().setRoads([roadA, roadB])
  })

  afterEach(() => {
    cleanup()
    vi.restoreAllMocks()
  })

  it('renders road metrics when matching details are loaded', () => {
    useSelectionStore.getState().select(['road:road-A'])
    useRoadStore.getState().setDetails(detailsA)

    render(<RoadProfileEditor getEngineClient={() => null} />)

    expect(screen.getByText('Road Alpha')).toBeInTheDocument()
    expect(screen.getByText('100.00 project units')).toBeInTheDocument()
    expect(screen.getByLabelText('Breakpoint 1 value')).toHaveValue(10)
    expect(screen.getByLabelText('Breakpoint 2 value')).toHaveValue(15)
  })

  it('protects against using Road A details when Road B is selected', async () => {
    const updateRoadElevationSpy = vi.spyOn(roadApi, 'updateRoadElevation').mockResolvedValue(undefined as any)
    vi.spyOn(roadApi, 'getRoad').mockResolvedValue(undefined as any)

    // 1. Road A selected and details loaded
    useSelectionStore.getState().select(['road:road-A'])
    useRoadStore.getState().setDetails(detailsA)

    const { rerender } = render(<RoadProfileEditor getEngineClient={() => ({} as any)} />)
    expect(screen.getByText('Road Alpha')).toBeInTheDocument()

    // 2. User selects Road B (e.g. via Navigator or Viewport)
    // details in roadStore are still Road A until Road B finishes fetching
    useSelectionStore.getState().select(['road:road-B'])
    rerender(<RoadProfileEditor getEngineClient={() => ({} as any)} />)

    // Road B name should be shown, but details should show loading/stale protection
    expect(screen.getByText('Road Beta')).toBeInTheDocument()
    expect(screen.getByText('Loading canonical road profile…')).toBeInTheDocument()

    // Apply button must be disabled while details do not match selected road
    const saveButton = screen.getByRole('button', { name: /Loading details…/ })
    expect(saveButton).toBeDisabled()

    // Even if somehow clicked, updateRoadElevation must not be invoked
    await userEvent.click(saveButton)
    expect(updateRoadElevationSpy).not.toHaveBeenCalled()

    // 3. Hydrate Road B details
    useRoadStore.getState().setDetails(detailsB)
    rerender(<RoadProfileEditor getEngineClient={() => ({} as any)} />)

    expect(screen.getByText(/No authored breakpoints/)).toBeInTheDocument()
    const activeSaveButton = screen.getByRole('button', { name: 'Save profile' })
    expect(activeSaveButton).not.toBeDisabled()

    // 4. Click Apply — must target Road B with Road B's 4 control points
    await userEvent.click(activeSaveButton)
    expect(updateRoadElevationSpy).toHaveBeenCalledTimes(1)
    expect(updateRoadElevationSpy).toHaveBeenCalledWith(
      expect.anything(),
      'road-B',
      [],
      [],
    )
  })

  it('edits and saves exact canonical elevation breakpoints', async () => {
    const updateSpy = vi.spyOn(roadApi, 'updateRoadElevation').mockResolvedValue(undefined as any)
    vi.spyOn(roadApi, 'getRoad').mockResolvedValue(undefined as any)
    useSelectionStore.getState().select(['road:road-A'])
    useRoadStore.getState().setDetails(detailsA)
    render(<RoadProfileEditor getEngineClient={() => ({} as any)} />)

    const firstValue = screen.getByLabelText('Breakpoint 1 value')
    await userEvent.clear(firstValue)
    await userEvent.type(firstValue, '12.5')
    await userEvent.click(screen.getByRole('button', { name: 'Save profile' }))

    expect(updateSpy).toHaveBeenCalledWith(expect.anything(), 'road-A', [0, 100], [12.5, 15])
  })

  it('switches to and saves superelevation breakpoints', async () => {
    const updateSpy = vi.spyOn(roadApi, 'updateRoadSuperelevation').mockResolvedValue(undefined as any)
    vi.spyOn(roadApi, 'getRoad').mockResolvedValue(undefined as any)
    useSelectionStore.getState().select(['road:road-A'])
    useRoadStore.getState().setDetails(detailsA)
    render(<RoadProfileEditor getEngineClient={() => ({} as any)} />)

    await userEvent.click(screen.getByRole('tab', { name: 'Superelevation' }))
    await userEvent.click(screen.getByRole('button', { name: 'Save profile' }))
    expect(updateSpy).toHaveBeenCalledWith(expect.anything(), 'road-A', [0, 100], [0, 0.02])
  })

  it('switches to and saves asymmetric width breakpoints', async () => {
    const updateSpy = vi.spyOn(roadApi, 'updateRoadWidth').mockResolvedValue(undefined as any)
    vi.spyOn(roadApi, 'getRoad').mockResolvedValue(undefined as any)
    useSelectionStore.getState().select(['road:road-A'])
    useRoadStore.getState().setDetails(detailsA)
    render(<RoadProfileEditor getEngineClient={() => ({} as any)} />)

    await userEvent.click(screen.getByRole('tab', { name: 'Width' }))
    await userEvent.click(screen.getByRole('button', { name: 'Save profile' }))
    expect(updateSpy).toHaveBeenCalledWith(expect.anything(), 'road-A', [0, 100], [3, 7], [4, 2])
  })

  it('conforms the selected road to terrain with explicit sampling controls and default dataset', async () => {
    const conformSpy = vi.spyOn(roadApi, 'conformRoadToTerrain').mockResolvedValue(undefined as any)
    vi.spyOn(roadApi, 'getRoad').mockResolvedValue(undefined as any)
    useSelectionStore.getState().select(['road:road-A'])
    useRoadStore.getState().setDetails(detailsA)
    render(<RoadProfileEditor getEngineClient={() => ({} as any)} />)

    const interval = screen.getByLabelText('Terrain conformance interval')
    const offset = screen.getByLabelText('Terrain conformance offset')
    await userEvent.clear(interval)
    await userEvent.type(interval, '5')
    await userEvent.clear(offset)
    await userEvent.type(offset, '0.2')
    await userEvent.click(screen.getByRole('button', { name: 'Conform to terrain' }))

    expect(conformSpy).toHaveBeenCalledWith(expect.anything(), 'road-A', 5, 0.2, '')
  })

  it('inserts breakpoint at largest gap midpoint when profile already ends at road length', async () => {
    const updateSpy = vi.spyOn(roadApi, 'updateRoadElevation').mockResolvedValue(undefined as any)
    vi.spyOn(roadApi, 'getRoad').mockResolvedValue(undefined as any)
    useSelectionStore.getState().select(['road:road-A'])
    useRoadStore.getState().setDetails(detailsA) // detailsA has breakpoints at 0 and 100 (road length = 100)
    render(<RoadProfileEditor getEngineClient={() => ({} as any)} />)

    // Initially 2 rows: station 0 and 100
    expect(screen.getByLabelText('Breakpoint 1 station')).toHaveValue(0)
    expect(screen.getByLabelText('Breakpoint 2 station')).toHaveValue(100)

    // Click "Add breakpoint" — should insert at midpoint of gap [0, 100], station 50
    await userEvent.click(screen.getByRole('button', { name: 'Add breakpoint' }))

    // Now 3 rows sorted by station: 0, 50, 100
    expect(screen.getByLabelText('Breakpoint 1 station')).toHaveValue(0)
    expect(screen.getByLabelText('Breakpoint 2 station')).toHaveValue(50)
    expect(screen.getByLabelText('Breakpoint 3 station')).toHaveValue(100)

    // Save profile should succeed without duplicate station or validation errors
    await userEvent.click(screen.getByRole('button', { name: 'Save profile' }))
    expect(updateSpy).toHaveBeenCalledWith(expect.anything(), 'road-A', [0, 50, 100], [10, 0, 15])
  })

  it('conforms to terrain using globally selected dataset when no local override is chosen', async () => {
    const conformSpy = vi.spyOn(roadApi, 'conformRoadToTerrain').mockResolvedValue(undefined as any)
    vi.spyOn(roadApi, 'getRoad').mockResolvedValue(undefined as any)
    useSelectionStore.getState().select(['road:road-A'])
    useRoadStore.getState().setDetails(detailsA)

    const dataset1 = create(TerrainDatasetInfoSchema, {
      datasetUuid: 'dataset-uuid-1',
      displayName: 'LIDAR Survey 2026',
    })
    useTerrainStore.getState().setDatasets([dataset1])
    useTerrainStore.getState().selectDataset('dataset-uuid-1')

    render(<RoadProfileEditor getEngineClient={() => ({} as any)} />)

    // No local override chosen: should inherit active global selection
    await userEvent.click(screen.getByRole('button', { name: 'Conform to terrain' }))
    expect(conformSpy).toHaveBeenCalledWith(expect.anything(), 'road-A', 10, 0.1, 'dataset-uuid-1')
  })

  it('conforms to terrain using user-selected terrain dataset override', async () => {
    const conformSpy = vi.spyOn(roadApi, 'conformRoadToTerrain').mockResolvedValue(undefined as any)
    vi.spyOn(roadApi, 'getRoad').mockResolvedValue(undefined as any)
    useSelectionStore.getState().select(['road:road-A'])
    useRoadStore.getState().setDetails(detailsA)

    const dataset1 = create(TerrainDatasetInfoSchema, {
      datasetUuid: 'dataset-uuid-1',
      displayName: 'LIDAR Survey 2026',
    })
    const dataset2 = create(TerrainDatasetInfoSchema, {
      datasetUuid: 'dataset-uuid-2',
      displayName: 'Photogrammetry DEM',
    })
    useTerrainStore.getState().setDatasets([dataset1, dataset2])
    useTerrainStore.getState().selectDataset('dataset-uuid-1')

    render(<RoadProfileEditor getEngineClient={() => ({} as any)} />)

    const selector = screen.getByLabelText('Terrain dataset')
    expect(selector).toBeInTheDocument()
    await userEvent.selectOptions(selector, 'dataset-uuid-2')

    await userEvent.click(screen.getByRole('button', { name: 'Conform to terrain' }))
    expect(conformSpy).toHaveBeenCalledWith(expect.anything(), 'road-A', 10, 0.1, 'dataset-uuid-2')
  })

  it('explicit "Automatic / Default dataset" sends empty string even while global dataset is selected', async () => {
    const conformSpy = vi.spyOn(roadApi, 'conformRoadToTerrain').mockResolvedValue(undefined as any)
    vi.spyOn(roadApi, 'getRoad').mockResolvedValue(undefined as any)
    useSelectionStore.getState().select(['road:road-A'])
    useRoadStore.getState().setDetails(detailsA)

    const dataset1 = create(TerrainDatasetInfoSchema, {
      datasetUuid: 'dataset-uuid-1',
      displayName: 'LIDAR Survey 2026',
    })
    useTerrainStore.getState().setDatasets([dataset1])
    useTerrainStore.getState().selectDataset('dataset-uuid-1')

    render(<RoadProfileEditor getEngineClient={() => ({} as any)} />)

    const selector = screen.getByLabelText('Terrain dataset')
    await userEvent.selectOptions(selector, '__automatic__')

    await userEvent.click(screen.getByRole('button', { name: 'Conform to terrain' }))
    expect(conformSpy).toHaveBeenCalledWith(expect.anything(), 'road-A', 10, 0.1, '')
  })

  it('resets to inherit mode when the chosen dataset disappears from store', async () => {
    const conformSpy = vi.spyOn(roadApi, 'conformRoadToTerrain').mockResolvedValue(undefined as any)
    vi.spyOn(roadApi, 'getRoad').mockResolvedValue(undefined as any)
    useSelectionStore.getState().select(['road:road-A'])
    useRoadStore.getState().setDetails(detailsA)

    const dataset1 = create(TerrainDatasetInfoSchema, {
      datasetUuid: 'dataset-uuid-1',
      displayName: 'Dataset 1',
    })
    const dataset2 = create(TerrainDatasetInfoSchema, {
      datasetUuid: 'dataset-uuid-2',
      displayName: 'Dataset 2',
    })
    useTerrainStore.getState().setDatasets([dataset1, dataset2])

    const { rerender } = render(<RoadProfileEditor getEngineClient={() => ({} as any)} />)
    const selector = screen.getByLabelText('Terrain dataset')
    await userEvent.selectOptions(selector, 'dataset-uuid-2')
    expect(selector).toHaveValue('dataset-uuid-2')

    // Remove dataset2 from store
    useTerrainStore.getState().setDatasets([dataset1])
    rerender(<RoadProfileEditor getEngineClient={() => ({} as any)} />)

    // Should reset to inherit mode ('__inherit__') rather than keeping stale choice
    expect(screen.getByLabelText('Terrain dataset')).toHaveValue('__inherit__')
  })

  it('session token increment resets local dataset choice', async () => {
    useSelectionStore.getState().select(['road:road-A'])
    useRoadStore.getState().setDetails(detailsA)

    const dataset1 = create(TerrainDatasetInfoSchema, {
      datasetUuid: 'dataset-uuid-1',
      displayName: 'Dataset 1',
    })
    useTerrainStore.getState().setDatasets([dataset1])

    const { rerender } = render(<RoadProfileEditor getEngineClient={() => ({} as any)} />)
    const selector = screen.getByLabelText('Terrain dataset')
    await userEvent.selectOptions(selector, '__automatic__')
    expect(selector).toHaveValue('__automatic__')

    // Simulate project/session switch minting new token and hydrating datasets
    useTerrainStore.getState().beginSession()
    useTerrainStore.getState().setDatasets([dataset1])
    rerender(<RoadProfileEditor getEngineClient={() => ({} as any)} />)

    expect(screen.getByLabelText('Terrain dataset')).toHaveValue('__inherit__')
  })

  it('inserts breakpoint with full precision in sub-centimeter gaps without duplicate stations', async () => {
    const updateSpy = vi.spyOn(roadApi, 'updateRoadElevation').mockResolvedValue(undefined as any)
    vi.spyOn(roadApi, 'getRoad').mockResolvedValue(undefined as any)

    const denseRoad = create(RoadSummarySchema, {
      roadId: 'road-dense',
      name: 'Dense Road',
      length: 0.002,
      alignmentSegmentCount: 1,
    })
    const denseDetails = create(RoadDetailsSchema, {
      roadId: 'road-dense',
      elevationBreakpoints: [
        { station: 0.001, value: 10 },
        { station: 0.002, value: 12 },
      ],
    })
    useRoadStore.getState().setRoads([denseRoad])
    useSelectionStore.getState().select(['road:road-dense'])
    useRoadStore.getState().setDetails(denseDetails)

    render(<RoadProfileEditor getEngineClient={() => ({} as any)} />)

    // Click Add breakpoint — gap between 0.001 and 0.002 is 0.001. Midpoint is 0.0015.
    // In dense profile, it must not round to 0.00 and duplicate 0.
    await userEvent.click(screen.getByRole('button', { name: 'Add breakpoint' }))

    const inputs = screen.getAllByLabelText(/Breakpoint \d station/)
    const stationValues = inputs.map((input) => (input as HTMLInputElement).value)
    expect(stationValues).toContain('0.0005')

    // Strictly increasing stations check
    const numericStations = stationValues.map(Number)
    for (let i = 1; i < numericStations.length; ++i) {
      expect(numericStations[i]!).toBeGreaterThan(numericStations[i - 1]!)
    }

    await userEvent.click(screen.getByRole('button', { name: 'Save profile' }))
    expect(updateSpy).toHaveBeenCalled()
  })

  it('safely rejects adding breakpoint when no usable gap exists', async () => {
    const microscopicRoad = create(RoadSummarySchema, {
      roadId: 'road-micro',
      name: 'Microscopic Road',
      length: 0.00001,
      alignmentSegmentCount: 1,
    })
    const microscopicDetails = create(RoadDetailsSchema, {
      roadId: 'road-micro',
      elevationBreakpoints: [
        { station: 0.0, value: 10 },
        { station: 0.00001, value: 12 },
      ],
    })
    useRoadStore.getState().setRoads([microscopicRoad])
    useSelectionStore.getState().select(['road:road-micro'])
    useRoadStore.getState().setDetails(microscopicDetails)

    render(<RoadProfileEditor getEngineClient={() => ({} as any)} />)

    await userEvent.click(screen.getByRole('button', { name: 'Add breakpoint' }))
    expect(screen.getByText('No usable gap remains to insert a new breakpoint.')).toBeInTheDocument()
  })
})
