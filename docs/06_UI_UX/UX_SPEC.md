# InfraForge UX specification

## Objective

InfraForge is one viewport-first desktop editor. Workspace changes should swap tools and context without making the user learn a new application. OpenGeoStudio is a workflow reference only; InfraForge keeps its own canonical engine, protocol, persistence, renderer, and visual identity.

Users should always know the active workspace, active tool, current selection, next gesture, exact editable values, save/validation state, and how to cancel or recover.

## UI direction

InfraForge adopts a dense, engineering-editor interaction model informed by the
OpenGeoStudio editor experience: a persistent viewport surrounded by a
workspace rail, contextual tools, a navigable scene hierarchy, an inspector,
and durable diagnostic/operation surfaces. This is a UX reference only. No
OpenGeoStudio source, architecture, state model, renderer, or protocol is an
InfraForge implementation dependency.

The intended result is a coherent professional shell, not a visual clone:

- the native viewport remains the dominant working area;
- every workspace changes tools and projections, not the underlying shell;
- selection is one shared presentation state synchronized among viewport,
  outliner, inspector, and Problems;
- menus, toolbar buttons, context menus, shortcuts, and the command palette
  invoke the same registered command;
- panels are compact, information-dense, resizable, and recoverable at common
  Windows DPI scales;
- unavailable capabilities are visibly unavailable, never simulated.

## Persistent layout

```text
Menu / Project / Undo-Redo / Workspace / Import / Validate / Export / Search
Workspace tool shelf / context toolbar
Navigator [Scene | Layers | Assets | Sources] | Main 2D/3D Viewport | Inspector
Context Editor [Profile | Cross-section | Topology | Timeline | other task view]
Problems | Operations | Console | Performance/Simulation
Status: tool hint | coordinates/station | CRS | engine | renderer | save/revision
```

The viewport stays dominant. Context editors open only when the task benefits from a precise secondary representation. The right dock uses progressive disclosure: essential selection properties first, engineering/detail sections second. The bottom dock is persistent enough that errors and long-running work do not disappear behind transient notifications.


## Workspaces

Target workspaces are Home/Project, World, Terrain, Roads, Lanes & Junctions, Infrastructure, Environment & Assets, Rail, Scenario, and Simulation.

A workspace defines visible tool groups, selection filters, Inspector sections, optional context editor, status hints, and viewport interaction mode. It does not create separate canonical state.

Normal release builds should hide workspaces that do not yet have a real production path. Developer builds may expose feature-gated entries for implementation testing.

## Global commands

New/Open/Save/Save As/Close, Undo/Redo, Import, Check World, Export, Command Palette, Preferences, and Help remain globally discoverable. They use the same command registry and availability rules in every workspace.

## Common interaction grammar

Every authoring workspace follows:

`Select -> Create/Edit -> Inspector -> Context Editor (when needed) -> Validate -> Undo/Redo`

Selection uses stable canonical IDs. Hover differs from selection. Shift modifies multi-selection where supported. Locked objects may be inspected but not edited.

`Esc` cancels an in-progress operation first; when no operation is active, another `Esc` may clear selection. Switching tools must not silently commit a preview.

Direct manipulation should preview constrained results before commit where practical. Exact numeric editing is always available for engineering values with explicit units and validation.

## Navigator

- **Scene** — semantic hierarchy, search/filter, visibility/lock where meaningful, multi-selection, context actions, virtualization.
- **Layers** — real layer visibility/isolate/lock/export inclusion; hidden until the layer model exists.
- **Assets** — searchable project asset catalog with type/category filters, preview, source/relink state, and placement when supported.
- **Sources** — GIS/OSM/terrain/engineering source provenance, CRS, coverage/bounds, attribution, refresh/relink status, warnings/errors, and links to derived canonical entities.

## Inspector

Preferred section order:

`Identity -> Geometry -> Semantics -> Appearance -> Connections -> Source/Provenance -> Export -> Diagnostics`

Common fields are easy to find; advanced groups use progressive disclosure. Edits send typed commands. Invalid values and backend rejection stay visible and attributable to the attempted edit.

## Context editor

The context editor is a docked/resizable surface sharing the same selection and project revision as the viewport. Typical uses include road elevation profile, lane cross-section, junction topology, rail profile/cant, controller phase editing, scenario timeline, and simulation metrics/table views.

It may collapse to maximize viewport space. It must never become a duplicate domain model.

## Workspace guidance

### Home / Project
New/Open, recent projects, recovery state, and concise getting-started guidance. No fake sample world.

### World
CRS/origin, project area, georeferencing, traffic side, map/reference sources, and project-wide spatial settings. Sources are prominent.

### Terrain
Import Terrain, Download Area, dataset selection, coverage inspection, and supported terrain operations. Inspector emphasizes source, CRS, coverage, resolution, NoData, attribution, and streaming/build status.

### Roads
Select, Create Road, Edit Plan, Edit Profile, and supported control/split/join operations. The viewport handles plan/direct geometry; the context editor handles vertical profile. Imported roads can compare source geometry with canonical alignment.

### Lanes & Junctions
Lane sections, cross-section, markings, connectivity, and junction movement/topology editing. The context editor hosts cross-section or topology views.

### Infrastructure
Road/lane-aware placement and editing of real semantic signals, signs, barriers, gantries, controllers, and other supported infrastructure. Inspector shows binding, semantics, appearance, connections, and diagnostics.

### Environment & Assets
Asset import/browse/place/transform and supported environment tools. Assets are prominent in the Navigator.

### Rail
Same overall interaction grammar as Roads, with rail-specific language, visuals, gauge/cant, switches/turnouts, topology, and signalling. Rail must not be presented as road data.

### Scenario
Canonical scenario entities and their routes/events/conditions through a timeline or other suitable context editor when implemented.

### Simulation
Configure, Run, Pause, Step, Reset. Runtime state is clearly separated from authored project state; results appear without replacing the authoring shell.

## Problems

Diagnostics include severity, stable code, domain/source, message, entity link where applicable, project revision, and optional supported actions such as Focus, Select, Fix, or Re-run. Critical failures must not exist only as transient toasts.

## Operations

Long work uses real states: queued, running, measurable progress, cancel requested, completed, completed with warnings, failed, or cancelled. Percentage is shown only when backend progress is meaningful.

## Command model

All significant commands register with one command service containing ID, label, workspace/domain grouping, availability predicate, shortcut, and invoke action. Menus, toolbars, palette, shortcuts, and context menus reference the same command registrations.

## Interaction qualities

- Dense professional controls rather than dashboard cards.
- Compact spacing and restrained radii.
- Strong focus/hover/selection/active-tool states.
- Tooltips for icon-only actions.
- Status hints for the active gesture.
- Clear units and predictable numeric editing.
- Empty states point to the next real action.
- Destructive actions are never ambiguous icon-only controls.
- Advanced options do not permanently occupy prime toolbar space.
- Errors stay associated with the action/property that caused them when possible.
- Viewport navigation, selection, framing, and tool cancellation have visible affordances and documented shortcuts; focus must never be trapped in a dock.

## Accessibility baseline

Core controls and tabs are keyboard accessible, focus is visible, semantic labels are provided where appropriate, and critical state is never color-only. Numeric fields support keyboard increments and unit suffixes.

## Acceptance

A representative new user should be able to create/open a project, confirm world settings, import/download terrain, enter Roads, create/select/edit a road, edit exact plan/profile values, run validation, focus a problem, save, close, and reopen using visible UI guidance and the same interaction grammar throughout.
