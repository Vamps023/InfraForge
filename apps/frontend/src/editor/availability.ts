import type { EngineSessionStatus, EngineSession } from '../lib/engineSession'
import { useProjectStore } from '../features/project/projectStore'
import { useViewportStore, viewportSurfaceActive } from '../features/viewport/viewportStore'

// Centralized engine/project availability predicates for the editor shell.
// Commands and shell components query these instead of independently
// re-deriving engine/project state scattered across components. The policy
// is owned here so gating rules change in one place.

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

export function deriveAvailability(
  engineStatus: EngineSessionStatus,
  engineSession: EngineSession | null,
): AvailabilityContext {
  const engine = engineAvailabilityFromStatus(engineStatus)
  // ready requires both the status projection and a live session client.
  const engineReady = engine === 'ready' && engineSession !== null
  const summary = useProjectStore.getState().summary
  const operation = useProjectStore.getState().operation
  const project: ProjectAvailabilityState =
    summary === null
      ? 'no-project'
      : operation !== null
        ? 'busy'
        : 'project-open'
  const viewportActive = viewportSurfaceActive(useViewportStore.getState().status.state)
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
  requirements: { requiresEngine?: boolean; requiresProject?: boolean; requiresViewport?: boolean },
): CommandAvailability {
  if (requirements.requiresEngine && context.engine !== 'ready') {
    return { enabled: false, disabledReason: 'Native engine is not ready.' }
  }
  if (requirements.requiresProject && context.project === 'no-project') {
    return { enabled: false, disabledReason: 'No project is open.' }
  }
  if (requirements.requiresProject && context.project === 'busy') {
    return { enabled: false, disabledReason: 'A project operation is in progress.' }
  }
  if (requirements.requiresViewport && !context.viewportActive) {
    return { enabled: false, disabledReason: 'Native viewport is not active.' }
  }
  return { enabled: true }
}
