import { describe, expect, it } from 'vitest'
import { create } from '@bufbuild/protobuf'
import {
  RoadLaneSectionInfoSchema,
  RoadLaneInfoSchema,
  type RoadLaneSectionInfo,
} from '@infraforge/protocol'
import type { EngineClient } from '../../lib/engineSession'
import {
  fitRoadSource,
  updateRoadLanes,
  listJunctions,
  createJunction,
  updateJunction,
  deleteJunction,
} from './roadApi'
import { useRoadStore } from './roadStore'

function clientCapturing(command: unknown): EngineClient {
  return {
    sendCommand: async (sent: unknown) => {
      Object.assign(command as object, { sent })
      return {
        case: 'fitRoadSourceResult',
        value: { road: { roadId: 'road-1', revision: 1n } },
      }
    },
  } as EngineClient
}

describe('roadApi', () => {
  it('represents preserve, replace, and clear curvature intent distinctly in fitRoadSource', async () => {
    useRoadStore.getState().reset()

    const preserve: { sent?: any } = {}
    await fitRoadSource(clientCapturing(preserve), 'road-1', { positionTolerance: 2 })
    expect(preserve.sent.value.maxCurvature).toBeUndefined()
    expect(preserve.sent.value.clearMaxCurvature).toBe(false)

    const replace: { sent?: any } = {}
    await fitRoadSource(clientCapturing(replace), 'road-1', {
      maxCurvature: { kind: 'replace', value: 0.02 },
    })
    expect(replace.sent.value.maxCurvature).toBe(0.02)
    expect(replace.sent.value.clearMaxCurvature).toBe(false)

    const clear: { sent?: any } = {}
    await fitRoadSource(clientCapturing(clear), 'road-1', {
      maxCurvature: { kind: 'clear' },
    })
    expect(clear.sent.value.maxCurvature).toBeUndefined()
    expect(clear.sent.value.clearMaxCurvature).toBe(true)
  })

  it('sends updateRoadLanes and updates store', async () => {
    useRoadStore.getState().reset()

    const captured: { sent?: any } = {}
    const client: EngineClient = {
      sendCommand: async (sent: unknown) => {
        Object.assign(captured, { sent })
        return {
          case: 'updateRoadLanesResult',
          value: {
            road: {
              roadId: 'road-1',
              name: 'Main Street',
              length: 100,
              alignmentSegmentCount: 1,
              sourceProvider: '',
              sourceId: '',
              protectedAnchorCount: 0,
              revision: 2n,
            },
          },
        }
      },
    } as unknown as EngineClient

    const section: RoadLaneSectionInfo = create(RoadLaneSectionInfoSchema, {
      sectionIndex: 0,
      startStation: 0,
      endStation: 100,
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
      ],
    })

    const road = await updateRoadLanes(client, 'road-1', [section])
    expect(captured.sent.case).toBe('updateRoadLanes')
    expect(captured.sent.value.roadId).toBe('road-1')
    expect(captured.sent.value.laneSections.length).toBe(1)
    expect(road.roadId).toBe('road-1')
    expect(useRoadStore.getState().roads.some((r) => r.roadId === 'road-1')).toBe(true)
  })

  it('performs junction CRUD operations through roadApi', async () => {
    useRoadStore.getState().reset()

    // 1. Create Junction
    const clientCreate: EngineClient = {
      sendCommand: async () => ({
        case: 'createJunctionResult',
        value: {
          junction: {
            junctionId: 'junc-1',
            name: 'Crossroads',
            type: 'priority',
            posX: 10,
            posY: 20,
            elevation: 5,
            revision: 1n,
            approaches: [],
            connections: [],
          },
        },
      }),
    } as unknown as EngineClient

    const created = await createJunction(clientCreate, 'Crossroads', 10, 20, 5, 'priority')
    expect(created.junctionId).toBe('junc-1')
    expect(useRoadStore.getState().junctions.length).toBe(1)
    expect(useRoadStore.getState().junctions[0]!.name).toBe('Crossroads')

    // 2. Update Junction
    const clientUpdate: EngineClient = {
      sendCommand: async () => ({
        case: 'updateJunctionResult',
        value: {
          junction: {
            junctionId: 'junc-1',
            name: 'Roundabout Crossroads',
            type: 'roundabout',
            posX: 10,
            posY: 20,
            elevation: 5,
            revision: 2n,
            approaches: [],
            connections: [],
          },
        },
      }),
    } as unknown as EngineClient

    const updated = await updateJunction(clientUpdate, 'junc-1', {
      name: 'Roundabout Crossroads',
      type: 'roundabout',
    })
    expect(updated.type).toBe('roundabout')
    expect(useRoadStore.getState().junctions[0]!.name).toBe('Roundabout Crossroads')

    // 3. List Junctions
    const clientList: EngineClient = {
      sendCommand: async () => ({
        case: 'listJunctionsResult',
        value: {
          junctions: [
            {
              junctionId: 'junc-1',
              name: 'Roundabout Crossroads',
              type: 'roundabout',
              posX: 10,
              posY: 20,
              elevation: 5,
              revision: 2n,
              approaches: [],
              connections: [],
            },
          ],
        },
      }),
    } as unknown as EngineClient

    const listed = await listJunctions(clientList)
    expect(listed.length).toBe(1)

    // 4. Delete Junction
    const clientDelete: EngineClient = {
      sendCommand: async () => ({
        case: 'deleteJunctionResult',
        value: {},
      }),
    } as unknown as EngineClient

    await deleteJunction(clientDelete, 'junc-1')
    expect(useRoadStore.getState().junctions.length).toBe(0)
  })
})
