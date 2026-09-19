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
  UpdateRoadWidthCommandSchema,
  ConformRoadToTerrainCommandSchema,
  ListRoadsCommandSchema,
  GetRoadCommandSchema,
  GetRoadSceneCommandSchema,
  UndoRoadCommandSchema,
  RedoRoadCommandSchema,
  UpdateRoadLanesCommandSchema,
  CreateJunctionCommandSchema,
  UpdateJunctionCommandSchema,
  DeleteJunctionCommandSchema,
  ListJunctionsCommandSchema,
  type CommandEnvelope,
  type RoadSummary,
  type RoadDetails,
  type RoadSceneResult,
  type RoadLaneSectionInfo,
  type JunctionInfo,
  type JunctionApproachInfo,
  type JunctionConnectionInfo,
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
    const failure = expectFailure(outcome)
    useRoadStore.getState().setLastError(failure.message)
    throw failure
  }
  useRoadStore.getState().setLastError(null)
  useRoadStore.getState().upsertRoad(outcome.value.road)
  return outcome.value.road
}

// Wrap a road command so failures are surfaced to the road store as
// diagnostics (source: 'road') before re-throwing. Successful commands
// clear the road error.
async function withRoadError<T>(fn: () => Promise<T>): Promise<T> {
  try {
    const result = await fn()
    useRoadStore.getState().setLastError(null)
    return result
  } catch (error) {
    const failure = error instanceof ProjectCommandFailure ? error : toFailure(error, 'Road command failed')
    useRoadStore.getState().setLastError(failure.message)
    throw failure
  }
}

export async function deleteRoad(client: EngineClient, roadId: string): Promise<void> {
  await withRoadError(async () => {
    const command = create(DeleteRoadCommandSchema, { roadId })
    const outcome = await sendRoadCommand(client, { case: 'deleteRoad', value: command })
    if (outcome.case !== 'deleteRoadResult') {
      throw expectFailure(outcome)
    }
    useRoadStore.getState().removeRoad(roadId)
  })
}

export async function renameRoad(client: EngineClient, roadId: string, name: string): Promise<RoadSummary> {
  return withRoadError(async () => {
    const command = create(RenameRoadCommandSchema, { roadId, name })
    const outcome = await sendRoadCommand(client, { case: 'renameRoad', value: command })
    if (outcome.case !== 'renameRoadResult' || !outcome.value.road) {
      throw expectFailure(outcome)
    }
    useRoadStore.getState().upsertRoad(outcome.value.road)
    return outcome.value.road
  })
}

export async function insertRoadControl(
  client: EngineClient,
  roadId: string,
  insertBeforeIndex: number,
  easting: number,
  northing: number,
  elevation?: number,
): Promise<RoadSummary> {
  return withRoadError(async () => {
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
  })
}

export async function moveRoadControl(
  client: EngineClient,
  roadId: string,
  controlIndex: number,
  easting: number,
  northing: number,
  elevation?: number,
): Promise<RoadSummary> {
  return withRoadError(async () => {
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
  })
}

export async function deleteRoadControl(
  client: EngineClient,
  roadId: string,
  controlIndex: number,
): Promise<RoadSummary> {
  return withRoadError(async () => {
    const command = create(DeleteRoadControlCommandSchema, { roadId, controlIndex })
    const outcome = await sendRoadCommand(client, { case: 'deleteRoadControl', value: command })
    if (outcome.case !== 'deleteRoadControlResult' || !outcome.value.road) {
      throw expectFailure(outcome)
    }
    useRoadStore.getState().upsertRoad(outcome.value.road)
    return outcome.value.road
  })
}

export type MaxCurvatureChange =
  | { kind: 'preserve' }
  | { kind: 'replace'; value: number }
  | { kind: 'clear' }

export interface FitRoadSourceOptions {
  positionTolerance?: number
  maxCurvature?: MaxCurvatureChange
}

export async function fitRoadSource(
  client: EngineClient,
  roadId: string,
  options: FitRoadSourceOptions = {},
): Promise<RoadSummary> {
  return withRoadError(async () => {
    const maxCurvature = options.maxCurvature ?? { kind: 'preserve' as const }
    const command = create(FitRoadSourceCommandSchema, {
      roadId,
      positionTolerance: options.positionTolerance,
      maxCurvature: maxCurvature.kind === 'replace' ? maxCurvature.value : undefined,
      clearMaxCurvature: maxCurvature.kind === 'clear',
    })
    const outcome = await sendRoadCommand(client, { case: 'fitRoadSource', value: command })
    if (outcome.case !== 'fitRoadSourceResult' || !outcome.value.road) {
      throw expectFailure(outcome)
    }
    useRoadStore.getState().upsertRoad(outcome.value.road)
    return outcome.value.road
  })
}

export async function updateRoadElevation(
  client: EngineClient,
  roadId: string,
  stations: number[],
  elevations: number[],
): Promise<RoadSummary> {
  return withRoadError(async () => {
    const command = create(UpdateRoadElevationCommandSchema, { roadId, stations, elevations })
    const outcome = await sendRoadCommand(client, { case: 'updateRoadElevation', value: command })
    if (outcome.case !== 'updateRoadElevationResult' || !outcome.value.road) {
      throw expectFailure(outcome)
    }
    useRoadStore.getState().upsertRoad(outcome.value.road)
    return outcome.value.road
  })
}

export async function updateRoadSuperelevation(
  client: EngineClient,
  roadId: string,
  stations: number[],
  superelevations: number[],
): Promise<RoadSummary> {
  return withRoadError(async () => {
    const command = create(UpdateRoadSuperelevationCommandSchema, { roadId, stations, superelevations })
    const outcome = await sendRoadCommand(client, { case: 'updateRoadSuperelevation', value: command })
    if (outcome.case !== 'updateRoadSuperelevationResult' || !outcome.value.road) {
      throw expectFailure(outcome)
    }
    useRoadStore.getState().upsertRoad(outcome.value.road)
    return outcome.value.road
  })
}

export async function updateRoadWidth(
  client: EngineClient,
  roadId: string,
  stations: number[],
  leftWidths: number[],
  rightWidths: number[],
): Promise<RoadSummary> {
  return withRoadError(async () => {
    const command = create(UpdateRoadWidthCommandSchema, { roadId, stations, leftWidths, rightWidths })
    const outcome = await sendRoadCommand(client, { case: 'updateRoadWidth', value: command })
    if (outcome.case !== 'updateRoadWidthResult' || !outcome.value.road) {
      throw expectFailure(outcome)
    }
    useRoadStore.getState().upsertRoad(outcome.value.road)
    return outcome.value.road
  })
}

export async function conformRoadToTerrain(
  client: EngineClient,
  roadId: string,
  stationInterval: number,
  verticalOffset: number,
  datasetId = '',
): Promise<RoadSummary> {
  return withRoadError(async () => {
    const command = create(ConformRoadToTerrainCommandSchema, {
      roadId, datasetId, stationInterval, verticalOffset,
    })
    const outcome = await sendRoadCommand(client, { case: 'conformRoadToTerrain', value: command })
    if (outcome.case !== 'conformRoadToTerrainResult' || !outcome.value.road) {
      throw expectFailure(outcome)
    }
    useRoadStore.getState().upsertRoad(outcome.value.road)
    return outcome.value.road
  })
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
  if (!outcome.value.summary) {
    throw new ProjectCommandFailure(CommandErrorCode.INTERNAL, 'Engine returned road details without a summary projection')
  }
  useRoadStore.getState().applyRoadProjection(outcome.value.road, outcome.value.summary)
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
  return withRoadError(async () => {
    const command = create(UndoRoadCommandSchema, { roadId })
    const outcome = await sendRoadCommand(client, { case: 'undoRoad', value: command })
    if (outcome.case !== 'undoRoadResult') {
      throw expectFailure(outcome)
    }
    if (outcome.value.road) {
      useRoadStore.getState().upsertRoad(outcome.value.road)
      return outcome.value.road
    }
    if (outcome.value.roadId && !outcome.value.existsAfterOperation) {
      useRoadStore.getState().removeRoad(outcome.value.roadId)
    }
    return null
  })
}

export async function redoRoadEdit(
  client: EngineClient,
  roadId: string = '',
): Promise<RoadSummary | null> {
  return withRoadError(async () => {
    const command = create(RedoRoadCommandSchema, { roadId })
    const outcome = await sendRoadCommand(client, { case: 'redoRoad', value: command })
    if (outcome.case !== 'redoRoadResult') {
      throw expectFailure(outcome)
    }
    if (outcome.value.road) {
      useRoadStore.getState().upsertRoad(outcome.value.road)
      return outcome.value.road
    }
    if (outcome.value.roadId && !outcome.value.existsAfterOperation) {
      useRoadStore.getState().removeRoad(outcome.value.roadId)
    }
    return null
  })
}

export async function updateRoadLanes(
  client: EngineClient,
  roadId: string,
  laneSections: RoadLaneSectionInfo[],
): Promise<RoadSummary> {
  return withRoadError(async () => {
    const command = create(UpdateRoadLanesCommandSchema, { roadId, laneSections })
    const outcome = await sendRoadCommand(client, { case: 'updateRoadLanes', value: command })
    if (outcome.case !== 'updateRoadLanesResult' || !outcome.value.road) {
      throw expectFailure(outcome)
    }
    useRoadStore.getState().upsertRoad(outcome.value.road)
    return outcome.value.road
  })
}

export async function listJunctions(client: EngineClient): Promise<JunctionInfo[]> {
  return withRoadError(async () => {
    const command = create(ListJunctionsCommandSchema, {})
    const outcome = await sendRoadCommand(client, { case: 'listJunctions', value: command })
    if (outcome.case !== 'listJunctionsResult') {
      throw expectFailure(outcome)
    }
    useRoadStore.getState().setJunctions(outcome.value.junctions)
    return outcome.value.junctions
  })
}

export async function createJunction(
  client: EngineClient,
  name: string,
  posX: number,
  posY: number,
  elevation = 0,
  type = 'priority',
  approaches: JunctionApproachInfo[] = [],
  connections: JunctionConnectionInfo[] = [],
): Promise<JunctionInfo> {
  return withRoadError(async () => {
    const command = create(CreateJunctionCommandSchema, {
      name,
      posX,
      posY,
      elevation,
      type,
      approaches,
      connections,
    })
    const outcome = await sendRoadCommand(client, { case: 'createJunction', value: command })
    if (outcome.case !== 'createJunctionResult' || !outcome.value.junction) {
      throw expectFailure(outcome)
    }
    useRoadStore.getState().upsertJunction(outcome.value.junction)
    return outcome.value.junction
  })
}

export async function updateJunction(
  client: EngineClient,
  junctionId: string,
  updates: {
    name?: string
    posX?: number
    posY?: number
    elevation?: number
    type?: string
    approaches?: JunctionApproachInfo[]
    connections?: JunctionConnectionInfo[]
  },
): Promise<JunctionInfo> {
  return withRoadError(async () => {
    const command = create(UpdateJunctionCommandSchema, {
      junctionId,
      name: updates.name ?? '',
      posX: updates.posX ?? 0,
      posY: updates.posY ?? 0,
      elevation: updates.elevation ?? 0,
      type: updates.type ?? '',
      approaches: updates.approaches ?? [],
      connections: updates.connections ?? [],
    })
    const outcome = await sendRoadCommand(client, { case: 'updateJunction', value: command })
    if (outcome.case !== 'updateJunctionResult' || !outcome.value.junction) {
      throw expectFailure(outcome)
    }
    useRoadStore.getState().upsertJunction(outcome.value.junction)
    return outcome.value.junction
  })
}

export async function deleteJunction(client: EngineClient, junctionId: string): Promise<void> {
  return withRoadError(async () => {
    const command = create(DeleteJunctionCommandSchema, { junctionId })
    const outcome = await sendRoadCommand(client, { case: 'deleteJunction', value: command })
    if (outcome.case !== 'deleteJunctionResult') {
      throw expectFailure(outcome)
    }
    useRoadStore.getState().removeJunction(junctionId)
  })
}

export { toFailure }
