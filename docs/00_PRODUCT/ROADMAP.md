# InfraForge roadmap

This roadmap is dependency ordered. Later phases must not bypass incomplete foundation requirements. UI/UX work is cross-cutting: every domain phase must integrate into the shared workspace model rather than inventing a domain-specific application shell.

## Phase 0 — Product and architecture lock

- PRD/TRD and domain boundaries
- process model
- WebSocket contract model
- project/data design
- native viewport integration strategy
- UI/UX design system and shared workspace interaction grammar
- coding/test/Definition-of-Done rules
- ADR set

## Phase 1 — Executable foundation

- C++ engine executable
- loopback-only authenticated WebSocket server
- Electron process launcher/preload bridge
- React shell connection state
- generated protocol types
- structured logging
- deterministic error envelope
- CI build/type/format checks

## Phase 2 — Editor shell

- menu and command registry
- shared workspace switcher
- docking layout
- Navigator/outliner shell
- inspector shell
- context editor/drawer host
- operations/problems/status panels
- keyboard shortcut system
- layout persistence separate from canonical project state

## Phase 2B — Workspace UX unification

Before domain-specific UI expands independently, align the editor around one OpenGeoStudio-lessons-learned interaction model:

- persistent shell across all workspaces;
- Scene / Layers / Assets / Sources Navigator model as capabilities become real;
- stable Inspector section ordering;
- workspace-specific tool shelf/context toolbar;
- contextual bottom editor for profile/cross-section/topology/timeline/phase/table workflows;
- global Save / Undo / Redo / Import / Validate / Export / Command Palette actions;
- consistent Select -> Create/Edit -> Inspector -> Context Editor -> Validate interaction grammar;
- progressive disclosure for advanced engineering properties;
- hide unavailable workspaces in normal release builds rather than filling the rail with placeholders;
- shared selection/tool/cancel/snapping/status-hint conventions;
- accessibility and keyboard behavior shared by every workspace.

This phase changes presentation and interaction architecture only. Canonical domain ownership remains in the engine.

## Phase 3 — Project lifecycle

- create/open/save/save-as/close
- SQLite project database
- project metadata and schema versioning
- autosave/recovery
- migration framework
- revision tracking
- recent projects
- Home/Project workspace and clear project state/saving feedback

## Phase 4 — Native Vulkan viewport

- Vulkan device/swapchain lifecycle
- embedded native surface
- camera and grid
- picking/selection
- resize/DPI/multi-monitor lifecycle
- render graph/resource ownership
- renderer diagnostics
- shared viewport selection/hover/tool interaction contract

## Phase 5 — Geospatial foundation / World workspace

- canonical CRS/origin
- coordinate transforms
- floating-origin renderer mapping
- spatial index
- world partition/chunks
- map view and area-of-interest model
- Sources projection for imported GIS/provenance
- World Inspector for CRS/origin/traffic-side/project geospatial settings

## Phase 6 — Terrain workspace

- raster/DEM import
- terrain tile model
- LOD and streaming
- imagery/material binding
- terrain sampling API
- terrain validation
- Terrain workspace tools and dataset hierarchy
- coverage/source/provenance Inspector sections
- Download Area workflow integrated with Operations/Problems

## Phase 7 — Roads workspace

- reference line geometry
- line/arc/spiral primitives
- reusable deterministic source-polyline -> alignment fitting kernel
- protected-anchor/topology constraints for later imports
- elevation/superelevation profiles
- control-point editing
- road mesh derivation
- incremental dirty-region rebuilds
- Road Plan tool workflow
- contextual vertical Profile editor
- source-vs-canonical inspection when imported provenance exists

## Phase 8 — Lanes & Junctions workspace

- lane sections and width profiles
- lane semantics/direction
- markings
- road connectivity
- junction/lane-link graph
- intersection rendering/validation
- lane/cross-section contextual editor
- junction movement/topology editor

## Phase 9 — Import adapters

- OSM-derived roads with original source geometry/provenance retained
- topology analysis before smoothing/fitting; never smooth through real junctions
- metric-space line/arc/clothoid reconstruction with configured source-deviation limits
- source-vs-canonical alignment import review/diagnostics
- OpenDRIVE
- GeoJSON/GIS adapters where domain-valid
- import reports and conflict handling
- imported sources visible through the shared Sources surface

## Phase 10 — Environment & Assets workspace

- asset catalog
- buildings/vegetation/props
- materials/LOD
- procedural placement with persisted parameters
- Asset Browser with search/filter/preview
- project-safe source/relink state
- placement tools using shared selection/snapping/Inspector conventions

## Phase 11 — Infrastructure workspace

- signals/signs/gantries/barriers
- controllers/rules
- lane-aware placement
- infrastructure validation
- shared semantic/appearance/connections Inspector sections

## Phase 12 — Scenario / Simulation workspaces

- fixed-step runtime
- routing
- vehicle lifecycle
- car-following/lane changing
- signal/rule integration
- scenario controls and metrics
- contextual timeline/configuration surfaces
- run/pause/step/reset integrated with global job/diagnostic state

## Phase 13 — Rail workspace

- rail alignment/track using shared domain-neutral line/arc/clothoid services
- OSM/GIS rail source-line import with retained provenance and topology-safe curve fitting
- switches/turnouts as protected topology anchors; never smooth through branch points
- platforms/wayside features
- signalling primitives
- train/scenario integration
- rail plan/profile/cant/topology tools using the same interaction grammar as roads without treating rail as road data

## Phase 14 — Export/runtime/plugin surface

- OpenDRIVE export
- glTF/runtime packages
- OpenSCENARIO direction
- staged export UX with validation/preview/report
- plugin SDK
- automation API stabilization

## Cross-cutting Definition of Done

A domain UI is not complete until:

1. its commands operate on canonical engine state;
2. selection identity survives renderer rebuilds;
3. it integrates into the shared Navigator/Inspector/Operations/Problems surfaces;
4. direct manipulation and numeric editing agree;
5. errors/diagnostics are actionable and persistent;
6. supported actions are keyboard accessible;
7. unavailable/future actions are not presented as working;
8. the workspace follows the shared interaction grammar rather than creating a new navigation model.
