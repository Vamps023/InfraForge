import { create } from '@bufbuild/protobuf'
import {
  AxisConvention,
  CloseProjectCommandSchema,
  CommandEnvelopeSchema,
  CommandErrorCode,
  CreateProjectCommandSchema,
  GeoreferenceConfigSchema,
  GetProjectSummaryCommandSchema,
  OpenProjectCommandSchema,
  ProjectSummarySchema,
  SaveProjectCommandSchema,
  type CommandEnvelope,
  type ProjectSummary,
  type ResultEnvelope,
  type TrafficSide,
} from '@infraforge/protocol'
import { EngineCommandError, type EngineClient } from '../../lib/engineSession'
import { useProjectStore, type ProjectOperation } from './projectStore'

type ProjectCommand = NonNullable<CommandEnvelope['command']>
type ResultOutcome = NonNullable<ResultEnvelope['outcome']>

export class ProjectCommandFailure extends Error {
  constructor(
    readonly code: CommandErrorCode,
    message: string,
  ) {
    super(message)
  }
}

export interface CreateProjectInput {
  displayName: string
  parentDirectory: string
  horizontalCrs: string
  linearUnit: string
  trafficSide: Exclude<TrafficSide, TrafficSide.UNSPECIFIED>
}

const failureFallbackMessages: Record<CommandErrorCode, string> = {
  [CommandErrorCode.UNSPECIFIED]: 'The native engine reported an unknown project failure.',
  [CommandErrorCode.INVALID_ARGUMENT]: 'The project request was rejected as invalid.',
  [CommandErrorCode.PROJECT_NOT_OPEN]: 'No project is open.',
  [CommandErrorCode.PROJECT_ALREADY_OPEN]: 'A project is already open.',
  [CommandErrorCode.PROJECT_DIRECTORY_INVALID]: 'That directory cannot be used as a project location.',
  [CommandErrorCode.PROJECT_FORMAT_UNSUPPORTED]: 'This project format is not supported by this build.',
  [CommandErrorCode.SCHEMA_VERSION_UNSUPPORTED]:
    'The project uses a newer schema than this application supports.',
  [CommandErrorCode.PERSISTENCE_FAILURE]:
    'The native engine could not complete the project storage operation.',
  [CommandErrorCode.INTERNAL]: 'An internal engine error occurred.',
}

function describeFailure(code: CommandErrorCode, message: string): string {
  if (message.trim().length > 0) {
    return message
  }
  return failureFallbackMessages[code]
}

function expectFailure(outcome: ResultOutcome): ProjectCommandFailure {
  if (outcome.case === 'error') {
    return new ProjectCommandFailure(
      outcome.value.code,
      describeFailure(outcome.value.code, outcome.value.message),
    )
  }
  return new ProjectCommandFailure(
    CommandErrorCode.INTERNAL,
    'The native engine returned an unexpected result for this command.',
  )
}

function expectSummary(outcome: ResultOutcome): ProjectSummary {
  if (outcome.case !== 'projectState' || !outcome.value.summary) {
    throw expectFailure(outcome)
  }
  // Re-create through the schema so the store holds its own projection
  // object instead of a reference into the wire payload.
  const summary = create(ProjectSummarySchema, outcome.value.summary)
  useProjectStore.getState().setSummary(summary)
  return summary
}

async function runProjectCommand<T>(
  operation: ProjectOperation,
  client: EngineClient,
  command: ProjectCommand,
  interpretResult: (outcome: ResultOutcome) => T,
): Promise<T> {
  const store = useProjectStore.getState()
  store.setOperation(operation)
  store.setLastError(null)
  try {
    const envelope = create(CommandEnvelopeSchema, { command })
    const outcome = await client.sendCommand(envelope.command)
    return interpretResult(outcome)
  } catch (error) {
    const failure =
      error instanceof ProjectCommandFailure
        ? error
        : new ProjectCommandFailure(
            CommandErrorCode.INTERNAL,
            error instanceof EngineCommandError
              ? error.message
              : 'The native engine could not be reached for this project command.',
          )
    useProjectStore.getState().setLastError({ code: String(failure.code), message: failure.message })
    throw failure
  } finally {
    useProjectStore.getState().setOperation(null)
  }
}

export async function createProject(client: EngineClient, input: CreateProjectInput): Promise<ProjectSummary> {
  const command = create(CreateProjectCommandSchema, {
    displayName: input.displayName,
    parentDirectory: input.parentDirectory,
    georeference: create(GeoreferenceConfigSchema, {
      horizontalCrs: input.horizontalCrs,
      linearUnit: input.linearUnit,
      axisConvention: AxisConvention.EASTING_NORTHING_UP,
      originEasting: 0,
      originNorthing: 0,
      verticalCrs: '',
    }),
    trafficSide: input.trafficSide,
  })
  return runProjectCommand('creating', client, { case: 'createProject', value: command }, expectSummary)
}

export async function openProject(client: EngineClient, projectDirectory: string): Promise<ProjectSummary> {
  const command = create(OpenProjectCommandSchema, { projectDirectory })
  return runProjectCommand('opening', client, { case: 'openProject', value: command }, expectSummary)
}

export async function saveProject(client: EngineClient): Promise<ProjectSummary> {
  const command = create(SaveProjectCommandSchema, {})
  return runProjectCommand('saving', client, { case: 'saveProject', value: command }, expectSummary)
}

export async function closeProject(client: EngineClient): Promise<void> {
  const command = create(CloseProjectCommandSchema, {})
  return runProjectCommand('closing', client, { case: 'closeProject', value: command }, (outcome) => {
    if (outcome.case !== 'projectClosed') {
      throw expectFailure(outcome)
    }
    useProjectStore.getState().clearProject()
    return undefined
  })
}

// Synchronizes the projection with engine truth. PROJECT_NOT_OPEN is the
// expected initial state after engine startup and is not surfaced as an error.
export async function refreshProjectSummary(client: EngineClient): Promise<ProjectSummary | null> {
  const command = create(GetProjectSummaryCommandSchema, {})
  return runProjectCommand('opening', client, { case: 'getProjectSummary', value: command }, (outcome) => {
    if (outcome.case === 'projectState') {
      return outcome.value.summary ? expectSummary(outcome) : null
    }
    if (outcome.case === 'error' && outcome.value.code === CommandErrorCode.PROJECT_NOT_OPEN) {
      useProjectStore.getState().setLastError(null)
      return null
    }
    throw expectFailure(outcome)
  })
}
