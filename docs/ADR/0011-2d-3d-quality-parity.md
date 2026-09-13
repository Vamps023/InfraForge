# ADR-0011 2D and 3D quality parity

- Status: Accepted
- Date: 2026-09-13

## Decision

InfraForge treats 2D map editing and 3D scene editing as two views of the same canonical world. Neither view is a reduced demonstration mode or a separate domain model. Every supported authoring operation must have equivalent semantic behavior in both views, with differences limited to camera, projection, and interaction affordances.

## Requirements

- Both views consume the same engine-owned road, lane, junction, terrain, infrastructure, and georeference state.
- A mutation accepted from either view produces the same command, revision, undo record, validation result, affected bounds, and invalidation classes.
- Selection resolves to the same stable entity and sub-entity IDs in both views.
- 2D and 3D display the same topology, lane links, junction boundaries, warnings, locks, and operation states. A diagnostic hidden in one view is still available in the other.
- Geometry calculations use the same double-precision domain algorithms. 2D projection and 3D rendering may derive view-local coordinates, but neither may reimplement road or CRS math.
- Meshes, textures, labels, and overlays are derived presentation data. They cannot become a second source of truth for either view.
- View-specific degradation is explicit. If a feature cannot be represented at the current camera scale or renderer capability, the UI reports that state and retains access to the canonical edit and diagnostic path.

## Verification

Each road and junction feature requires paired acceptance tests: create, select, move, split, connect, disconnect, lock or override, validate, undo, redo, save, reopen, and recover from a failed rebuild in both 2D and 3D. Golden projects compare entity state, revision, diagnostics, and invalidation records after the same operation issued from either view. Performance budgets are measured separately for 2D and 3D, while correctness thresholds are shared.

## Consequences

The frontend may have separate 2D and 3D camera/interaction controllers, but they call shared typed application commands and projections. A 2D-only shortcut that bypasses the native domain path, or a 3D-only mesh edit that cannot be represented semantically, is rejected during review.
