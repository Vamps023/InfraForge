# Database schema design

SQLite is the canonical structured-data store for a project.

## Schema governance

- A `schema_migrations` table records ordered migration IDs and application timestamps.
- Migrations are forward-only in production project files.
- Opening a newer unsupported schema fails without modifying the project.
- Migrations run inside transactions when SQLite operations permit.

### Migration record

| ID | Name | Content |
|---|---|---|
| 1 | `core project foundation` | `project_state` + `georeference` singleton rows (schema v1). |
| 2 | `georeference origin height` | `ALTER TABLE georeference ADD COLUMN origin_height REAL NOT NULL DEFAULT 0` — extends the canonical georeference with the vertical origin component; v1 databases read 0. |

## Core tables

The initial schema is organized by domain, not by UI screens.

### Project/core

- `project_state` — singleton project revision and core state.
- `schema_migrations` — applied schema migration IDs.
- `layers` — semantic/editor grouping.
- `entity_metadata` — optional shared name/tags/visibility metadata when cross-domain ownership requires it.

### Geospatial

- `georeference` — canonical project CRS/origin/axis/unit configuration
  (singleton row `id = 1`): `horizontal_crs`, `linear_unit`,
  `axis_convention`, `origin_easting`, `origin_northing`, `origin_height`
  (migration 2), `vertical_crs`.
- `spatial_bounds` — derived/query-oriented bounds keyed by stable entity IDs where persisted indexing is beneficial.

### Roads

- `roads`
- `road_geometry_segments`
- `road_elevation_segments`
- `road_superelevation_segments`
- `lane_sections`
- `lanes`
- `lane_width_segments`
- `road_markings`
- `road_connections`

### Junctions

- `junctions`
- `junction_connections`
- `junction_lane_links`

### Infrastructure

- `infrastructure_objects`
- `infrastructure_bindings`
- `signal_controllers`
- `signal_controller_phases`

### Terrain/environment/assets

- `terrain_datasets`
- `terrain_tiles`
- `assets`
- `asset_instances`
- `environment_rules`

### Scenario/simulation

- `scenarios`
- `scenario_entities`
- `scenario_events`
- `simulation_settings`

Rail tables are introduced with the rail domain migration rather than pre-creating unused placeholder tables.

## Indexing

- Stable primary keys on entity IDs.
- Foreign-key enforcement enabled.
- Domain query indexes derived from measured query plans.
- SQLite R-tree virtual tables used for persisted spatial indexing where the concrete query workload justifies them.

## Binary policy

Large opaque asset/raster blobs are not stored in SQLite merely for convenience. Small structured binary values may be stored when transactional ownership benefits outweigh file-backed access.

## Revision policy

Canonical mutations increment `project_state.revision`. Domain rows may carry their last-modified revision to support incremental synchronization and dirty-region computation.