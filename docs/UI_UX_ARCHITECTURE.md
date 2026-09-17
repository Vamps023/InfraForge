# InfraForge UI/UX Architecture

**Status:** Target workspace architecture v2. Existing Phase-1 shell code remains the baseline until this target is implemented. When this document conflicts with older workspace-placeholder behavior, this document defines the intended direction.

## 1. Architecture goal

InfraForge uses one persistent desktop editor shell. Domain workspaces configure tools, selection filters, projections, Inspector sections, and optional context editors; they do not become separate pages or alternate state owners.

OpenGeoStudio is a lessons-learned workflow reference only. InfraForge keeps its own native-first architecture, command model, canonical engine state, and visual identity.

## 2. Principles

1. **One shell, many workspaces.** Workspace switches are context changes, not application navigation.
2. **Simple first.** Common actions are prominent; advanced engineering properties use progressive disclosure.
3. **Viewport-first.** The native 2D/3D view remains central while precise profile/cross-section/topology/timeline editors dock contextually.
4. **Canonical IDs everywhere.** Selection, diagnostics, Navigator rows, Inspector sections, and context editors refer to canonical entity IDs/sub-elements.
5. **Command-driven.** Menus, toolbars, shortcuts, palette, context menus, and workspace tools dispatch through the same command registry.
6. **Frontend owns presentation only.** Domain/business logic remains in the engine.
7. **Honest capability exposure.** Release builds normally hide workspaces/commands without real production paths.
8. **Global state stays global.** Project lifecycle, Save, Undo/Redo, Import, Validate, Export, Problems, Operations, engine/renderer state, and project revision do not move into domain-specific implementations.

## 3. Target composition

```text
AppShell
├── AppHeader
│   ├── Brand / Project identity
│   ├── AppMenu
│   ├── Global commands (Undo/Redo, Import, Validate, Export)
│   ├── WorkspaceSwitcher
│   └── Command/Search trigger
├── EditorMain
│   ├── ContextToolShelf
│   └── EditorLayout
│       ├── NavigatorDock
│       │   └── Scene | Layers | Assets | Sources
│       ├── ViewportArea
│       │   └── native viewport host
│       ├── InspectorDock
│       └── ContextEditorDock (optional/collapsible)
├── UtilityDock
│   └── Problems | Operations | Console | Performance/Simulation
└── StatusBar
```

The exact visual arrangement may evolve, but these responsibilities remain stable.

## 4. Workspace registry

The current `workspaceStore` concept evolves into a real workspace registry rather than a list of navigation buttons.

Target `WorkspaceDefinition` responsibilities:

- `id`, `label`, icon;
- availability/feature gate;
- tool groups/command IDs;
- default Navigator tab/filter;
- allowed/selectable canonical entity kinds and supported sub-selection;
- Inspector section registrations or section filters;
- optional context-editor registration;
- optional viewport interaction adapter;
- status/help hints;
- layout recommendation that never overrides explicit user customization.

Workspace definitions contain presentation configuration only. They must not embed engine mutations or canonical data stores.

## 5. Navigator architecture

Replace domain-specific left-panel inventions with one Navigator host containing capability tabs:

- `Scene` — canonical semantic hierarchy.
- `Layers` — real layer model only.
- `Assets` — project asset catalog only.
- `Sources` — provenance/import source projections.

Each tab consumes registered projection providers keyed by stable canonical IDs. Large collections use virtualization. Search/filter state is transient UI state.

## 6. Inspector architecture

The existing Inspector section registry remains the extension point. Sections are contributed by feature/domain adapters and sorted into stable groups:

`Identity -> Geometry -> Semantics -> Appearance -> Connections -> Source/Provenance -> Export -> Diagnostics`

A section renders backend-projected values and invokes commands. It must not mutate a private canonical object copy.

## 7. Context editor architecture

Add a `ContextEditorHost`/registry parallel to the Inspector registry. It hosts focused editors that share canonical selection/revision:

- road profile;
- lane cross-section;
- junction topology;
- rail profile/cant/topology;
- controller phases;
- scenario timeline;
- simulation metrics/results.

A context editor contribution defines applicability, title, preferred minimum size, and component. It may maintain transient interaction state but not canonical domain truth.

## 8. Tool architecture

Workspace tool shelves reference central command registrations plus optional viewport interaction modes.

A viewport tool adapter may define:

- selectable entity/sub-element kinds;
- pointer cursor/hover policy;
- preview state projection;
- click/drag gesture translation into typed commands;
- snapping visualization/control;
- cancellation behavior;
- status hint text.

The command registry remains the mutation entry point. A tool adapter must not create a second mutation path.

## 9. Selection architecture

The shared selection store remains canonical-ID based. Extend it only as needed to represent explicit sub-selection (for example road control ID, lane section ID, junction movement ID) without storing mutable domain objects.

Selection should survive renderer mesh rebuilds and compatible workspace switches. Workspace adapters may restrict editability without rewriting entity identity.

## 10. Global command architecture

The following command families remain global:

- project lifecycle;
- undo/redo;
- import entry points;
- world validation;
- export;
- command palette;
- preferences/help.

Workspace toolbars filter or group relevant commands but do not reimplement execution/availability.

## 11. Panel/layout state

`layoutStore` continues to persist user layout preferences separately from canonical project data. Target persisted preferences include panel sizes/visibility, active Navigator tab, context editor size/collapse state, and optional named layout profiles.

Workspace defaults are recommendations only. Explicit user customization wins.

## 12. Native viewport constraints

InfraForge uses a native Vulkan child surface. CSS z-index cannot reliably cover it. Therefore:

1. The viewport-host element stays mounted for the native process lifecycle.
2. `ResizeObserver`/desktop plumbing continues to report bounds.
3. Blocking React overlays that overlap the viewport use the established viewport visibility policy.
4. Frequent HUD/status information should prefer native rendering or shell regions outside the child surface rather than invisible CSS overlays.
5. ContextEditorDock should resize the viewport rather than float over the native surface by default.
6. Renderer status in the shell remains truthful when the surface is hidden/suspended.

## 13. Design-system architecture

Semantic tokens remain the only shared styling contract. The design system expands around workspace/editor primitives such as WorkspaceSwitcher, ToolGroup, VirtualTree, ContextEditorHost, property controls with units/validation, and standardized EmptyState/Diagnostic/Job rows.

Feature CSS should compose shared primitives and tokens rather than fork core controls.

## 14. Availability policy

A production workspace is visible only when its core workflow is real enough to provide useful behavior. Future-only domain entries should normally be hidden in release builds. Feature flags/developer builds may expose disabled or partial surfaces with explicit development labeling.

This replaces the earlier assumption that the workspace rail should permanently display every future domain as a disabled placeholder.

## 15. Migration direction from current shell

Implementation should be incremental:

1. keep existing command registry, selection, layout, Inspector, Problems, Operations, and native viewport plumbing;
2. refactor `WorkspaceRail` into a registry-driven WorkspaceSwitcher;
3. add Navigator tab host and source/asset/layer provider registries without fabricating content;
4. add ContextEditorHost;
5. move Terrain and Roads onto the shared workspace contract first;
6. standardize selection/cancel/status-hint/tool behavior;
7. remove old one-off toolbar/layout branches after migrated workspaces prove parity;
8. add later workspaces only through the same contract.

## 16. Testing requirements

- workspace registry availability/feature-gate tests;
- workspace switching preserves project/global state;
- in-progress tool cancellation is explicit;
- compatible canonical selection survives switching;
- global commands remain available/consistent;
- Navigator/Inspector/context-editor registrations resolve deterministically;
- layout persistence does not enter project truth;
- hidden unavailable workspaces cannot be activated by stale state;
- keyboard/focus behavior works across workspace/tool/panel changes;
- viewport visibility/resize behavior remains correct when context editor/panels open or close.

## 17. Definition of Done for a new workspace

A workspace is not complete until it uses the shared command, selection, Navigator, Inspector, Problems, Operations, layout, and status infrastructure; supports direct and numeric editing where required; has explicit cancellation/validation behavior; and introduces no duplicate canonical frontend state.
