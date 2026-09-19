import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { render, screen } from '@testing-library/react'
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

// These tests exercise the REAL production ViewportArea component
// (extracted from App.tsx into features/viewport/ViewportArea.tsx) and the
// REAL production visibility helpers (computeShowHomeScreen,
// computeBlockedByOverlay in features/viewport/viewportVisibility.ts).
// No test-only copies of rendering precedence or visibility logic exist
// here — if the production code regresses, these tests fail.

import { ViewportArea } from './features/viewport/ViewportArea'
import {
  computeShowHomeScreen,
  computeBlockedByOverlay,
} from './features/viewport/viewportVisibility'

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

// A stable hostRef for ViewportArea. The native viewport host div must
// remain mounted for every state; we verify that by checking the div is
// always present.
function makeHostRef(): React.RefObject<HTMLDivElement | null> {
  return { current: null }
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

// Helper to detect the home screen in the rendered output. The production
// ProjectHomeScreen renders a region with aria-label="Start screen".
function expectHomeVisible(): void {
  expect(screen.getByRole('region', { name: 'Start screen' })).toBeInTheDocument()
}

function expectHomeAbsent(): void {
  expect(screen.queryByRole('region', { name: 'Start screen' })).not.toBeInTheDocument()
}

// Helper to detect the renderer status overlay. The production ViewportArea
// renders an h1 with "Starting native viewport…" or "Native viewport
// unavailable".
function expectRendererOverlayVisible(): void {
  expect(screen.getByText(/Starting native viewport|Native viewport unavailable/)).toBeInTheDocument()
}

function expectRendererOverlayAbsent(): void {
  expect(screen.queryByText(/Starting native viewport|Native viewport unavailable/)).not.toBeInTheDocument()
}

// Helper to detect the viewport-host div.
function expectViewportHostMounted(): void {
  expect(document.querySelector('.viewport-host')).not.toBeNull()
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
    it(`no project + renderer ${state} → Home visible`, () => {
      setRendererState(state, `detail for ${state}`)
      const showHomeScreen = computeShowHomeScreen(false, 'terrain')
      render(
        <ViewportArea
          hostRef={makeHostRef()}
          showHomeScreen={showHomeScreen}
          commandContext={ctx(noProjectReadyContext())}
        />,
      )
      expectHomeVisible()
      expectRendererOverlayAbsent()
    })
  }

  it('project open + Home workspace + renderer suspended → Home visible', () => {
    useProjectStore.getState().setSummary(makeSummary())
    useWorkspaceStore.getState().setWorkspace('home')
    setRendererState('suspended', 'surface hidden by shell')
    const showHomeScreen = computeShowHomeScreen(true, 'home')
    render(
      <ViewportArea
        hostRef={makeHostRef()}
        showHomeScreen={showHomeScreen}
        commandContext={ctx(readyContext())}
      />,
    )
    expectHomeVisible()
    expectRendererOverlayAbsent()
  })

  it('project open + Home workspace + renderer ready → Home visible', () => {
    useProjectStore.getState().setSummary(makeSummary())
    useWorkspaceStore.getState().setWorkspace('home')
    setRendererState('ready', 'Renderer ready')
    const showHomeScreen = computeShowHomeScreen(true, 'home')
    render(
      <ViewportArea
        hostRef={makeHostRef()}
        showHomeScreen={showHomeScreen}
        commandContext={ctx(readyContext())}
      />,
    )
    expectHomeVisible()
    expectRendererOverlayAbsent()
  })

  it('Terrain workspace + renderer ready → Home absent, no overlay', () => {
    useProjectStore.getState().setSummary(makeSummary())
    useWorkspaceStore.getState().setWorkspace('terrain')
    setRendererState('ready', 'Renderer ready')
    const showHomeScreen = computeShowHomeScreen(true, 'terrain')
    render(
      <ViewportArea
        hostRef={makeHostRef()}
        showHomeScreen={showHomeScreen}
        commandContext={ctx(readyContext())}
      />,
    )
    expectHomeAbsent()
    expectRendererOverlayAbsent()
    expectViewportHostMounted()
  })

  it('Terrain workspace + renderer starting → renderer-status overlay visible, Home absent', () => {
    useProjectStore.getState().setSummary(makeSummary())
    useWorkspaceStore.getState().setWorkspace('terrain')
    setRendererState('starting', 'Starting…')
    const showHomeScreen = computeShowHomeScreen(true, 'terrain')
    render(
      <ViewportArea
        hostRef={makeHostRef()}
        showHomeScreen={showHomeScreen}
        commandContext={ctx(readyContext())}
      />,
    )
    expectHomeAbsent()
    expectRendererOverlayVisible()
  })

  it('Terrain workspace + renderer failed → renderer-status overlay visible, Home absent', () => {
    useProjectStore.getState().setSummary(makeSummary())
    useWorkspaceStore.getState().setWorkspace('terrain')
    setRendererState('failed', 'GPU lost')
    const showHomeScreen = computeShowHomeScreen(true, 'terrain')
    render(
      <ViewportArea
        hostRef={makeHostRef()}
        showHomeScreen={showHomeScreen}
        commandContext={ctx(readyContext())}
      />,
    )
    expectHomeAbsent()
    expectRendererOverlayVisible()
  })
})

describe('Viewport host always mounted', () => {
  it('viewport-host is mounted when Home is shown', () => {
    setRendererState('suspended', 'surface hidden by shell')
    const showHomeScreen = computeShowHomeScreen(false, 'terrain')
    render(
      <ViewportArea
        hostRef={makeHostRef()}
        showHomeScreen={showHomeScreen}
        commandContext={ctx(noProjectReadyContext())}
      />,
    )
    expectViewportHostMounted()
  })

  it('viewport-host is mounted when renderer overlay is shown', () => {
    useProjectStore.getState().setSummary(makeSummary())
    useWorkspaceStore.getState().setWorkspace('terrain')
    setRendererState('starting', 'Starting…')
    const showHomeScreen = computeShowHomeScreen(true, 'terrain')
    render(
      <ViewportArea
        hostRef={makeHostRef()}
        showHomeScreen={showHomeScreen}
        commandContext={ctx(readyContext())}
      />,
    )
    expectViewportHostMounted()
  })

  it('viewport-host is mounted when native viewport path is active', () => {
    useProjectStore.getState().setSummary(makeSummary())
    useWorkspaceStore.getState().setWorkspace('terrain')
    setRendererState('ready', 'Renderer ready')
    const showHomeScreen = computeShowHomeScreen(true, 'terrain')
    render(
      <ViewportArea
        hostRef={makeHostRef()}
        showHomeScreen={showHomeScreen}
        commandContext={ctx(readyContext())}
      />,
    )
    expectViewportHostMounted()
  })
})

describe('Home screen does not disappear when renderer suspends', () => {
  // This is the core regression test for the circular dependency bug.
  // Before the fix, ViewportArea gated Home on surfaceActive. When the
  // shell hid the native viewport (because Home was shown), the renderer
  // reported 'suspended', which is not surfaceActive, so Home disappeared.
  it('Home stays visible when renderer transitions ready → suspended', () => {
    setRendererState('ready', 'Renderer ready')
    const hostRef = makeHostRef()
    const { rerender } = render(
      <ViewportArea
        hostRef={hostRef}
        showHomeScreen={computeShowHomeScreen(false, 'terrain')}
        commandContext={ctx(noProjectReadyContext())}
      />,
    )
    expectHomeVisible()

    // Shell hides native viewport → renderer suspends
    setRendererState('suspended', 'surface hidden by shell')
    rerender(
      <ViewportArea
        hostRef={hostRef}
        showHomeScreen={computeShowHomeScreen(false, 'terrain')}
        commandContext={ctx(noProjectReadyContext())}
      />,
    )
    expectHomeVisible()
  })

  it('Home stays visible when renderer transitions suspended → ready → suspended', () => {
    setRendererState('suspended', 'surface hidden by shell')
    const hostRef = makeHostRef()
    const { rerender } = render(
      <ViewportArea
        hostRef={hostRef}
        showHomeScreen={computeShowHomeScreen(false, 'terrain')}
        commandContext={ctx(noProjectReadyContext())}
      />,
    )
    expectHomeVisible()

    setRendererState('ready', 'Renderer ready')
    rerender(
      <ViewportArea
        hostRef={hostRef}
        showHomeScreen={computeShowHomeScreen(false, 'terrain')}
        commandContext={ctx(noProjectReadyContext())}
      />,
    )
    expectHomeVisible()

    setRendererState('suspended', 'surface hidden by shell')
    rerender(
      <ViewportArea
        hostRef={hostRef}
        showHomeScreen={computeShowHomeScreen(false, 'terrain')}
        commandContext={ctx(noProjectReadyContext())}
      />,
    )
    expectHomeVisible()
  })
})

describe('computeShowHomeScreen (production helper)', () => {
  it('returns true when no project is open', () => {
    expect(computeShowHomeScreen(false, 'terrain')).toBe(true)
    expect(computeShowHomeScreen(false, 'home')).toBe(true)
  })

  it('returns true when project is open and workspace is home', () => {
    expect(computeShowHomeScreen(true, 'home')).toBe(true)
  })

  it('returns false when project is open and workspace is terrain', () => {
    expect(computeShowHomeScreen(true, 'terrain')).toBe(false)
  })
})

describe('computeBlockedByOverlay (production helper)', () => {
  it('returns true when a dialog is open', () => {
    expect(computeBlockedByOverlay('new-project', false)).toBe(true)
    expect(computeBlockedByOverlay('import-terrain', false)).toBe(true)
  })

  it('returns true when home screen is shown (no dialog)', () => {
    expect(computeBlockedByOverlay(null, true)).toBe(true)
  })

  it('returns false when no dialog and home screen is not shown', () => {
    expect(computeBlockedByOverlay(null, false)).toBe(false)
  })

  it('returns true when both dialog and home screen are active', () => {
    expect(computeBlockedByOverlay('georeference', true)).toBe(true)
  })

  it('returns true when an application menu dropdown is open', () => {
    expect(computeBlockedByOverlay(null, false, true)).toBe(true)
  })

  it('Home + menu: closing menu while on Home screen never unblocks viewport', () => {
    // Menu opens while on Home screen
    expect(computeBlockedByOverlay(null, true, true)).toBe(true)
    // Menu closes while on Home screen -> must remain blocked!
    expect(computeBlockedByOverlay(null, true, false)).toBe(true)
  })

  it('Dialog + menu: closing menu while a dialog is open never unblocks viewport', () => {
    // Menu opens while New Project dialog is open
    expect(computeBlockedByOverlay('new-project', false, true)).toBe(true)
    // Menu closes while New Project dialog is open -> must remain blocked!
    expect(computeBlockedByOverlay('new-project', false, false)).toBe(true)
  })

  it('Project active: menu open blocks viewport and menu close restores it', () => {
    // Normal editor state: project open, no dialog, on terrain workspace
    expect(computeBlockedByOverlay(null, false, false)).toBe(false)
    // Open menu
    expect(computeBlockedByOverlay(null, false, true)).toBe(true)
    // Close menu
    expect(computeBlockedByOverlay(null, false, false)).toBe(false)
  })
})

describe('Workspace transition visibility (production helpers)', () => {
  it('switching Home → Terrain changes blockedByOverlay true → false', () => {
    useProjectStore.getState().setSummary(makeSummary())

    // Start on Home
    useWorkspaceStore.getState().setWorkspace('home')
    const homeShow = computeShowHomeScreen(true, 'home')
    expect(computeBlockedByOverlay(null, homeShow)).toBe(true)

    // Switch to Terrain
    useWorkspaceStore.getState().setWorkspace('terrain')
    const terrainShow = computeShowHomeScreen(true, 'terrain')
    expect(computeBlockedByOverlay(null, terrainShow)).toBe(false)
  })

  it('switching Terrain → Home changes blockedByOverlay false → true', () => {
    useProjectStore.getState().setSummary(makeSummary())

    // Start on Terrain
    useWorkspaceStore.getState().setWorkspace('terrain')
    const terrainShow = computeShowHomeScreen(true, 'terrain')
    expect(computeBlockedByOverlay(null, terrainShow)).toBe(false)

    // Switch to Home
    useWorkspaceStore.getState().setWorkspace('home')
    const homeShow = computeShowHomeScreen(true, 'home')
    expect(computeBlockedByOverlay(null, homeShow)).toBe(true)
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
    expect(computeBlockedByOverlay('new-project', showHomeScreen)).toBe(true)
  })
})
