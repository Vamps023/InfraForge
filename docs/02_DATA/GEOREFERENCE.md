# Canonical georeference

A project has one canonical georeference owned by the Geo domain.

## Required fields

- horizontal CRS identifier/definition;
- linear unit;
- axis convention used by the InfraForge domain world;
- project origin expressed in the horizontal CRS;
- optional vertical CRS/datum metadata when known;
- traffic side is project traffic semantics and is stored separately from coordinate transforms.

## Coordinate spaces

1. **Source coordinate space** — coordinate system used by imported source data.
2. **Project global space** — double-precision canonical domain coordinates after transformation.
3. **Render local space** — camera-/chunk-relative coordinates optimized for GPU precision.

All source transformations go through the shared Geo service. Terrain, roads, OSM, buildings, simulation, and exporters may not maintain independent origin/scale/CRS logic.

## Precision

Canonical horizontal/vertical computations use double precision. Renderer conversion to float occurs only at the render boundary with a documented origin transform.

## Change policy

Changing a project's CRS/origin is a project-level migration operation because it can affect every spatial domain. It is not a simple UI preference.