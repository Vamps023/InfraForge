import { create } from '@bufbuild/protobuf'
import {
  GetGeoreferenceCommandSchema,
  SetGeoreferenceCommandSchema,
  type CommandEnvelope,
  type GeoreferenceConfig,
  type GeoreferenceInfo,
} from '@infraforge/protocol'
import { EngineCommandError, type EngineClient } from '../../lib/engineSession'
import { CommandErrorCode } from '@infraforge/protocol'
import { expectFailure, ProjectCommandFailure, type ResultOutcome } from '../project/projectApi'
import { useGeoStore } from './geoStore'
import { useProjectStore } from '../project/projectStore'

type GeoCommand = NonNullable<CommandEnvelope['command']>

// Loads the resolved canonical georeference into the frontend projection.
// The projection mirrors engine truth; it is never a second source of it.
export async function refreshGeoreference(client: EngineClient): Promise<GeoreferenceInfo> {
  const command = create(GetGeoreferenceCommandSchema, {})
  const outcome = await sendGeoCommand(client, { case: 'getGeoreference', value: command })
  if (outcome.case !== 'georeferenceState' || !outcome.value.georeference) {
    throw expectFailure(outcome)
  }
  const info = outcome.value.georeference
  useGeoStore.getState().setInfo(info, outcome.value.revision)
  return info
}

// Replaces the canonical project georeference through the engine. The
// expected revision guards against losing a concurrent change.
export async function applyGeoreference(
  client: EngineClient,
  config: GeoreferenceConfig,
  expectedRevision?: bigint,
): Promise<GeoreferenceInfo> {
  const command = create(SetGeoreferenceCommandSchema, {
    georeference: config,
    ...(expectedRevision !== undefined ? { expectedRevision } : {}),
  })
  const store = useGeoStore.getState()
  store.setApplying(true)
  store.setLastError(null)
  try {
    const outcome = await sendGeoCommand(client, { case: 'setGeoreference', value: command })
    if (outcome.case !== 'georeferenceState' || !outcome.value.georeference) {
      throw expectFailure(outcome)
    }
    const info = outcome.value.georeference
    useGeoStore.getState().setInfo(info, outcome.value.revision)
    // The summary's canonical georeference is patched by the
    // georeference_changed event; mirror the authoritative result here so
    // the projection is consistent even if events arrive out of order.
    if (info.config) {
      useProjectStore.getState().patchSummary({
        georeference: info.config,
        revision: outcome.value.revision,
        dirty: true,
      })
    }
    return info
  } catch (error) {
    const failure =
      error instanceof ProjectCommandFailure
        ? error
        : new ProjectCommandFailure(
            CommandErrorCode.INTERNAL,
            error instanceof EngineCommandError
              ? error.message
              : 'The native engine could not be reached for this georeference command.',
          )
    useGeoStore.getState().setLastError({ code: String(failure.code), message: failure.message })
    throw failure
  } finally {
    useGeoStore.getState().setApplying(false)
  }
}

async function sendGeoCommand(client: EngineClient, command: GeoCommand): Promise<ResultOutcome> {
  return client.sendCommand(command)
}
