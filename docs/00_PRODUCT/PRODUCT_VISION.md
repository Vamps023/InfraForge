# InfraForge product vision

InfraForge is intended to become a unified infrastructure authoring environment: geospatial source data enters one canonical project model; users refine it with professional terrain, road, lane, junction, infrastructure, environment, rail, scenario, and simulation tools; validation identifies model defects; simulation consumes the same semantic world; and rendering visualizes that world without becoming its source of truth.

The product is deliberately not centered on one renderer, one importer, or one simulation implementation. Those are adapters around the canonical world model.

OpenGeoStudio is a lessons-learned interaction and workflow reference, not a source-code or visual-cloning template. InfraForge should preserve the parts that made OpenGeoStudio approachable—one persistent editor, obvious tools, direct manipulation, precise numeric editing, stable panels, and context-specific editors—while using InfraForge's stricter engine, protocol, persistence, renderer, and validation architecture.

## Long-term capability layers

1. **World acquisition** — maps, GIS, imagery, terrain, existing engineering formats.
2. **Authoring** — roads, lanes, profiles, junctions, infrastructure, environment, rail.
3. **Validation** — topology, geometry, semantic rules, missing assets, spatial consistency.
4. **Simulation** — traffic and later rail/scenario execution.
5. **Visualization** — native Vulkan viewport with large-world streaming.
6. **Interoperability** — standards-based import/export and runtime packages.
7. **Automation** — stable commands/events suitable for plugins and external tooling.

## Product experience principles

### One editor, many workspaces

World, Terrain, Roads, Lanes & Junctions, Infrastructure, Environment & Assets, Rail, Scenario, and Simulation are workspaces inside one persistent desktop editor. Switching workspace changes tools, panel content, selection filters, and context editors; it does not navigate to a disconnected page or replace the application shell.

### Simple first, precision always available

The default surface shows the actions required for the current task. Advanced engineering properties remain available in the Inspector or context editor without forcing every user to understand them before beginning. Direct viewport manipulation and exact numeric editing are equal-quality workflows.

### Predictable interaction grammar

Every authoring workspace follows the same mental model:

`Select -> Create/Edit -> Inspector -> Context Editor -> Validate -> Undo/Redo`

Users should not have to relearn selection, cancellation, snapping, confirmation, diagnostics, or numeric editing when moving between terrain, roads, rail, infrastructure, and assets.

### Viewport-first, not viewport-only

The 2D/3D viewport is the main canvas, but engineering tasks may require profile, cross-section, topology, timeline, phase, or table editors. These appear as contextual docked drawers/panels while preserving the same canonical selection and project revision.

### Honest capability exposure

Release builds show workspaces and commands only when a real production path exists. Unavailable future domains should normally be hidden rather than filling the UI with disabled placeholders. Developer builds may expose feature-gated entries when useful for implementation.

## Design character

InfraForge should feel precise, calm, compact, and immediately understandable. It should use professional engineering/DCC conventions—stable panel locations, restrained decoration, strong selection/validation feedback, compact controls, clear units, and maximum viewport area—without becoming visually dense for its own sake. Information appears progressively: common actions first, advanced settings on demand.

The target is not "more UI." The target is less friction around a powerful canonical engineering model.
