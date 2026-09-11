# InfraForge product vision

InfraForge is intended to become a unified infrastructure authoring environment: geospatial source data enters one canonical project model; users refine it with professional road/terrain/infrastructure tools; validation identifies model defects; simulation consumes the same semantic world; and rendering visualizes that world without becoming its source of truth.

The product is deliberately not centered on one renderer, one importer, or one simulation implementation. Those are adapters around the canonical world model.

## Long-term capability layers

1. **World acquisition** — maps, GIS, imagery, terrain, existing engineering formats.
2. **Authoring** — roads, lanes, profiles, junctions, infrastructure, environment, rail.
3. **Validation** — topology, geometry, semantic rules, missing assets, spatial consistency.
4. **Simulation** — traffic and later rail/scenario execution.
5. **Visualization** — native Vulkan viewport with large-world streaming.
6. **Interoperability** — standards-based import/export and runtime packages.
7. **Automation** — stable commands/events suitable for plugins and external tooling.

## Design character

The application should feel precise, dense, predictable, and tool-oriented. The visual language should borrow from professional DCC/engineering software rather than consumer dashboards: compact controls, stable panel locations, clear hierarchy, restrained decoration, strong selection/validation feedback, and a viewport-first layout.