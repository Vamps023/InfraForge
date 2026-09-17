# App shell specification

The app shell owns persistent editor composition, navigation, and transient UI state. It never owns canonical domain behavior or project truth.

## UX objective

InfraForge should feel like one desktop engineering tool regardless of active domain. Workspace changes must preserve the application frame and familiar interaction locations while swapping only the tools, projections, and contextual editors relevant to the task.

## Persistent regions

```text
┌──────────────────────────────────────────────────────────────────────────────┐
│ Menu / Project / Undo-Redo / Workspace / Validate / Import / Export / Search│
├──────────────────────────────────────────────────────────────────────────────┤
│ Workspace-specific tool shelf / context toolbar                             │
├───────────────┬───────────────────────────────────────┬──────────────────────┤
│ Navigator     │                                       │ Inspector            │
│ Scene         │           Native 2D/3D Viewport       │ selection/tool       │
│ Layers        │                                       │ properties           │
│ Assets        │                                       │                      │
│ Sources       │                                       │                      │
├───────────────┴───────────────────────────────────────┴──────────────────────┤
│ Context Editor: Profile / Cross-section / Topology / Timeline / Phases / ...│
├──────────────────────────────────────────────────────────────────────────────┤
│ Problems | Operations | Console | Performance / Simulation                   │
├──────────────────────────────────────────────────────────────────────────────┤
│ Status: tool hint | coordinates | CRS | engine | renderer | save/revision   │
└──────────────────────────────────────────────────────────────────────────────┘
```

Not every capability must be visible at once. Regions/tabs that do not yet have a real production model should be hidden in normal release builds.

## Global versus workspace-specific actions

Global application actions stay in stable locations and do not move between workspaces:

- New/Open/Save/Save As/Close
- Undo/Redo
- Command Palette / Search
- Import
- Check World / Validate
- Export
- Preferences / Help

Workspace-specific actions live in the context toolbar/tool shelf. Examples:

- Terrain: Import Terrain, Download Area, Inspect Coverage
- Roads: Select, Create Road, Edit Plan, Edit Profile, Split/Join
- Lanes & Junctions: Lane Sections, Cross-section, Markings, Junction
- Infrastructure: Signal, Sign, Barrier, Gantry, Controller
- Rail: Track Plan, Profile/Cant, Switch/Topology
- Simulation: Configure, Run, Pause, Step, Reset

## Workspace switcher

Target workspaces are Home/Project, World, Terrain, Roads, Lanes & Junctions, Infrastructure, Environment & Assets, Rail, Scenario, and Simulation.

Rules:

1. A workspace is a tool/context configuration, not a separate page application.
2. Switching workspace must not close the project, clear unrelated canonical state, or reset layout preferences.
3. Canonical selection should remain when the selected entity is meaningful in the target workspace; otherwise the UI may present the selection read-only or clear it explicitly.
4. Normal release builds should hide unavailable workspaces. Developer/feature-gated builds may expose disabled entries for implementation testing.
5. Global Problems/Operations/Save/Undo/Redo remain available regardless of workspace.

## Navigator

The left dock uses a consistent tab model as real capability becomes available:

- **Scene** — canonical semantic hierarchy and selection.
- **Layers** — visibility, lock, isolate, grouping, export inclusion where the layer model supports it.
- **Assets** — project asset browser/catalog.
- **Sources** — imported GIS/source provenance, attribution, bounds, refresh/relink/errors.

The Navigator must support search/filter, keyboard navigation, virtualization for large models, and stable canonical IDs.

## Inspector

Inspector content is driven by canonical selection and active tool context. Feature adapters contribute sections; they do not own project truth.

Preferred stable section order:

`Identity -> Geometry -> Semantics -> Appearance -> Connections -> Source/Provenance -> Export -> Diagnostics`

Advanced sections may be collapsed by default. Common editable properties should not be buried behind multiple modal dialogs.

## Context editor

The context editor is a dockable/resizable surface for tasks that are awkward in the main viewport but still edit the same canonical entity/revision. Examples include:

- road elevation profile;
- lane cross-section/width transitions;
- junction movement/topology graph;
- rail elevation/cant;
- signal controller phases;
- scenario timeline;
- simulation metrics/table views.

Opening a context editor must not create a duplicate domain model or independent selection system.

## Bottom utility panel

- **Problems** — persistent structured diagnostics with entity focus/fix actions when supported.
- **Operations** — real jobs with queued/running/progress/cancel/completed/warning/failed lifecycle.
- **Console** — developer/advanced log surface where enabled.
- **Performance / Simulation** — optional advanced runtime metrics; hidden when unsupported.

## Status bar

The status bar communicates compact, high-frequency context such as active tool gesture help, pointer/project coordinates, station/bearing when relevant, CRS, engine/renderer availability, save/revision state, and simulation state. It must not become a dumping ground for verbose diagnostics.

## Layout persistence

Panel size, visibility, docking, active Navigator tab, context-editor size, and similar preferences are user preferences. They are not canonical project state. Project-specific workspace state may be stored separately only when explicitly designed and versioned.

A future layout profile system may provide sensible defaults per workspace, but user customization must be respected rather than reset on every workspace switch.

## Engine state

The shell visibly distinguishes `starting`, `ready`, `disconnected`, `failed`, and `shutting_down`. Domain controls requiring the engine are disabled when the engine session is unavailable, with a discoverable reason.

## Empty states

Before a project is open, Home/Project shows real project actions and recent projects. Workspace empty states explain the next supported action using real commands; they must not inject sample terrain, roads, assets, or fake jobs.

## Native viewport

The viewport host must correctly handle resize, focus, pointer capture, DPI scaling, maximize/restore, multi-monitor movement, tab/dock visibility, destruction, renderer failure, and blocking React overlays.

Because the native Vulkan child surface cannot be covered by ordinary CSS z-index, blocking dialogs/context surfaces that overlap its bounds must use the established viewport visibility policy or native-side overlay strategy. The viewport host remains mounted for process lifecycle stability.

## Interaction invariants

- `Esc` cancels an in-progress tool operation before clearing selection.
- Hover and selection have distinct feedback.
- Tool changes are explicit and visible.
- Destructive actions have text or unambiguous confirmation.
- Direct manipulation previews before commit where practical.
- Numeric edits show units and validation inline.
- Every workspace uses the central command registry and common availability predicates.
