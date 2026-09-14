import { describe, expect, it } from 'vitest'
import {
  deriveAvailability,
  evaluateAvailability,
  type AvailabilityContext,
} from './availability'
import type { EngineSessionStatus, EngineSession } from '../lib/engineSession'
import type { ViewportState } from '../features/viewport/viewportStore'

function viewportReady(): ViewportState {
  return 'ready'
}

function status(state: EngineSessionStatus['state'], message = ''): EngineSessionStatus {
  return { state, message }
}

// A non-null EngineSession placeholder. The actual object shape does not
// matter for deriveAvailability — only its nullness is tested.
function session(): EngineSession {
  return { client: {} as never, dispose: () => {} } as unknown as EngineSession
}

describe('deriveAvailability engine readiness invariant', () => {
  it('ready status + live session => ready', () => {
    const ctx = deriveAvailability(status('ready'), session(), null, null, viewportReady())
    expect(ctx.engine).toBe('ready')
  })

  it('ready status + null session => starting (NOT ready)', () => {
    const ctx = deriveAvailability(status('ready'), null, null, null, viewportReady())
    expect(ctx.engine).not.toBe('ready')
    expect(ctx.engine).toBe('starting')
  })

  it('starting status + null session => starting', () => {
    const ctx = deriveAvailability(status('starting'), null, null, null, viewportReady())
    expect(ctx.engine).toBe('starting')
  })

  it('disconnected status + retained session => disconnected', () => {
    const ctx = deriveAvailability(status('disconnected'), session(), null, null, viewportReady())
    expect(ctx.engine).toBe('disconnected')
  })

  it('failed status + null session => failed', () => {
    const ctx = deriveAvailability(status('failed'), null, null, null, viewportReady())
    expect(ctx.engine).toBe('failed')
  })

  it('unavailable status + null session => unavailable', () => {
    const ctx = deriveAvailability(status('unavailable'), null, null, null, viewportReady())
    expect(ctx.engine).toBe('unavailable')
  })
})

describe('requiresEngine command gating with ready-status/null-session', () => {
  it('requiresEngine command is disabled for ready-status/null-session', () => {
    const ctx: AvailabilityContext = deriveAvailability(
      status('ready'),
      null,
      null,
      null,
      viewportReady(),
    )
    expect(ctx.engine).not.toBe('ready')
    const result = evaluateAvailability(ctx, { requiresEngine: true })
    expect(result.enabled).toBe(false)
  })

  it('requiresEngine command becomes enabled once the live session is available', () => {
    const ctx: AvailabilityContext = deriveAvailability(
      status('ready'),
      session(),
      null,
      null,
      viewportReady(),
    )
    expect(ctx.engine).toBe('ready')
    const result = evaluateAvailability(ctx, { requiresEngine: true })
    expect(result.enabled).toBe(true)
  })
})
