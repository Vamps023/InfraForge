import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { cleanup, render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { RoadProfileEditor } from './RoadProfileEditor'
import { useRoadStore } from './roadStore'
import { useSelectionStore } from '../../editor/selection/selectionStore'
import { create } from '@bufbuild/protobuf'
import { RoadDetailsSchema, RoadSummarySchema } from '@infraforge/protocol'
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
    expect(screen.getByText('100.00 m')).toBeInTheDocument()
    expect(screen.getByText('Authored')).toBeInTheDocument()
    expect(screen.getByText('4')).toBeInTheDocument()
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
    expect(screen.getByText('Loading…')).toBeInTheDocument()

    // Apply button must be disabled while details do not match selected road
    const applyButton = screen.getByRole('button', { name: /Loading details…/ })
    expect(applyButton).toBeDisabled()

    // Even if somehow clicked, updateRoadElevation must not be invoked
    await userEvent.click(applyButton)
    expect(updateRoadElevationSpy).not.toHaveBeenCalled()

    // 3. Hydrate Road B details
    useRoadStore.getState().setDetails(detailsB)
    rerender(<RoadProfileEditor getEngineClient={() => ({} as any)} />)

    expect(screen.getByText('Default flat')).toBeInTheDocument()
    const activeApplyBtn = screen.getByRole('button', { name: 'Apply to Alignment' })
    expect(activeApplyBtn).not.toBeDisabled()

    // 4. Click Apply — must target Road B with Road B's 4 control points
    await userEvent.click(activeApplyBtn)
    expect(updateRoadElevationSpy).toHaveBeenCalledTimes(1)
    expect(updateRoadElevationSpy).toHaveBeenCalledWith(
      expect.anything(),
      'road-B',
      expect.arrayContaining([0, 250]), // stations across road B length
      expect.arrayContaining([0, 0, 0, 0]), // 4 elevation points matching detailsB.controlPoints.length
    )
  })
})
