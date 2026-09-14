# InfraForge UI/UX Architecture

This document describes the UI/UX architecture, design system, and interaction conventions for the InfraForge editor. It covers the Phase 1 modernization delivered on the `feature/ui-modernization` branch.

## Design Principles

1. **Professional, not flashy.** InfraForge is a desktop engineering application. The visual language is restrained: layered charcoal/slate surfaces, cool blue accents, subtle transitions. No neon, no excessive gradients, no overdone glassmorphism.

2. **Desktop-first.** The layout targets 1920×1080, 2560×1440, and 3840×2160. Panels use sensible minimum widths and remain usable at 1366×768. No mobile-style large rounded cards.

3. **Honest UI.** No fake functionality. Future-only features are either hidden or shown clearly disabled with a "Coming later" tooltip. Every wired button has a real action behind it.

4. **Viewport dominance.** The 3D viewport is the center of the application. Chrome around it is minimized. Overlays show real data only (renderer state, GPU, Vulkan version).

5. **Command-driven.** The central command registry is the single entry point for user actions. Menus, toolbar, keyboard shortcuts, and command palette all dispatch through `executeCommand()`. No duplicated execution logic.

6. **Architecture boundaries.** The frontend owns presentation and transient UI state only. Domain/business logic stays in the C++ engine. The React layer projects backend state; it never owns canonical data.

## Layout Architecture

```
AppShell (grid: 40px / 1fr / 26px)
├── AppHeader (40px)
│   ├── Brand (logo + "InfraForge")
│   ├── AppMenu (inline File/Edit/View/Help menus)
│   ├── Workspace label
│   ├── Project chip
│   ├── Global search trigger (Ctrl+Shift+P)
│   ├── Renderer status indicator
│   └── Version label
│
├── AppMain (flex row)
│   ├── WorkspaceRail (52px vertical navigation)
│   │   ├── Home
│   │   ├── Terrain (functional)
│   │   ├── Roads (disabled — future)
│   │   ├── Rail (disabled — future)
│   │   ├── Environment (disabled — future)
│   │   ├── Traffic (disabled — future)
│   │   └── Simulation (disabled — future)
│   │
│   └── EditorArea (flex column)
│       ├── ContextToolbar (36px, workspace-specific actions)
│       └── EditorLayout (dockable/resizable)
│           ├── LeftPanel (Outliner)
│           ├── CenterViewport (native Vulkan HWND + overlays)
│           ├── RightPanel (Inspector)
│           └── BottomPanel (Problems / Operations)
│
└── StatusBar (26px)
    ├── Engine status dot + state
    ├── Renderer status dot + state + GPU
    ├── Project revision
    ├── CRS
    └── Version
```

### Key files

| Component | File |
|---|---|
| App shell | `apps/frontend/src/App.tsx` |
| AppHeader | `apps/frontend/src/editor/shell/AppHeader.tsx` |
| WorkspaceRail | `apps/frontend/src/editor/shell/WorkspaceRail.tsx` |
| ContextToolbar | `apps/frontend/src/editor/shell/ContextToolbar.tsx` |
| StatusBar | `apps/frontend/src/editor/shell/StatusBar.tsx` |
| ProjectHomeScreen | `apps/frontend/src/editor/shell/ProjectHomeScreen.tsx` |
| Workspace store | `apps/frontend/src/editor/shell/workspaceStore.ts` |
| Shell UI store | `apps/frontend/src/editor/shell/shellUiStore.ts` |
| EditorLayout | `apps/frontend/src/editor/layout/EditorLayout.tsx` |
| Layout store | `apps/frontend/src/editor/layout/layoutStore.ts` |

## Workspace Concept

Workspaces are the primary navigation metaphor. Each workspace represents a domain area (Terrain, Roads, Rail, etc.). The vertical workspace rail on the far left switches between them.

**v0.1 status:**
- **Home**: functional — shows the project start screen when no project is open, and also when the user explicitly navigates to the Home workspace (even with a project open). Navigating to Home hides the native viewport and shows the home screen overlay; switching back to Terrain restores the native viewport.
- **Terrain**: functional — the only authoring workspace with real actions (Import, Download Area, Georeference)
- **Roads, Rail, Environment, Traffic, Simulation**: disabled — clearly marked as "Coming later"

Future workspaces are rendered as disabled buttons with `aria-disabled="true"` and a tooltip explaining they are future modules. They do not execute any action when clicked.

## Design Tokens

All component styling references semantic CSS custom properties defined in `apps/frontend/src/styles/tokens.css`. No hardcoded colors in component CSS.

### Color tokens

| Token | Purpose |
|---|---|
| `--if-bg-canvas` | Darkest — viewport background |
| `--if-bg-panel` | Panels (outliner, inspector) |
| `--if-bg-elevated` | Raised controls, dropdowns, cards |
| `--if-bg-hover` | Hover state |
| `--if-bg-active` | Active/selected button |
| `--if-bg-input` | Input field backgrounds |
| `--if-border-subtle` | Lightest separators |
| `--if-border-default` | Standard panel borders |
| `--if-border-strong` | Inputs, dropdowns |
| `--if-text-primary` | Main text |
| `--if-text-secondary` | Secondary labels |
| `--if-text-muted` | Muted hints, metadata |
| `--if-text-disabled` | Disabled state |
| `--if-accent` | Cool blue accent |
| `--if-success` | Green — success states |
| `--if-warning` | Amber — warning states |
| `--if-danger` | Red — error states |

### Spacing, radius, shadows, typography, z-index

The token file also defines:
- Spacing scale (`--if-space-0` through `--if-space-24`, 4px base)
- Radius (`--if-radius-sm` through `--if-radius-xl`)
- Shadows (`--if-shadow-sm` through `--if-shadow-lg`)
- Control heights (`--if-control-sm` through `--if-control-xl`)
- Panel header heights (`--if-header-app`, `--if-header-panel`, etc.)
- Font sizes and weights
- Z-index scale (`--if-z-base` through `--if-z-command-palette`)
- Transition timing

## CSS Architecture

The monolithic `styles.css` has been replaced with modular files:

```
src/styles/
├── tokens.css          — semantic design tokens
├── base.css             — reset, global defaults, scrollbar, imports tokens
├── shell.css            — app shell, header, workspace rail, menu, toolbar, status bar, UI primitives
├── panels.css           — outliner, inspector, panel headers, resize handles, bottom panel
├── viewport.css         — viewport area, overlays, empty states, home screen
├── dialogs.css          — dialog overlay, forms, CRS picker, terrain dialog, download map
└── command-palette.css  — command palette
```

All files are imported in `main.tsx`. The `base.css` file imports `tokens.css`.

## Component Architecture

### UI Primitives (`src/ui/`)

Reusable components built on design tokens:

| Component | Purpose |
|---|---|
| `Button` | Standard button with default/primary variants |
| `IconButton` | Icon-only button with tooltip and `aria-label` |
| `Tooltip` | CSS-based hover/focus tooltip (top/bottom/right) |
| `PanelHeader` | Panel title bar with optional actions |
| `StatusDot` | Colored status indicator dot |

### Shell Components (`src/editor/shell/`)

| Component | Purpose |
|---|---|
| `AppHeader` | Compact header with brand, menu, workspace, project, search |
| `WorkspaceRail` | Vertical workspace navigation |
| `ContextToolbar` | Workspace-specific action toolbar |
| `StatusBar` | Simplified status indicators |
| `ProjectHomeScreen` | Start screen when no project is open |
| `AppMenu` | Traditional File/Edit/View menu (inline in header) |
| `Toolbar` | Legacy toolbar (command-registry-driven) |
| `BottomPanel` | Problems/Operations tabs |

## Panel System

Panels are dockable and resizable. The `layoutStore` persists panel visibility and sizes to `localStorage` under `infraforge.editor.layout.v1` (separate from canonical project state, per ADR-0009).

- **Left panel**: Outliner (scene tree)
- **Right panel**: Inspector (properties)
- **Bottom panel**: Problems / Operations tabs
- **Resize handles**: nearly invisible by default, accent-highlighted on hover/drag

Minimum sizes prevent panels from collapsing to unusable widths. The native viewport host's `ResizeObserver` re-reports bounds on every layout change.

## Viewport Overlays

The viewport area hosts the native Vulkan child HWND. CSS overlays cannot occlude the HWND, so any overlay that must be visible to the user triggers the viewport visibility policy (`blockedByOverlay` in `useViewportHost`), which tells the desktop shell to hide the native surface.

### Project home screen (no project open or Home workspace active)

The `ProjectHomeScreen` is rendered as a CSS overlay over the viewport area when no project is open OR when the user navigates to the Home workspace (even with a project open). Because the native viewport HWND would otherwise cover it, `App` reports `blockedByOverlay = true` for both states, so the desktop shell hides the native viewport and the home screen is visible. When a project is opened and the user is on the Terrain workspace, `blockedByOverlay` returns to `false` and the native viewport is shown again.

The blocking signal is `showHomeScreen = !projectOpen || activeWorkspace === 'home'` (not `surfaceActive && !projectOpen`) so that `blockedByOverlay` is `true` from initial mount — before the viewport process starts — rather than transitioning to `true` only when the renderer reports ready. This avoids a race where the desktop shell's post-readiness `applyViewportVisibilityPlan` runs before the renderer's `setViewportVisible(false)` IPC round-trip lands, which would briefly show the native surface over the home screen.

**Home screen rendering is independent of native renderer state.** The `ProjectHomeScreen` is shown whenever `showHomeScreen` is true, regardless of whether the renderer reports `ready`, `suspended`, `starting`, `failed`, or any other state. This is critical because hiding the native viewport causes the renderer to report `suspended` (which is NOT `viewportSurfaceActive`). If Home were gated on `surfaceActive`, hiding the native viewport would make the renderer suspend, which would make Home disappear — a circular dependency. The UI precedence in `ViewportArea` is:

1. `viewport-host` div (always mounted — native viewport process lifecycle)
2. `ProjectHomeScreen` (when `showHomeScreen` — independent of renderer state)
3. Renderer status overlay (when `!showHomeScreen && !surfaceActive` — only for authoring workspaces when the renderer is not yet ready or has failed)

### Renderer not yet active

When an authoring workspace (e.g. Terrain) is active and the renderer surface is not yet active, the viewport area shows a CSS empty-state overlay (starting message or error). This overlay is only shown when `!showHomeScreen` — the Home screen takes precedence when active.

### Viewport HUD (deferred)

An on-screen viewport HUD (renderer state, GPU, Vulkan version, FPS) is not rendered while a project is open. The native viewport HWND covers the full viewport-host area and CSS overlays cannot render above it, so a CSS-based HUD would be invisible to the user. Renderer/GPU/Vulkan status is already surfaced in the `StatusBar`, which is not occluded by the native surface. A visible viewport HUD requires native-side HUD rendering or a separate overlay window and is deferred to a future phase (see `docs/UI_UX_ROADMAP.md`).

## Command Integration

All user actions flow through the central command registry (`commandRegistry.ts`):

1. **Menu**: `AppMenu` renders commands with `surfaces: ['menu']`
2. **Toolbar**: `Toolbar` renders commands with `surfaces: ['toolbar']`
3. **Shortcuts**: `useCommandShortcuts` dispatches commands with `surfaces: ['shortcut']`
4. **Palette**: `CommandPalette` lists commands with `surfaces: ['palette']`
5. **ContextToolbar**: workspace-specific actions routed through `useCommandExecutor` so availability gating (engine, project, busy state) is consistent with all other surfaces

The `ContextToolbar` uses dedicated commands (`terrain.import-local`, `terrain.download-area`, `project.georeference`) that call `openTerrainImport(mode)` on the `shellUiStore` to deep-link the Import Terrain dialog to either "Local File" or "Download Area" mode. The toolbar's disabled state is derived from `resolveCommandAvailability()` — the same path every other surface uses — so it cannot drift apart from the command architecture's safety gating.

## Native Viewport Constraint

InfraForge uses a native Vulkan viewport surface (child HWND on Windows). Key constraints:

1. CSS z-index cannot occlude the native viewport — blocking dialogs and the home screen (no project or Home workspace) trigger `blockedByOverlay` to hide it
2. The viewport host div must always be in the DOM for the native viewport process lifecycle
3. The `useViewportHost` hook reports bounds via `ResizeObserver` and visibility via `document.hidden + blockedByOverlay`
4. The desktop shell's `ViewportVisibilityPolicy` handles the actual show/hide with placement refresh

## Accessibility Conventions

- All interactive elements have `aria-label` or visible text
- Keyboard navigation: menu (ArrowUp/Down/Enter/Escape), tabs (ArrowLeft/Right), outliner (Arrow keys), command palette (Arrow/Enter/Escape)
- Focus-visible outlines use `--if-accent`
- Disabled commands use `aria-disabled` (not native `disabled`) in menus so they remain keyboard-navigable
- Screen reader support via semantic HTML and ARIA roles
- The workspace rail uses `aria-current="page"` for the active workspace
- Tooltips use `aria-describedby` to associate the tooltip text with the trigger element; the tooltip element is always rendered in the DOM (hidden via CSS when not visible) so the reference is always valid

## Interaction Conventions

- **Hover**: subtle background change (`--if-bg-hover`)
- **Active/Selected**: accent-tinted background (`--if-bg-active`) with accent border
- **Focus**: 1px accent outline
- **Disabled**: muted color, no cursor, no hover effect
- **Tooltips**: 400ms delay, appear on hover/focus, dismiss on blur/mouse-leave
- **Transitions**: 100-200ms ease for all state changes

## Future Workspace Expansion

New workspaces are added by:

1. Adding a `WorkspaceDefinition` to `WORKSPACES` in `workspaceStore.ts`
2. Setting `enabled: true` when the workspace has real functionality
3. Adding a `ContextToolbar` branch for workspace-specific actions
4. Registering workspace-specific commands in the command registry

The workspace rail, context toolbar, and command palette automatically pick up new commands and workspaces through their reactive subscriptions.
