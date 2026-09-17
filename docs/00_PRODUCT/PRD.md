# Product Requirements Document — InfraForge

## 1. Product definition

InfraForge is a desktop infrastructure authoring, geospatial editing, rendering, validation, and simulation application. It targets professional workflows for terrain, roads, lanes, junctions, traffic infrastructure, environment/assets, scenario, simulation, and later rail authoring in large georeferenced worlds.

InfraForge is a new product architecture. OpenGeoStudio is a lessons-learned workflow/interaction reference only; its source structure is not the implementation template and its visual styling is not a clone target.

## 2. Product goals

InfraForge shall let a user:

1. create or open a georeferenced project;
2. select or import real-world geographic data while retaining source provenance;
3. acquire/import terrain and imagery;
4. author roads using geometric alignments and elevation profiles;
5. author lane sections, lane properties, markings, and connectivity;
6. create and edit junction topology;
7. place semantic traffic infrastructure such as signals, signs, barriers, gantries, and roadside objects;
8. place environment assets such as buildings, vegetation, lights, and props;
9. validate the authored world and receive actionable diagnostics;
10. create and run traffic scenarios;
11. export standard and runtime-oriented formats;
12. work on large projects without loading the whole world into GPU memory;
13. move between domain workspaces without losing selection, context, project state, or familiar interaction patterns.

## 3. Product principles

### 3.1 Canonical world model

Project/domain data is authoritative. UI widgets, generated meshes, renderer objects, caches, and simulation snapshots are derived data.

### 3.2 Desktop-first professional editor

The viewport is the primary workspace. InfraForge must behave like one coherent desktop authoring tool rather than a collection of web pages or domain-specific mini-applications.

### 3.3 Workspace consistency

A workspace changes the tools and contextual editors used to manipulate the canonical world; it does not create a second navigation model or duplicate canonical state. Selection, commands, undo/redo, jobs, diagnostics, and project status remain global and consistent.

### 3.4 Simple first, advanced on demand

Common workflows should be visible and understandable without reading documentation. Advanced engineering parameters must remain accessible through predictable Inspector/context-editor sections with explicit units and validation.

### 3.5 Incremental systems

A local edit invalidates only dependent domain/cache/render data. A one-road edit must not require a full-world rebuild.

### 3.6 Explicit failures

Production workflows must not fabricate success, fake progress, silently substitute data, or convert hard failures into hidden fallbacks.

### 3.7 Extensibility

Import/export, domain tooling, simulation, rendering, and UI command interfaces must permit future plugins without exposing internal mutable state.

## 4. Primary personas

- **Infrastructure designer** — authors roads, lanes, junctions, markings, and road furniture.
- **Simulation engineer** — configures traffic behavior, scenarios, signals, routes, and runtime conditions.
- **Technical artist** — manages visual assets, materials, vegetation, LOD, terrain appearance, and scene quality.
- **GIS engineer** — imports geographic datasets, verifies CRS/origin/elevation, and checks spatial alignment.
- **Integrator/developer** — consumes exports, automation interfaces, and plugin APIs.

## 5. Core user workflow

The main workflow must be achievable through visible UI without understanding backend architecture:

1. **Home / Project** — create or open a project.
2. **World** — confirm CRS, units, traffic side, origin, area of interest, and source alignment.
3. **Terrain** — import/download terrain and imagery, inspect coverage, and resolve source problems.
4. **Roads** — create/import roads, edit plan geometry and vertical profile, and inspect source-vs-canonical alignment when applicable.
5. **Lanes & Junctions** — edit sections, widths, markings, connectivity, and junction movements.
6. **Infrastructure** — place signals, signs, barriers, gantries, and controllers.
7. **Environment & Assets** — import/place visual assets and environmental content.
8. **Scenario / Simulation** — configure actors/routes/runtime conditions and run supported simulations.
9. **Validate** — run Check World from a global command surface and focus actionable diagnostics.
10. **Export** — choose scope/format, review validation/preview, and run a tracked export job.

Import, Validate, Save, Undo/Redo, Command Palette, and Export are global application capabilities and must not be hidden inside a single workspace.

## 6. Functional domains

The product is divided into Project, Geo, Terrain, Road, Lane, Junction, Infrastructure, Environment, Asset, Traffic, Scenario, Simulation, Validation, Import, Export, Renderer, and Plugin domains. Rail is a first-class future domain sharing linear-infrastructure services but not road-specific state.

## 7. UX requirements

### 7.1 Persistent editor shell

The default desktop shell contains:

- compact application/menu bar with project identity and global commands;
- workspace switcher;
- context toolbar/tool shelf for the active workspace/tool;
- left Navigator with **Scene / Layers / Assets / Sources** tabs as capabilities become real;
- persistent central 2D/3D viewport host;
- right Inspector driven by canonical selection/tool context;
- resizable context editor/drawer for profile, cross-section, topology, timeline, phases, tables, or other task-specific 2D editing;
- bottom utility tabs for **Problems / Operations / Console / Performance/Simulation** when available;
- status bar with coordinates, CRS, active tool hints, engine/renderer state, save/revision state, and relevant viewport status.

### 7.2 Workspace model

Target workspaces:

- Home / Project
- World
- Terrain
- Roads
- Lanes & Junctions
- Infrastructure
- Environment & Assets
- Rail
- Scenario
- Simulation

A workspace may be absent from release UI until its real production path exists. Normal builds should prefer hiding unavailable workspaces over showing large groups of disabled "coming later" entries.

### 7.3 Common interaction grammar

Every authoring workspace shall use the same baseline behavior:

- Select is always understandable and recoverable.
- Hover and selected states are visually distinct.
- `Esc` cancels the in-progress operation first; another `Esc` may clear selection.
- Shift modifies multi-selection where supported.
- Direct manipulation previews the result before commit when practical.
- Inspector sections use stable ordering and explicit units.
- Tool changes do not discard canonical selection unless the selected type is incompatible.
- Undo/redo uses canonical commands/transactions, never UI-only snapshots.
- Long operations appear in Operations with real lifecycle state.
- Validation findings remain persistent in Problems until resolved/acknowledged.

### 7.4 Precision and context editors

Direct 2D/3D editing and exact numeric editing are equal-quality workflows. Road profile, cross-section, rail cant/profile, signal phases, scenario timeline, and similar engineering editors appear as docked contextual surfaces sharing canonical IDs/revisions with the viewport.

### 7.5 Outliner/Navigator

The Scene view supports hierarchy, search, type/domain filters, visibility where meaningful, lock where meaningful, multi-selection, rename where allowed, context actions, and virtualization for large trees. Layers, Assets, and Sources become available only when their real models exist.

### 7.6 Inspector

Inspector content is selection/tool driven. Stable section ordering should be used where applicable:

`Identity -> Geometry -> Semantics -> Appearance -> Connections -> Source/Provenance -> Export -> Diagnostics`

Feature code sends commands; it must not mutate cached canonical objects locally and pretend acceptance.

### 7.7 2D/3D parity

2D map and 3D viewport are equal-quality views of the same canonical world. Supported editing, selection, snapping, diagnostics, undo/redo, and revision behavior must remain semantically equivalent across both views; projection and camera differences are presentation concerns only.

### 7.8 Discoverability and density

- Primary actions use text or unmistakable icon+tooltip patterns.
- Advanced/rare settings use progressive disclosure, not permanent toolbar clutter.
- Critical destructive actions are never ambiguous icon-only affordances.
- Tooltips and status hints explain active gestures and shortcuts.
- Empty states explain the next real action; they do not display fake sample content.
- Compact desktop sizing is preferred over oversized cards.

### 7.9 Accessibility

Core controls must be keyboard reachable, focus visible, screen-reader labelled where appropriate, and not encode critical state using color alone. Numeric fields support keyboard increments and unit suffixes.

## 8. Reliability requirements

- Project writes are transactional.
- Autosave is asynchronous and recoverable.
- Schema migrations are versioned and deterministic.
- Cache deletion must not destroy canonical project content.
- Long operations support cancellation where safe.
- Engine crash/disconnect is surfaced explicitly to the desktop UI.
- Unsaved changes are tracked by canonical project revision, not component-local flags.
- Workspace/layout preferences are user preferences unless an explicitly designed project-scoped layout exists.

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

Piecewise-linear source linework such as OSM roads and railways is source evidence, not automatically final engineering geometry. InfraForge retains the original source coordinates/IDs/tags for provenance, transforms them through the shared Geo service, protects topology-defining anchors, and derives smooth canonical alignments using domain-appropriate mathematical primitives. A visual smoothing pass may be offered for preview, but it is derived data and cannot replace the canonical line/arc/clothoid representation required by road/rail engineering workflows. Source geometry must remain inspectable so users can compare imported data with the reconstructed canonical alignment.

Detailed requirements are defined in `docs/05_DOMAINS/LINEAR_INFRASTRUCTURE_GEOMETRY.md`.

## 11. Non-goals for the foundation phase

The foundation phase does not claim production-ready road authoring, simulation, rail, or exporters merely because their workspace shell can be represented. UI surfaces must not be counted as feature completion unless their real canonical production path is implemented.

## 12. Success criteria

InfraForge succeeds when:

- a feature can be implemented vertically without bypassing architecture boundaries;
- large projects remain incremental and recoverable;
- project files are deterministic and canonical;
- the user can move across domain workspaces without relearning the application;
- common tasks are obvious while advanced engineering controls remain available;
- UI state never becomes a second source of domain truth;
- a new user can complete a representative terrain -> road -> lane/junction -> validate -> save workflow using visible UI guidance.
