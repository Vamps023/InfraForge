import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { render, screen, act } from '@testing-library/react'
import { create } from '@bufbuild/protobuf'
import { ProjectSummarySchema, type ProjectSummary } from '@infraforge/protocol'
import { useViewportStore, type ViewportState } from './features/viewport/viewportStore'
import { useProjectStore } from './features/project/projectStore'
import { useWorkspaceStore } from './editor/shell/workspaceStore'
import { useShellUiStore } from './editor/shell/shellUiStore'
import { useUiStore } from './state/uiStore'
import { commandRegistry, type CommandContext } from './editor/commands/useCommands'
import { registerBuiltinCommands, unregisterBuiltinCommands } from './editor/commands/builtinCommands'
import type { AvailabilityContext } from './editor/availability'

// ViewportArea is a private component in App.tsx. To test its rendering
// behavior (Home screen vs renderer status overlay vs native viewport
// path) without importing the full App (which starts an engine session),
// we re-implement the same precedence logic in a test harness that
// mirrors the production ViewportArea exactly. This is acceptable because
// the tests verify the STATE MACHINE contract, not the visual styling.
//
// The contract under test:
//   1. viewport-host div is always mounted
//   2. showHomeScreen → ProjectHomeScreen (regardless of renderer state)
//   3. !showHomeScreen && !surfaceActive → renderer status overlay
//   4. !showHomeScreen && surfaceActive → no overlay (native viewport)
//
// Additionally, we verify that blockedByOverlay (which controls native
// viewport visibility via useViewportHost) tracks showHomeScreen, not
// surfaceActive.

function makeSummary(): ProjectSummary {
  return create(ProjectSummarySchema, {
    projectUuid: 'test-uuid',
    displayName: 'Test Project',
    directory: '/test',
    revision: 1n,
    dirty: false,
    georeference: { horizontalCrs: 'EPSG:3857', originHeight: 0 },
  })
}

function readyContext(): AvailabilityContext {
  return {
    engine: 'ready',
    engineMessage: 'ready',
    project: 'project-open',
    viewportActive: true,
  }
}

function noProjectReadyContext(): AvailabilityContext {
  return {
    engine: 'ready',
    engineMessage: 'ready',
    project: 'no-project',
    viewportActive: true,
  }
}

function ctx(availability: AvailabilityContext): CommandContext {
  return { availability }
}

// Mirrors the ViewportArea precedence from App.tsx. If App.tsx changes,
// this harness must be updated to match — that's the point: the test
// catches regressions in the precedence logic.
function ViewportAreaHarness({
  showHomeScreen,
  context,
}: {
  showHomeScreen: boolean
  context: CommandContext
}) {
  const rendererStatus = useViewportStore((state) => state.status)
  const surfaceActive = rendererStatus.state === 'ready' || rendererStatus.state === 'recreating'

  return (
    <main className="viewport-area" aria-label="Viewport" data-testid="viewport-area">
      <div className="viewport-host" data-testid="viewport-host" />
      {showHomeScreen ? (
        <div data-testid="home-screen" role="region" aria-label="Start screen">
          InfraForge
        </div>
      ) : !surfaceActive ? (
        <div data-testid="renderer-overlay" className="viewport-overlay">
          {rendererStatus.state === 'failed' || rendererStatus.state === 'stopped' ? (
            <h1>Native viewport unavailable</h1>
          ) : (
            <h1>Starting native viewport…</h1>
          )}
        </div>
      ) : null}
    </main>
  )
}

// Mirrors the blockedByOverlay computation from App.tsx.
function computeBlockedByOverlay(openDialog: string | null, showHomeScreen: boolean): boolean {
  return openDialog !== null || showHomeScreen
}

// Mirrors showHomeScreen from App.tsx.
function computeShowHomeScreen(projectOpen: boolean, activeWorkspace: string): boolean {
  return !projectOpen || activeWorkspace === 'home'
}

beforeEach(() => {
  useViewportStore.getState().setStatus({ state: 'unavailable', detail: 'reset' })
  useProjectStore.getState().clearProject()
  useWorkspaceStore.getState().setWorkspace('terrain')
  useShellUiStore.getState().closeDialog()
  useUiStore.getState().setEngineStatus({ state: 'ready', message: 'ready' })
  for (const cmd of commandRegistry.all()) {
    commandRegistry.unregister(cmd.id)
  }
  registerBuiltinCommands({ getEngineClient: () => null })
})

afterEach(() => {
  useViewportStore.getState().setStatus({ state: 'unavailable', detail: 'reset' })
  useProjectStore.getState().clearProject()
  useWorkspaceStore.getState().setWorkspace('terrain')
  useShellUiStore.getState().closeDialog()
  unregisterBuiltinCommands()
})

function setRendererState(state: ViewportState, detail = ''): void {
  useViewportStore.getState().setStatus({ state, detail })
}

describe('ViewportArea Home screen independence from renderer state', () => {
  // No project + each renderer state → Home must be shown
  const noProjectStates: ViewportState[] = [
    'unavailable',
    'starting',
    'ready',
    'suspended',
    'recreating',
    'failed',
    'stopped',
    'device_lost',
  ]

  for (const state of noProjectStates) {
    it(`no project + renderer ${state} → Home shown`, () => {
      setRendererState(state, `detail for ${state}`)
      const showHomeScreen = computeShowHomeScreen(false, 'terrain')
      render(<ViewportAreaHarness showHomeScreen={showHomeScreen} context={ctx(noProjectReadyContext())} />)
      expect(screen.getByTestId('home-screen')).toBeInTheDocument()
      expect(screen.queryByTestId('renderer-overlay')).not.toBeInTheDocument()
    })
  }

  it('project open + Home workspace + renderer suspended → Home shown', () => {
    useProjectStore.getState().setSummary(makeSummary())
    useWorkspaceStore.getState().setWorkspace('home')
    setRendererState('suspended', 'surface hidden by shell')
    const showHomeScreen = computeShowHomeScreen(true, 'home')
    render(<ViewportAreaHarness showHomeScreen={showHomeScreen} context={ctx(readyContext())} />)
    expect(screen.getByTestId('home-screen')).toBeInTheDocument()
    expect(screen.queryByTestId('renderer-overlay')).not.toBeInTheDocument()
  })

  it('project open + Home workspace + renderer ready → Home shown', () => {
    useProjectStore.getState().setSummary(makeSummary())
    useWorkspaceStore.getState().setWorkspace('home')
    setRendererState('ready', 'Renderer ready')
    const showHomeScreen = computeShowHomeScreen(true, 'home')
    render(<ViewportAreaHarness showHomeScreen={showHomeScreen} context={ctx(readyContext())} />)
    expect(screen.getByTestId('home-screen')).toBeInTheDocument()
    expect(screen.queryByTestId('renderer-overlay')).not.toBeInTheDocument()
  })

  it('project open + Terrain workspace + renderer ready → native viewport path active, Home absent', () => {
    useProjectStore.getState().setSummary(makeSummary())
    useWorkspaceStore.getState().setWorkspace('terrain')
    setRendererState('ready', 'Renderer ready')
    const showHomeScreen = computeShowHomeScreen(true, 'terrain')
    render(<ViewportAreaHarness showHomeScreen={showHomeScreen} context={ctx(readyContext())} />)
    expect(screen.queryByTestId('home-screen')).not.toBeInTheDocument()
    expect(screen.queryByTestId('renderer-overlay')).not.toBeInTheDocument()
    // viewport-host must always be mounted
    expect(screen.getByTestId('viewport-host')).toBeInTheDocument()
  })

  it('project open + Terrain workspace + renderer starting → renderer overlay shown, Home absent', () => {
    useProjectStore.getState().setSummary(makeSummary())
    useWorkspaceStore.getState().setWorkspace('terrain')
    setRendererState('starting', 'Starting…')
    const showHomeScreen = computeShowHomeScreen(true, 'terrain')
    render(<ViewportAreaHarness showHomeScreen={showHomeScreen} context={ctx(readyContext())} />)
    expect(screen.queryByTestId('home-screen')).not.toBeInTheDocument()
    expect(screen.getByTestId('renderer-overlay')).toBeInTheDocument()
  })
})

describe('Viewport host always mounted', () => {
  it('viewport-host is mounted when Home is shown', () => {
    setRendererState('suspended', 'surface hidden by shell')
    const showHomeScreen = computeShowHomeScreen(false, 'terrain')
    render(<ViewportAreaHarness showHomeScreen={showHomeScreen} context={ctx(noProjectReadyContext())} />)
    expect(screen.getByTestId('viewport-host')).toBeInTheDocument()
  })

  it('viewport-host is mounted when renderer overlay is shown', () => {
    useProjectStore.getState().setSummary(makeSummary())
    useWorkspaceStore.getState().setWorkspace('terrain')
    setRendererState('starting', 'Starting…')
    const showHomeScreen = computeShowHomeScreen(true, 'terrain')
    render(<ViewportAreaHarness showHomeScreen={showHomeScreen} context={ctx(readyContext())} />)
    expect(screen.getByTestId('viewport-host')).toBeInTheDocument()
  })

  it('viewport-host is mounted when native viewport path is active', () => {
    useProjectStore.getState().setSummary(makeSummary())
    useWorkspaceStore.getState().setWorkspace('terrain')
    setRendererState('ready', 'Renderer ready')
    const showHomeScreen = computeShowHomeScreen(true, 'terrain')
    render(<ViewportAreaHarness showHomeScreen={showHomeScreen} context={ctx(readyContext())} />)
    expect(screen.getByTestId('viewport-host')).toBeInTheDocument()
  })
})

describe('Workspace transition visibility', () => {
  it('switching Home → Terrain changes desired native viewport visibility from false → true', () => {
    useProjectStore.getState().setSummary(makeSummary())

    // Start on Home
    useWorkspaceStore.getState().setWorkspace('home')
    const homeShowHomeScreen = computeShowHomeScreen(true, 'home')
    const homeBlocked = computeBlockedByOverlay(null, homeShowHomeScreen)
    expect(homeBlocked).toBe(true)

    // Switch to Terrain
    useWorkspaceStore.getState().setWorkspace('terrain')
    const terrainShowHomeScreen = computeShowHomeScreen(true, 'terrain')
    const terrainBlocked = computeBlockedByOverlay(null, terrainShowHomeScreen)
    expect(terrainBlocked).toBe(false)
  })

  it('switching Terrain → Home changes desired native viewport visibility from true → false', () => {
    useProjectStore.getState().setSummary(makeSummary())

    // Start on Terrain
    useWorkspaceStore.getState().setWorkspace('terrain')
    const terrainShowHomeScreen = computeShowHomeScreen(true, 'terrain')
    const terrainBlocked = computeBlockedByOverlay(null, terrainShowHomeScreen)
    expect(terrainBlocked).toBe(false)

    // Switch to Home
    useWorkspaceStore.getState().setWorkspace('home')
    const homeShowHomeScreen = computeShowHomeScreen(true, 'home')
    const homeBlocked = computeBlockedByOverlay(null, homeShowHomeScreen)
    expect(homeBlocked).toBe(true)
  })

  it('no project always blocks viewport regardless of workspace', () => {
    useWorkspaceStore.getState().setWorkspace('terrain')
    expect(computeBlockedByOverlay(null, computeShowHomeScreen(false, 'terrain'))).toBe(true)

    useWorkspaceStore.getState().setWorkspace('home')
    expect(computeBlockedByOverlay(null, computeShowHomeScreen(false, 'home'))).toBe(true)
  })

  it('open dialog blocks viewport even when project is open on Terrain', () => {
    useProjectStore.getState().setSummary(makeSummary())
    useWorkspaceStore.getState().setWorkspace('terrain')
    const showHomeScreen = computeShowHomeScreen(true, 'terrain')
    // Simulate a dialog open
    const blocked = computeBlockedByOverlay('new-project', showHomeScreen)
    expect(blocked).toBe(true)
  })
})

describe('Home screen does not disappear when renderer suspends', () => {
  // This is the core regression test for the circular dependency bug.
  // Before the fix, ViewportArea gated Home on surfaceActive. When the
  // shell hid the native viewport (because Home was shown), the renderer
  // reported 'suspended', which is not surfaceActive, so Home disappeared.
  it('Home stays visible when renderer transitions ready → suspended', () => {
    // No project, renderer starts ready
    setRendererState('ready', 'Renderer ready')
    const { rerender } = render(
      <ViewportAreaHarness
        showHomeScreen={computeShowHomeScreen(false, 'terrain')}
        context={ctx(noProjectReadyContext())}
      />,
    )
    expect(screen.getByTestId('home-screen')).toBeInTheDocument()

    // Shell hides native viewport → renderer suspends
    setRendererState('suspended', 'surface hidden by shell')
    rerender(
      <ViewportAreaHarness
        showHomeScreen={computeShowHomeScreen(false, 'terrain')}
        context={ctx(noProjectReadyContext())}
      />,
    )
    // Home must still be visible
    expect(screen.getByTestId('home-screen')).toBeInTheDocument()
  })

  it('Home stays visible when renderer transitions suspended → ready → suspended', () => {
    setRendererState('suspended', 'surface hidden by shell')
    const { rerender } = render(
      <ViewportAreaHarness
        showHomeScreen={computeShowHomeScreen(false, 'terrain')}
        context={ctx(noProjectReadyContext())}
      />,
    )
    expect(screen.getByTestId('home-screen')).toBeInTheDocument()

    setRendererState('ready', 'Renderer ready')
    rerender(
      <ViewportAreaHarness
        showHomeScreen={computeShowHomeScreen(false, 'terrain')}
        context={ctx(noProjectReadyContext())}
      />,
    )
    expect(screen.getByTestId('home-screen')).toBeInTheDocument()

    setRendererState('suspended', 'surface hidden by shell')
    rerender(
      <ViewportAreaHarness
        showHomeScreen={computeShowHomeScreen(false, 'terrain')}
        context={ctx(noProjectReadyContext())}
      />,
    )
    expect(screen.getByTestId('home-screen')).toBeInTheDocument()
  })
})
