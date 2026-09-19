import { describe, it, expect } from 'vitest'
import { render, screen, fireEvent } from '@testing-library/react'
import { create } from '@bufbuild/protobuf'
import {
  RoadSummarySchema,
  RoadDetailsSchema,
  RoadLaneSectionInfoSchema,
  RoadLaneInfoSchema,
} from '@infraforge/protocol'
import { RoadLaneEditor, LANE_PRESETS } from './RoadLaneEditor'
import type { EngineClient } from '../../lib/engineSession'

describe('RoadLaneEditor', () => {
  const road = create(RoadSummarySchema, {
    roadId: 'road-test',
    name: 'Boulevard',
    length: 200,
    alignmentSegmentCount: 2,
    revision: 1n,
  })

  const details = create(RoadDetailsSchema, {
    roadId: 'road-test',
    name: 'Boulevard',
    length: 200,
    laneSections: [
      create(RoadLaneSectionInfoSchema, {
        sectionIndex: 0,
        startStation: 0,
        endStation: 200,
        lanes: [
          create(RoadLaneInfoSchema, {
            laneId: 'lane-l1',
            sectionIndex: 0,
            side: 'left',
            laneIndex: 0,
            type: 'driving',
            direction: 'backward',
            width: 3.5,
          }),
          create(RoadLaneInfoSchema, {
            laneId: 'lane-r1',
            sectionIndex: 0,
            side: 'right',
            laneIndex: 0,
            type: 'driving',
            direction: 'forward',
            width: 3.5,
          }),
        ],
      }),
    ],
  })

  it('renders existing road lane sections and cross-section preview', () => {
    const mockClient = {} as EngineClient
    render(<RoadLaneEditor road={road} details={details} getEngineClient={() => mockClient} />)

    expect(screen.getByText(/Left Width:/i)).toBeDefined()
    expect(screen.getByText(/Right Width:/i)).toBeDefined()
    expect(screen.getByText(/Total Road Width:/i)).toBeDefined()
    expect(screen.getByLabelText('Cross-section diagram')).toBeDefined()
    expect(screen.getByText('Left Lanes (Outward from Center)')).toBeDefined()
    expect(screen.getByText('Right Lanes (Outward from Center)')).toBeDefined()
  })

  it('allows adding and removing lanes', () => {
    const mockClient = {} as EngineClient
    render(<RoadLaneEditor road={road} details={details} getEngineClient={() => mockClient} />)

    const addLeftBtn = screen.getByText('+ Add Left Lane')
    fireEvent.click(addLeftBtn)

    // Now there should be L1 and L2
    expect(screen.getByText('L2')).toBeDefined()

    // Remove the newly added lane
    const removeBtns = screen.getAllByRole('button', { name: 'Remove' })
    fireEvent.click(removeBtns[removeBtns.length - 1]!)
  })

  it('applies presets correctly', () => {
    const mockClient = {} as EngineClient
    render(<RoadLaneEditor road={road} details={details} getEngineClient={() => mockClient} />)

    const presetSelect = screen.getByLabelText('Lane presets')
    fireEvent.change(presetSelect, { target: { value: 'four-lane-divided' } })

    expect(screen.getByText(/Applied preset: 4-Lane Divided/i)).toBeDefined()
    expect(screen.getByText('L1')).toBeDefined()
    expect(screen.getByText('L2')).toBeDefined()
    expect(screen.getByText('R1')).toBeDefined()
    expect(screen.getByText('R2')).toBeDefined()
  })

  it('validates widths and sends updateRoadLanes command on save', async () => {
    const capturedCommands: any[] = []
    const mockClient = {
      sendCommand: async (cmd: any) => {
        capturedCommands.push(cmd)
        if (cmd.case === 'updateRoadLanes') {
          return {
            case: 'updateRoadLanesResult',
            value: { road },
          }
        }
        return {
          case: 'getRoadResult',
          value: { road: details, summary: road },
        }
      },
    } as unknown as EngineClient

    render(<RoadLaneEditor road={road} details={details} getEngineClient={() => mockClient} />)

    const saveBtn = screen.getByRole('button', { name: 'Save Lanes' })
    fireEvent.click(saveBtn)

    // Await async action
    await new Promise((resolve) => setTimeout(resolve, 50))
    expect(capturedCommands.length).toBeGreaterThanOrEqual(1)
    const updateCmd = capturedCommands.find((c) => c.case === 'updateRoadLanes')
    expect(updateCmd).toBeDefined()
    expect(updateCmd.value.roadId).toBe('road-test')
    expect(updateCmd.value.laneSections.length).toBe(1)
  })
})
