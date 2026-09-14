import { create } from '@bufbuild/protobuf'
import {
  CreateRoadCommandSchema,
  DeleteRoadCommandSchema,
  RenameRoadCommandSchema,
  InsertRoadControlCommandSchema,
  MoveRoadControlCommandSchema,
  DeleteRoadControlCommandSchema,
  FitRoadSourceCommandSchema,
  UpdateRoadElevationCommandSchema,
  UpdateRoadSuperelevationCommandSchema,
  ListRoadsCommandSchema,
  GetRoadCommandSchema,
  GetRoadSceneCommandSchema,
  UndoRoadCommandSchema,
  RedoRoadCommandSchema,
  type CommandEnvelope,
  type RoadSummary,
  type RoadDetails,
  type RoadSceneResult,
} from '@infraforge/protocol'
import { CommandErrorCode } from '@infraforge/protocol'
import { EngineCommandError, type EngineClient } from '../../lib/engineSession'
import { expectFailure, ProjectCommandFailure, type ResultOutcome } from '../project/projectApi'
import { useRoadStore } from './roadStore'

type RoadCommand = NonNullable<CommandEnvelope['command']>

async function sendRoadCommand(client: EngineClient, command: RoadCommand): Promise<ResultOutcome> {
  return client.sendCommand(command)
}

function toFailure(error: unknown, fallback: string): ProjectCommandFailure {
  return error instanceof ProjectCommandFailure
    ? error
    : new ProjectCommandFailure(
        CommandErrorCode.INTERNAL,
        error instanceof EngineCommandError ? error.message : fallback,
      )
}

export async function createRoad(
  client: EngineClient,
  name: string,
  sourceEastings: number[],
  sourceNorthings: number[],
  positionTolerance: number,
  sourceElevations: number[] = [],
  protectedAnchorIndices: number[] = [],
  maxCurvature?: number,
): Promise<RoadSummary> {
  const command = create(CreateRoadCommandSchema, {
    name,
    sourceEastings,
    sourceNorthings,
    sourceElevations,
    positionTolerance,
    protectedAnchorIndices,
    maxCurvature,
  })
  const outcome = await sendRoadCommand(client, { case: 'createRoad', value: command })
  if (outcome.case !== 'createRoadResult' || !outcome.value.road) {
    throw expectFailure(outcome)
  }
  useRoadStore.getState().upsertRoad(outcome.value.road)
  return outcome.value.road
}

export async function deleteRoad(client: EngineClient, roadId: string): Promise<void> {
  const command = create(DeleteRoadCommandSchema, { roadId })
  const outcome = await sendRoadCommand(client, { case: 'deleteRoad', value: command })
  if (outcome.case !== 'deleteRoadResult') {
    throw expectFailure(outcome)
  }
  useRoadStore.getState().removeRoad(roadId)
}

export async function renameRoad(client: EngineClient, roadId: string, name: string): Promise<RoadSummary> {
  const command = create(RenameRoadCommandSchema, { roadId, name })
  const outcome = await sendRoadCommand(client, { case: 'renameRoad', value: command })
  if (outcome.case !== 'renameRoadResult' || !outcome.value.road) {
    throw expectFailure(outcome)
  }
  useRoadStore.getState().upsertRoad(outcome.value.road)
  return outcome.value.road
}

export async function insertRoadControl(
  client: EngineClient,
  roadId: string,
  insertBeforeIndex: number,
  easting: number,
  northing: number,
  elevation?: number,
): Promise<RoadSummary> {
  const command = create(InsertRoadControlCommandSchema, {
    roadId,
    insertBeforeIndex,
    easting,
    northing,
    elevation,
  })
  const outcome = await sendRoadCommand(client, { case: 'insertRoadControl', value: command })
  if (outcome.case !== 'insertRoadControlResult' || !outcome.value.road) {
    throw expectFailure(outcome)
  }
  useRoadStore.getState().upsertRoad(outcome.value.road)
  return outcome.value.road
}

export async function moveRoadControl(
  client: EngineClient,
  roadId: string,
  controlIndex: number,
  easting: number,
  northing: number,
  elevation?: number,
): Promise<RoadSummary> {
  const command = create(MoveRoadControlCommandSchema, {
    roadId,
    controlIndex,
    easting,
    northing,
    elevation,
  })
  const outcome = await sendRoadCommand(client, { case: 'moveRoadControl', value: command })
  if (outcome.case !== 'moveRoadControlResult' || !outcome.value.road) {
    throw expectFailure(outcome)
  }
  useRoadStore.getState().upsertRoad(outcome.value.road)
  return outcome.value.road
}

export async function deleteRoadControl(
  client: EngineClient,
  roadId: string,
  controlIndex: number,
): Promise<RoadSummary> {
  const command = create(DeleteRoadControlCommandSchema, { roadId, controlIndex })
  const outcome = await sendRoadCommand(client, { case: 'deleteRoadControl', value: command })
  if (outcome.case !== 'deleteRoadControlResult' || !outcome.value.road) {
    throw expectFailure(outcome)
  }
  useRoadStore.getState().upsertRoad(outcome.value.road)
  return outcome.value.road
}

export async function fitRoadSource(
  client: EngineClient,
  roadId: string,
  positionTolerance: number,
  maxCurvature?: number,
): Promise<RoadSummary> {
  const command = create(FitRoadSourceCommandSchema, { roadId, positionTolerance, maxCurvature })
  const outcome = await sendRoadCommand(client, { case: 'fitRoadSource', value: command })
  if (outcome.case !== 'fitRoadSourceResult' || !outcome.value.road) {
    throw expectFailure(outcome)
  }
  useRoadStore.getState().upsertRoad(outcome.value.road)
  return outcome.value.road
}

export async function updateRoadElevation(
  client: EngineClient,
  roadId: string,
  stations: number[],
  elevations: number[],
): Promise<RoadSummary> {
  const command = create(UpdateRoadElevationCommandSchema, { roadId, stations, elevations })
  const outcome = await sendRoadCommand(client, { case: 'updateRoadElevation', value: command })
  if (outcome.case !== 'updateRoadElevationResult' || !outcome.value.road) {
    throw expectFailure(outcome)
  }
  useRoadStore.getState().upsertRoad(outcome.value.road)
  return outcome.value.road
}

export async function updateRoadSuperelevation(
  client: EngineClient,
  roadId: string,
  stations: number[],
  superelevations: number[],
): Promise<RoadSummary> {
  const command = create(UpdateRoadSuperelevationCommandSchema, { roadId, stations, superelevations })
  const outcome = await sendRoadCommand(client, { case: 'updateRoadSuperelevation', value: command })
  if (outcome.case !== 'updateRoadSuperelevationResult' || !outcome.value.road) {
    throw expectFailure(outcome)
  }
  useRoadStore.getState().upsertRoad(outcome.value.road)
  return outcome.value.road
}

export async function listRoads(client: EngineClient): Promise<RoadSummary[]> {
  const outcome = await sendRoadCommand(client, {
    case: 'listRoads',
    value: create(ListRoadsCommandSchema, {}),
  })
  if (outcome.case !== 'listRoadsResult') {
    throw expectFailure(outcome)
  }
  useRoadStore.getState().setRoads(outcome.value.roads)
  return outcome.value.roads
}

export async function getRoad(client: EngineClient, roadId: string): Promise<RoadDetails | null> {
  const outcome = await sendRoadCommand(client, {
    case: 'getRoad',
    value: create(GetRoadCommandSchema, { roadId }),
  })
  if (outcome.case !== 'getRoadResult' || !outcome.value.road) {
    throw expectFailure(outcome)
  }
  useRoadStore.getState().setDetails(outcome.value.road)
  return outcome.value.road
}

// Renderer scene projection: forwarded to the native viewport through
// the desktop bridge; the shell and frontend never interpret the mesh
// payload.
export async function fetchRoadScene(client: EngineClient): Promise<RoadSceneResult | null> {
  const outcome = await sendRoadCommand(client, {
    case: 'getRoadScene',
    value: create(GetRoadSceneCommandSchema, {}),
  })
  if (outcome.case !== 'roadSceneResult') {
    throw expectFailure(outcome)
  }
  return outcome.value
}

// Blocker 18: undo/redo exposed through the canonical roadApi so commands
// do not directly construct protocol messages. Returns the optional
// summary from the result (populated when the road still exists).
export async function undoRoadEdit(
  client: EngineClient,
  roadId: string = '',
): Promise<RoadSummary | null> {
  const command = create(UndoRoadCommandSchema, { roadId })
  const outcome = await sendRoadCommand(client, { case: 'undoRoad', value: command })
  if (outcome.case !== 'undoRoadResult') {
    throw expectFailure(outcome)
  }
  if (outcome.value.road) {
    useRoadStore.getState().upsertRoad(outcome.value.road)
    return outcome.value.road
  }
  return null
}

export async function redoRoadEdit(
  client: EngineClient,
  roadId: string = '',
): Promise<RoadSummary | null> {
  const command = create(RedoRoadCommandSchema, { roadId })
  const outcome = await sendRoadCommand(client, { case: 'redoRoad', value: command })
  if (outcome.case !== 'redoRoadResult') {
    throw expectFailure(outcome)
  }
  if (outcome.value.road) {
    useRoadStore.getState().upsertRoad(outcome.value.road)
    return outcome.value.road
  }
  return null
}

export { toFailure }
