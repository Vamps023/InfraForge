import type { EngineSessionStatus, EngineSession } from '../lib/engineSession'
import { viewportSurfaceActive, type ViewportState } from '../features/viewport/viewportStore'

// Centralized engine/project availability predicates for the editor shell.
// Commands and shell components query these instead of independently
// re-deriving engine/project state scattered across components. The policy
// is owned here so gating rules change in one place.
//
// `deriveAvailability` is pure: it takes all inputs as parameters and never
// reads global Zustand stores via getState(). Callers (hooks) subscribe to
// the stores reactively and pass the values in, so the derived context is
// always fresh.

export type EngineAvailabilityState =
  | 'starting'
  | 'ready'
  | 'disconnected'
  | 'failed'
  | 'unavailable'

export type ProjectAvailabilityState =
  | 'no-project'
  | 'project-open'
  | 'busy'

export interface AvailabilityContext {
  engine: EngineAvailabilityState
  engineMessage: string
  project: ProjectAvailabilityState
  viewportActive: boolean
}

export function engineAvailabilityFromStatus(status: EngineSessionStatus): EngineAvailabilityState {
  return status.state
}

// Pure derivation. All inputs are explicit parameters — no hidden getState()
// reads. Callers subscribe to the relevant stores reactively and pass the
// values here so the context updates immediately on any state change.
export function deriveAvailability(
  engineStatus: EngineSessionStatus,
  engineSession: EngineSession | null,
  projectSummary: unknown | null,
  projectOperation: unknown | null,
  viewportState: ViewportState,
): AvailabilityContext {
  const engine = engineAvailabilityFromStatus(engineStatus)
  // ready requires both the status projection and a live session client.
  const engineReady = engine === 'ready' && engineSession !== null
  const project: ProjectAvailabilityState =
    projectSummary === null
      ? 'no-project'
      : projectOperation !== null
        ? 'busy'
        : 'project-open'
  const viewportActive = viewportSurfaceActive(viewportState)
  return {
    engine: engineReady ? 'ready' : engine,
    engineMessage: engineStatus.message,
    project,
    viewportActive,
  }
}

export interface CommandAvailability {
  // True when the command may execute at all. Commands that require a
  // project must not execute when project === 'no-project'. Commands that
  // require the engine must not execute when engine !== 'ready'.
  enabled: boolean
  // Optional reason for toolbars/menus to surface as a tooltip.
  disabledReason?: string
}

export function evaluateAvailability(
  context: AvailabilityContext,
  requirements: {
    requiresEngine?: boolean
    requiresProject?: boolean
    requiresViewport?: boolean
    requiresNotBusy?: boolean
  },
): CommandAvailability {
  if (requirements.requiresEngine && context.engine !== 'ready') {
    return { enabled: false, disabledReason: 'Native engine is not ready.' }
  }
  if (requirements.requiresProject && context.project === 'no-project') {
    return { enabled: false, disabledReason: 'No project is open.' }
  }
  if (
    (requirements.requiresProject || requirements.requiresNotBusy) &&
    context.project === 'busy'
  ) {
    return { enabled: false, disabledReason: 'A project operation is in progress.' }
  }
  if (requirements.requiresViewport && !context.viewportActive) {
    return { enabled: false, disabledReason: 'Native viewport is not active.' }
  }
  return { enabled: true }
}
