import { create } from '@bufbuild/protobuf'
import {
  JobCancelCommandSchema,
  JobListCommandSchema,
  GeoBoundsSchema,
  TerrainDownloadSelectedCommandSchema,
  TerrainGetDatasetCommandSchema,
  TerrainGetSceneCommandSchema,
  TerrainImportDatasetCommandSchema,
  TerrainListDatasetsCommandSchema,
  TerrainListSourcesCommandSchema,
  TerrainPlanDownloadCommandSchema,
  TerrainProbeSourceCommandSchema,
  TerrainRegenerateTilesCommandSchema,
  type CommandEnvelope,
  type JobRecord,
  type TerrainDatasetInfo,
  type TerrainListSourcesResult,
  type TerrainPlanDownloadResult,
  type TerrainProbeSourceResult,
  type TerrainSceneResult,
} from '@infraforge/protocol'
import { CommandErrorCode } from '@infraforge/protocol'
import { EngineCommandError, type EngineClient } from '../../lib/engineSession'
import { expectFailure, ProjectCommandFailure, type ResultOutcome } from '../project/projectApi'
import { useTerrainStore } from './terrainStore'

type TerrainCommand = NonNullable<CommandEnvelope['command']>

async function sendTerrainCommand(client: EngineClient, command: TerrainCommand): Promise<ResultOutcome> {
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

// Read-only source detection: CRS presence/resolution, raster geometry,
// NoData, units. Runs before any import so problems surface typed.
export async function probeTerrainSource(
  client: EngineClient,
  path: string,
): Promise<TerrainProbeSourceResult> {
  const command = create(TerrainProbeSourceCommandSchema, { path })
  const outcome = await sendTerrainCommand(client, { case: 'terrainProbeSource', value: command })
  if (outcome.case !== 'terrainProbeSourceResult' || !outcome.value.source) {
    throw expectFailure(outcome)
  }
  useTerrainStore.getState().setProbe(outcome.value)
  return outcome.value
}

// Starts the import job: project-owned copy, CRS-validated registration,
// then derived tile generation. Progress and cancellation flow through
// job events and job.cancel.
export async function importTerrainDataset(client: EngineClient, path: string, displayName: string, elevationUnitOverride = ''): Promise<string> {
  const store = useTerrainStore.getState()
  store.setImporting(true)
  store.setLastError(null)
  try {
    const command = create(TerrainImportDatasetCommandSchema, { path, displayName, elevationUnitOverride })
    const outcome = await sendTerrainCommand(client, { case: 'terrainImportDataset', value: command })
    if (outcome.case !== 'jobStarted') {
      throw expectFailure(outcome)
    }
    return outcome.value.jobId
  } catch (error) {
    const failure = toFailure(error, 'The terrain import could not be started.')
    useTerrainStore.getState().setLastError(failure.message)
    throw failure
  } finally {
    store.setImporting(false)
  }
}

export async function refreshTerrainDatasets(client: EngineClient): Promise<TerrainDatasetInfo[]> {
  const outcome = await sendTerrainCommand(client, {
    case: 'terrainListDatasets',
    value: create(TerrainListDatasetsCommandSchema, {}),
  })
  if (outcome.case !== 'terrainListDatasetsResult') {
    throw expectFailure(outcome)
  }
  useTerrainStore.getState().setDatasets(outcome.value.datasets)
  return outcome.value.datasets
}

export async function refreshTerrainJobs(client: EngineClient): Promise<JobRecord[]> {
  const outcome = await sendTerrainCommand(client, {
    case: 'jobList',
    value: create(JobListCommandSchema, {}),
  })
  if (outcome.case !== 'jobListResult') {
    throw expectFailure(outcome)
  }
  useTerrainStore.getState().setJobs(outcome.value.jobs)
  return outcome.value.jobs
}

export async function cancelTerrainJob(client: EngineClient, jobId: string): Promise<boolean> {
  const outcome = await sendTerrainCommand(client, {
    case: 'jobCancel',
    value: create(JobCancelCommandSchema, { jobId }),
  })
  if (outcome.case !== 'jobCancelResult') {
    throw expectFailure(outcome)
  }
  return outcome.value.cancelled
}

// Regenerates missing derived tiles after a cancelled or failed tile job.
export async function regenerateTerrainTiles(client: EngineClient, datasetUuid: string): Promise<string> {
  const outcome = await sendTerrainCommand(client, {
    case: 'terrainRegenerateTiles',
    value: create(TerrainRegenerateTilesCommandSchema, { datasetUuid }),
  })
  if (outcome.case !== 'jobStarted') {
    throw expectFailure(outcome)
  }
  return outcome.value.jobId
}

// Renderer scene projection: forwarded verbatim to the native viewport
// through the desktop bridge; the shell and frontend never interpret the
// tile payload.
export async function fetchTerrainScene(client: EngineClient): Promise<TerrainSceneResult | null> {
  const outcome = await sendTerrainCommand(client, {
    case: 'terrainGetScene',
    value: create(TerrainGetSceneCommandSchema, {}),
  })
  if (outcome.case !== 'terrainSceneResult') {
    throw expectFailure(outcome)
  }
  return outcome.value
}

export async function refreshDatasetDetails(
  client: EngineClient,
  datasetUuid: string,
): Promise<TerrainDatasetInfo | null> {
  const outcome = await sendTerrainCommand(client, {
    case: 'terrainGetDataset',
    value: create(TerrainGetDatasetCommandSchema, { datasetUuid }),
  })
  if (outcome.case !== 'terrainGetDatasetResult' || !outcome.value.dataset) {
    throw expectFailure(outcome)
  }
  useTerrainStore.getState().upsertDataset(outcome.value.dataset)
  return outcome.value.dataset
}

// ---- Download Area workflow (Issue #6 BLOCKER 7) ----

export async function listTerrainSources(client: EngineClient): Promise<TerrainListSourcesResult> {
  const outcome = await sendTerrainCommand(client, {
    case: 'terrainListSources',
    value: create(TerrainListSourcesCommandSchema, {}),
  })
  if (outcome.case !== 'terrainListSourcesResult') {
    throw expectFailure(outcome)
  }
  return outcome.value
}

export async function planTerrainDownload(
  client: EngineClient,
  providerId: string,
  area: { west: number; south: number; east: number; north: number },
  tileSizeMetres: number,
  selectedIndices: number[],
): Promise<TerrainPlanDownloadResult> {
  const outcome = await sendTerrainCommand(client, {
    case: 'terrainPlanDownload',
    value: create(TerrainPlanDownloadCommandSchema, {
      providerId,
      area: create(GeoBoundsSchema, area),
      tileSizeMetres,
      selectedIndices,
    }),
  })
  if (outcome.case !== 'terrainPlanDownloadResult') {
    throw expectFailure(outcome)
  }
  return outcome.value
}

export async function downloadSelectedTerrain(
  client: EngineClient,
  providerId: string,
  area: { west: number; south: number; east: number; north: number },
  tileSizeMetres: number,
  selectedIndices: number[],
  displayName: string,
): Promise<string> {
  const outcome = await sendTerrainCommand(client, {
    case: 'terrainDownloadSelected',
    value: create(TerrainDownloadSelectedCommandSchema, {
      providerId,
      area: create(GeoBoundsSchema, area),
      tileSizeMetres,
      selectedIndices,
      displayName,
    }),
  })
  if (outcome.case !== 'jobStarted') {
    throw expectFailure(outcome)
  }
  return outcome.value.jobId
}
