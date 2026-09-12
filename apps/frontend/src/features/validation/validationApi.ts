import { create } from '@bufbuild/protobuf'
import {
  CommandErrorCode,
  CommandEnvelopeSchema,
  WorldCheckCommandSchema,
  WorldCancelCheckCommandSchema,
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
//
// When the result is cancelled, partial diagnostics are never published by
// the engine — the result contains cancelled=true with empty diagnostics.
// The previous published set remains valid until a revision change
// invalidates it.
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
    useValidationStore.getState().setDiagnostics(
      diagnostics,
      Number(result.revision),
      result.cancelled,
      result.stale,
    )
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

// Sends the typed `world.cancel_check` command to cancel an in-progress
// validation run. The engine processes this on the network thread (not
// the executor) so the cancellation signal reaches the active validator
// immediately. Returns true if a run was active and cancellation was
// requested.
export async function cancelWorldCheck(client: EngineClient): Promise<boolean> {
  const command = create(WorldCancelCheckCommandSchema, {})
  const envelope = create(CommandEnvelopeSchema, { command: { case: 'worldCancelCheck', value: command } })
  const outcome: NonNullable<ResultEnvelope['outcome']> = await client.sendCommand(envelope.command)

  if (outcome.case !== 'worldCancelCheck') {
    if (outcome.case === 'error') {
      throw new ValidationCommandFailure(outcome.value.code, outcome.value.message || 'world.cancel_check failed')
    }
    throw new ValidationCommandFailure(
      CommandErrorCode.INTERNAL,
      'The native engine returned an unexpected result for world.cancel_check.',
    )
  }

  return outcome.value.cancellationRequested
}
