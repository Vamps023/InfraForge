import { describe, expect, it } from 'vitest'
import type { EngineClient } from '../../lib/engineSession'
import { fitRoadSource } from './roadApi'
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

describe('fitRoadSource', () => {
  it('represents preserve, replace, and clear curvature intent distinctly', async () => {
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
})
