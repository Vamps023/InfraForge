# InfraForge UI/UX Roadmap

This document outlines the phased UI/UX modernization plan for InfraForge. Each phase builds on the previous one without destabilizing existing functionality.

## Phase 1: Core Editor Shell Modernization ✅

**Status:** Delivered as the initial shell. The reference-informed refinements
below remain planned until they have production evidence.

**Scope:**
- Semantic design token system (`src/styles/tokens.css`)
- Modular CSS architecture (replaced monolithic `styles.css`)
- Reusable UI primitives (`src/ui/`: Button, IconButton, Tooltip, PanelHeader, StatusDot)
- Modern application header (compact, inline menu, workspace label, search trigger)
- Vertical workspace rail (Home, Terrain functional; Roads/Rail/Environment/Traffic/Simulation disabled)
- Context toolbar (Terrain workspace: Import, Download Area, Georeference)
- Simplified status bar (status dots, concise indicators, no permanent camera instructions)
- Viewport HUD overlay (renderer state, GPU, Vulkan version)
- Project home screen (no-project start experience with New/Open actions and getting-started guide)
- 39 new tests (414 total, all passing)

**Preserved:**
- All existing project workflows (create, open, save, close)
- Terrain import and download
- Georeferencing
- Vulkan viewport integration
- Command registry and palette
- Panel resizing and persistence
- All 375 pre-existing tests

## UI Direction: Reference-Informed Professional Shell

**Goal:** Evolve the whole application toward a cohesive, dense professional
editor comparable in workflow shape to OpenGeoStudio while preserving
InfraForge's independent implementation and architecture.

**Scope:**
- Keep the viewport-first shell constant across all workspaces.
- Use one workspace rail, command registry, selection presentation, outliner,
  inspector, bottom dock, and status surface.
- Introduce Scene/Layers/Assets dock tabs only when backed by real engine
  projections.
- Standardize contextual toolbars, selection affordances, and progressive
  inspector sections across domains.
- Add layout-reset, DPI, keyboard-focus, and multi-monitor acceptance coverage.

**Non-goals:** Copying OpenGeoStudio source, visual assets, data model,
frontend-store design, Electron responsibilities, renderer, or transport.

## Phase 2: Terrain Authoring UX

**Goal:** Deepen the terrain workspace into a full authoring experience.

**Scope:**
- Outliner tabs (Scene / Layers / Assets) with terrain dataset hierarchy
- Inspector sections for terrain datasets (source info, coverage, tile status, attribution)
- Terrain visibility toggles in the outliner
- Frame Terrain action (when the viewport supports camera framing)
- Terrain settings panel (resolution, LOD, display options)
- Download Area UX refinements (progress visualization, cancel feedback, error messaging)
- Terrain dataset context menu (rename, delete, regenerate tiles)
- On-screen viewport HUD (renderer/GPU/Vulkan/FPS) — requires native-side HUD rendering or a separate overlay window; CSS overlays are occluded by the native viewport HWND and cannot be used for this

**Dependencies:** Viewport camera framing API (engine-side), native HUD rendering

## Phase 3: Road Workspace

**Goal:** Continue road authoring and prepare lanes/junction authoring.

**Scope:**
- Refine the enabled Roads workspace within the common shell
- Road context toolbar (Create Road, Edit Geometry, Add Lane, Junction)
- Road outliner projection (road/lane/junction hierarchy)
- Road inspector sections (geometry, lane config, road properties)
- Road rendering in the viewport (when the renderer supports it)
- OpenDRIVE import/export integration

**Dependencies:** GitHub issues #7 and #8 (road/lanes/junctions domain)

## Phase 4: Rail Workspace

**Goal:** Enable rail infrastructure authoring.

**Scope:**
- Enable the Rail workspace in the workspace rail
- Rail context toolbar (Create Track, Edit Alignment, Add Switch)
- Rail outliner projection
- Rail inspector sections
- Rail rendering in the viewport

**Dependencies:** GitHub issue #13 (rail domain)

## Phase 5: Environment / Layers / Assets

**Goal:** Environmental authoring and asset management.

**Scope:**
- Enable the Environment workspace
- Layer system (terrain layers, vegetation, buildings, water)
- Asset browser (import, organize, place 3D models)
- Material editor (when supported)
- Environment context toolbar
- Layer outliner tab

**Dependencies:** Layer system domain, asset pipeline

## Phase 6: Traffic Workspace

**Goal:** Traffic infrastructure authoring.

**Scope:**
- Enable the Traffic workspace
- Traffic context toolbar (Add Signal, Add Sign, Define Flow)
- Traffic outliner projection
- Traffic inspector sections
- OSM data import workflow

**Dependencies:** GitHub issues #11 (traffic infrastructure), #12 (traffic simulation)

## Phase 7: Simulation Workspace

**Goal:** Simulation preparation and execution.

**Scope:**
- Enable the Simulation workspace
- Simulation context toolbar (Configure, Run, Pause, Stop)
- Simulation parameters inspector
- Scenario management
- Results visualization
- Performance profiling overlay

**Dependencies:** GitHub issue #12 (traffic simulation), simulation runtime

## Phase 8: Advanced Professional Tooling

**Goal:** Polish for production use.

**Scope:**
- Transform toolbar (Select, Move, Rotate, Scale, Measure, Draw) with real viewport integration
- Advanced viewport overlays (coordinates, FPS, shading modes, grid settings)
- Context menus (right-click) throughout the editor
- Keyboard shortcut customization
- Theme system (dark/light/custom)
- Undo/redo infrastructure
- Multi-monitor workspace layouts
- Plugin/extension architecture
- Performance optimization (virtualization, lazy loading, web workers)

**Dependencies:** Viewport interaction API, undo/redo domain

## Cross-Cutting Concerns

These apply across all phases:

- **Testing:** Every new UI component gets vitest tests. No phase is complete with failing tests.
- **Typecheck:** Frontend and desktop TypeScript must remain clean.
- **Architecture boundaries:** Frontend stays presentation-only. Domain logic stays in the C++ engine.
- **No fake functionality:** Every wired button has a real action. Future features are disabled, not faked.
- **Documentation:** Update `docs/UI_UX_ARCHITECTURE.md` as the architecture evolves.
