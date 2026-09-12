import { create } from '@bufbuild/protobuf'
import {
  CommandErrorCode,
  CommandEnvelopeSchema,
  WorldCheckCommandSchema,
  type Diagnostic,
  type ResultEnvelope,
} from '@infraforge/protocol'
import { EngineCommandError, type EngineClient } from '../../lib/engineSession'
import { useValidationStore } from './validationStore'

export class ValidationCommandFailure extends Error {
  constructor(
    readonly code: CommandErrorCode,
    message: string,
  ) {
    super(message)
  }
}

// Sends the typed `world.check` command and projects the structured result
// into the validation store. The frontend never computes diagnostics; it
// only displays what the native engine returns.
export async function checkWorld(client: EngineClient): Promise<Diagnostic[]> {
  const store = useValidationStore.getState()
  store.setChecking(true)
  try {
    const command = create(WorldCheckCommandSchema, {})
    const envelope = create(CommandEnvelopeSchema, { command: { case: 'worldCheck', value: command } })
    const outcome: NonNullable<ResultEnvelope['outcome']> = await client.sendCommand(envelope.command)

    if (outcome.case !== 'worldCheck') {
      if (outcome.case === 'error') {
        throw new ValidationCommandFailure(outcome.value.code, outcome.value.message || 'world.check failed')
      }
      throw new ValidationCommandFailure(
        CommandErrorCode.INTERNAL,
        'The native engine returned an unexpected result for world.check.',
      )
    }

    const result = outcome.value
    const diagnostics = result.diagnostics.length > 0 ? result.diagnostics : []
    useValidationStore.getState().setDiagnostics(diagnostics, Number(result.revision), result.cancelled)
    return diagnostics
  } catch (error) {
    useValidationStore.getState().setChecking(false)
    if (error instanceof ValidationCommandFailure) {
      throw error
    }
    throw new ValidationCommandFailure(
      CommandErrorCode.INTERNAL,
      error instanceof EngineCommandError
        ? error.message
        : 'The native engine could not be reached for world.check.',
    )
  }
}
