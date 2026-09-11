# Product Requirements Document — InfraForge

## 1. Product definition

InfraForge is a desktop infrastructure authoring, geospatial editing, rendering, validation, and simulation application. It targets professional workflows for road, terrain, traffic infrastructure, environment, scenario, and later rail authoring in large georeferenced worlds.

InfraForge is a new product architecture. OpenGeoStudio is a lessons-learned reference only; its source structure is not the implementation template.

## 2. Product goals

InfraForge shall let a user:

1. create or open a georeferenced project;
2. select or import real-world geographic data;
3. acquire/import terrain and imagery;
4. author roads using geometric alignments and elevation profiles;
5. author lane sections, lane properties, markings, and connectivity;
6. create and edit junction topology;
7. place semantic traffic infrastructure such as signals, signs, barriers, gantries, and roadside objects;
8. place environment assets such as buildings, vegetation, lights, and props;
9. validate the authored world and receive actionable diagnostics;
10. create and run traffic scenarios;
11. export standard and runtime-oriented formats;
12. work on large projects without loading the whole world into GPU memory.

## 3. Product principles

### 3.1 Canonical world model

Project/domain data is authoritative. UI widgets, generated meshes, renderer objects, caches, and simulation snapshots are derived data.

### 3.2 Desktop-first professional editor

The viewport is the primary workspace. InfraForge must behave like a desktop authoring tool rather than a collection of web pages.

### 3.3 Incremental systems

A local edit invalidates only dependent domain/cache/render data. A one-road edit must not require a full-world rebuild.

### 3.4 Explicit failures

Production workflows must not fabricate success, fake progress, silently substitute data, or convert hard failures into hidden fallbacks.

### 3.5 Extensibility

Import/export, domain tooling, simulation, and rendering interfaces must permit future plugins without exposing internal mutable state.

## 4. Primary personas

- **Infrastructure designer** — authors roads, lanes, junctions, markings, and road furniture.
- **Simulation engineer** — configures traffic behavior, scenarios, signals, routes, and runtime conditions.
- **Technical artist** — manages visual assets, materials, vegetation, LOD, terrain appearance, and scene quality.
- **GIS engineer** — imports geographic datasets, verifies CRS/origin/elevation, and checks spatial alignment.
- **Integrator/developer** — consumes exports, automation interfaces, and plugin APIs.

## 5. Core user workflow

1. Create Project.
2. Set CRS, units, traffic side, and project storage location.
3. Define/import area of interest.
4. Import/download terrain, imagery, and available map data.
5. Inspect generated/imported world data.
6. Author/edit roads.
7. Author lanes and junctions.
8. Add markings, signs, signals, barriers, and gantries.
9. Populate environment assets.
10. Create traffic/scenario content.
11. Run **Check World** validation.
12. Run simulation.
13. Export required deliverables.

## 6. Functional domains

The product is divided into Project, Geo, Terrain, Road, Lane, Junction, Infrastructure, Environment, Asset, Traffic, Scenario, Simulation, Validation, Import, Export, Renderer, and Plugin domains. Rail is a first-class future domain sharing linear-infrastructure services but not road-specific state.

## 7. UX requirements

- Persistent central viewport.
- Dockable/resizable supporting panels.
- Mode rail for World, Terrain, Road, Rail, Infrastructure, Assets, Scenario, Simulation.
- Context-sensitive toolbar and inspector.
- Virtualized searchable outliner.
- Bottom panel for Problems, Operations, Console, and Simulation.
- Global command palette.
- Keyboard shortcuts for common authoring actions.
- No critical operation represented only by transient toast notification.

## 8. Reliability requirements

- Project writes are transactional.
- Autosave is asynchronous and recoverable.
- Schema migrations are versioned and deterministic.
- Cache deletion must not destroy canonical project content.
- Long operations support cancellation where safe.
- Engine crash/disconnect is surfaced explicitly to the desktop UI.
- Unsaved changes are tracked by canonical project revision, not component-local flags.

## 9. Performance engineering budgets

These are design budgets, not claims of current implementation:

- Typical editor viewport target: 60 FPS on a representative RTX 3060-class workstation for a normally composed active working set.
- Fixed simulation target: 60 Hz when configured load permits.
- Simple local command round-trip target: under 50 ms typical.
- UI feedback target: under 100 ms perceived latency for direct interactions.
- Project spatial extent: 100 km+ supported by partitioning/streaming rather than full residency.
- Selection response: next rendered frame for resident selectable geometry.
- Autosave/export/import: asynchronous with visible operation state.

## 10. Interoperability direction

Initial product design includes OpenStreetMap-derived import, OpenDRIVE interoperability, GeoTIFF/DEM data, GeoJSON where applicable, glTF export, and later OpenSCENARIO integration. Each format adapter maps to/from canonical InfraForge domain objects; no importer-specific parallel world model is allowed.

## 11. Non-goals for the foundation phase

The foundation phase does not claim production-ready road authoring, terrain acquisition, simulation, rail, or exporters. Its purpose is to establish architecture, process separation, contracts, project lifecycle, viewport integration, and quality gates so later features do not require structural rewrites.

## 12. Success criteria

InfraForge succeeds when a feature can be implemented vertically without bypassing architecture boundaries, large projects remain incremental, project files are deterministic/recoverable, and the UI remains a coherent professional editor as domains grow.