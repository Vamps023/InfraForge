# InfraForge roadmap

This roadmap is dependency ordered. Later phases must not bypass incomplete foundation requirements.

## Phase 0 — Product and architecture lock

- PRD/TRD and domain boundaries
- process model
- WebSocket contract model
- project/data design
- native viewport integration strategy
- UI/UX design system
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
- mode rail
- docking layout
- outliner shell
- inspector shell
- operations/problems/status panels
- keyboard shortcut system

## Phase 3 — Project lifecycle

- create/open/save/save-as/close
- SQLite project database
- project metadata and schema versioning
- autosave/recovery
- migration framework
- revision tracking
- recent projects

## Phase 4 — Native Vulkan viewport

- Vulkan device/swapchain lifecycle
- embedded native surface
- camera and grid
- picking/selection
- resize/DPI/multi-monitor lifecycle
- render graph/resource ownership
- renderer diagnostics

## Phase 5 — Geospatial foundation

- canonical CRS/origin
- coordinate transforms
- floating-origin renderer mapping
- spatial index
- world partition/chunks
- map view and area-of-interest model

## Phase 6 — Terrain

- raster/DEM import
- terrain tile model
- LOD and streaming
- imagery/material binding
- terrain sampling API
- terrain validation

## Phase 7 — Road authoring

- reference line geometry
- line/arc/spiral primitives
- reusable deterministic source-polyline -> alignment fitting kernel
- protected-anchor/topology constraints for later imports
- elevation/superelevation profiles
- control-point editing
- road mesh derivation
- incremental dirty-region rebuilds

## Phase 8 — Lanes and junctions

- lane sections and width profiles
- lane semantics/direction
- markings
- road connectivity
- junction/lane-link graph
- intersection rendering/validation

## Phase 9 — Import adapters

- OSM-derived roads with original source geometry/provenance retained
- topology analysis before smoothing/fitting; never smooth through real junctions
- metric-space line/arc/clothoid reconstruction with configured source-deviation limits
- source-vs-canonical alignment import review/diagnostics
- OpenDRIVE
- GeoJSON/GIS adapters where domain-valid
- import reports and conflict handling

## Phase 10 — Environment and assets

- asset catalog
- buildings/vegetation/props
- materials/LOD
- procedural placement with persisted parameters

## Phase 11 — Traffic infrastructure

- signals/signs/gantries/barriers
- controllers/rules
- lane-aware placement
- infrastructure validation

## Phase 12 — Traffic simulation

- fixed-step runtime
- routing
- vehicle lifecycle
- car-following/lane changing
- signal/rule integration
- scenario controls and metrics

## Phase 13 — Rail

- rail alignment/track using shared domain-neutral line/arc/clothoid services
- OSM/GIS rail source-line import with retained provenance and topology-safe curve fitting
- switches/turnouts as protected topology anchors; never smooth through branch points
- platforms/wayside features
- signalling primitives
- train/scenario integration

## Phase 14 — Export/runtime/plugin surface

- OpenDRIVE export
- glTF/runtime packages
- OpenSCENARIO direction
- plugin SDK
- automation API stabilization
