# InfraForge UI/UX Roadmap

This roadmap replaces the earlier workspace-by-workspace UI expansion plan with a shared-workspace-first strategy. Domain UI must grow through one interaction architecture rather than accumulating separate toolbars, panels, and navigation conventions.

## Phase A — Shared workspace foundation

**Goal:** Make the current editor shell the stable frame for every domain.

- registry-driven WorkspaceSwitcher;
- global command placement for Project, Undo/Redo, Import, Validate, Export, Search;
- Navigator host with Scene / Layers / Assets / Sources capability tabs;
- stable Inspector group ordering;
- ContextEditorHost for profile/cross-section/topology/timeline-style editors;
- shared active-tool/status-hint/cancel conventions;
- hide unavailable workspaces in normal release builds;
- preserve current Problems, Operations, layout persistence, command registry, selection, and native viewport plumbing.

**Acceptance:** Switching workspaces never resets project state, duplicates selection/commands, or behaves like route navigation.

## Phase B — Terrain migration

**Goal:** Prove the model with an already-real domain.

- Terrain tool group: Import, Download Area, dataset selection/inspection;
- terrain hierarchy in Scene;
- source/provenance in Sources;
- Inspector for coverage, CRS, resolution, attribution, NoData, build/stream state;
- Operations/Problems remain the sole long-job/error surfaces;
- simplify dialogs so they act as focused tools, not separate applications.

**Acceptance:** Local File and Download Area workflows complete end-to-end inside the shared workspace grammar.

## Phase C — Roads authoring UX

**Goal:** Make road authoring fast for direct editing and precise for engineering work.

- Select / Create Road / Edit Plan / Edit Profile tool groups;
- consistent control-point hover/selection/preview/cancel behavior;
- Road Inspector with Geometry, Semantics, Source/Provenance, Diagnostics;
- docked vertical Profile context editor;
- source-vs-canonical comparison controls for imported roads;
- precise numeric editing synchronized with direct manipulation;
- supported snapping/status hints.

**Acceptance:** A road can be created, selected, edited in plan/profile, validated, undone/redone, saved and reopened without leaving the shared editor model.

## Phase D — Lanes & Junctions UX

**Goal:** Keep lane/cross-section/topology work clear instead of overloading the Road Plan tool.

- lane section/cross-section/marking tool groups;
- junction movement/topology tool;
- cross-section context editor;
- junction movement/topology context editor;
- stable lane/junction selection/sub-selection;
- diagnostics linked to canonical lane/junction entities.

## Phase E — World / Sources UX

**Goal:** Give GIS/georeference/provenance one predictable home.

- World workspace for CRS/origin/AOI/traffic-side/source alignment;
- Sources Navigator provider;
- source status, attribution, CRS, bounds/coverage, relink/refresh/error projection;
- clear source vs canonical derived-entity relationships;
- 2D map/3D parity for supported inspection/editing.

## Phase F — Infrastructure and Assets

**Goal:** Add semantic infrastructure and visual content without breaking interaction consistency.

- Infrastructure tool groups for real supported semantic objects;
- road/lane-aware placement;
- Asset Browser provider with search/filter/preview;
- Environment & Assets placement tools;
- shared Inspector patterns for Semantics, Appearance, Connections, Source;
- controller phase context editor when the controller model exists.

## Phase G — Rail

**Goal:** Reuse interaction grammar, not road-specific domain assumptions.

- rail plan/profile/cant/topology tool groups;
- rail-specific Scene/Inspector language;
- switch/turnout topology editing;
- rail profile/cant/topology context editors;
- source-vs-canonical rail inspection where applicable.

## Phase H — Scenario and Simulation

**Goal:** Preserve the authoring shell while adding timeline/runtime workflows.

- Scenario tools and timeline/event context editor;
- Simulation configure/run/pause/step/reset controls;
- clear authored-state vs runtime-state distinction;
- results/metrics context views;
- simulation Problems/Operations integration.

## Phase I — Professional polish

- robust context menus;
- shortcut discovery and later customization;
- named/user layout profiles;
- theme support after semantic token coverage is complete;
- improved keyboard traversal and accessibility audits;
- selection breadcrumbs where useful;
- native/renderer HUD where justified;
- high-density performance tuning/virtualization;
- plugin UI contribution contracts built on the same workspace/panel registries.

## Cross-cutting requirements

Every phase must preserve:

- canonical engine ownership;
- central command registry;
- stable canonical selection IDs;
- user layout state separate from project truth;
- truthful engine/renderer/job/diagnostic state;
- native viewport lifecycle/occlusion rules;
- no fake functionality or sample domain content;
- tests for workspace switching, keyboard/focus behavior, availability gating, and persistence of user layout preferences.

## UX release gate

Before calling the redesign complete, a representative tester must be able to perform:

`Create/Open Project -> World settings -> Terrain -> Roads -> Lanes/Junctions (when implemented) -> Validate -> Save/Reopen`

with the same selection, tool, Inspector, context-editor, cancellation, diagnostics, and command conventions throughout.
