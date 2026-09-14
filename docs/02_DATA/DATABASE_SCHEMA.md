# Database schema design

SQLite is the canonical structured-data store for a project.

## Schema governance

- A `schema_migrations` table records ordered migration IDs and application timestamps.
- Migrations are forward-only in production project files.
- Opening a newer unsupported schema fails without modifying the project.
- Migrations run inside transactions when SQLite operations permit.

### Migration record

| ID | Name | Status | Content |
|---|---|---|---|
| 1 | `core project foundation` | Active (v1) | `project_state` + `georeference` singleton rows (schema v1). |
| 2 | `georeference origin height` | Active (v2) | `ALTER TABLE georeference ADD COLUMN origin_height REAL NOT NULL DEFAULT 0` — extends the canonical georeference with the vertical origin component; v1 databases read 0. |
| 3 | `terrain datasets` | Active (v3) | Creates `terrain_datasets` — canonical imported DEM records (see below); arrives with the Terrain domain (issue #6). |

## 1. Active schema tables (implemented & verified)

These tables exist in production database files (`project.db`) under schema version 3:

### `schema_migrations`
Tracks ordered schema migration IDs and application timestamps:
- `migration_id` (INTEGER PRIMARY KEY)
- `name` (TEXT NOT NULL)
- `applied_at` (TEXT NOT NULL ISO-8601)

### `project_state`
Singleton row (`id = 1`) tracking project identity, revision counter, and timestamps:
- `id` (INTEGER PRIMARY KEY CHECK (id = 1))
- `project_id` (TEXT NOT NULL) — 128-bit canonical UUID string
- `display_name` (TEXT NOT NULL)
- `traffic_side` (TEXT NOT NULL CHECK (traffic_side IN ('left', 'right')))
- `revision` (INTEGER NOT NULL DEFAULT 1) — monotonically increasing mutation counter
- `saved_revision` (INTEGER NOT NULL DEFAULT 1) — revision covered by the latest save
- `created_at` (TEXT NOT NULL)
- `modified_at` (TEXT NOT NULL)

### `georeference`
Singleton row (`id = 1`) storing the canonical project geospatial reference:
- `id` (INTEGER PRIMARY KEY CHECK (id = 1))
- `horizontal_crs` (TEXT NOT NULL) — authority identifier or WKT2 definition
- `linear_unit` (TEXT NOT NULL) — e.g. `metre`, `US survey foot`
- `axis_convention` (TEXT NOT NULL CHECK (axis_convention = 'EASTING_NORTHING_UP'))
- `origin_easting` (REAL NOT NULL)
- `origin_northing` (REAL NOT NULL)
- `origin_height` (REAL NOT NULL DEFAULT 0) — added in migration 2
- `vertical_crs` (TEXT NULL) — optional vertical datum/CRS identifier

### `terrain_datasets`
Canonical records of imported georeferenced DEMs (docs/05_DOMAINS/TERRAIN.md):
- `id` (TEXT PRIMARY KEY) — canonical UUID text; also the world-index EntityId
- `display_name` (TEXT NOT NULL)
- `storage_path` (TEXT NOT NULL) — project-relative location of the ingested raster copy (`terrain/elevation/<uuid>.tif`)
- `source_format` (TEXT NOT NULL), `source_crs` (TEXT NOT NULL)
- `raster_width`/`raster_height` (INTEGER > 0), `origin_x`/`origin_y` (REAL), `cell_size_x`/`cell_size_y` (REAL > 0)
- `elevation_unit` (TEXT NOT NULL), `elevation_unit_to_metre` (REAL > 0)
- `has_nodata` (0/1), `nodata_value` (REAL)
- `min_z`/`max_z` (REAL) — canonical elevation range (project linear unit)
- `bounds_east`/`bounds_west`/`bounds_north`/`bounds_south` (REAL) — canonical project-global coverage
- `source_sha256` (TEXT NOT NULL), `source_bytes` (INTEGER > 0) — integrity of the stored copy
- `revision` (INTEGER >= 1) — per-dataset content revision (derived-tile provenance)
- `diagnostics` (TEXT NOT NULL, JSON array of `{code,message}`) — import-time detected facts
- `created_at`/`modified_at` (TEXT NOT NULL)

---

## 2. Planned domain tables (WIP / future milestone migrations)

In accordance with `docs/ADR/0010-no-placeholder-production-paths.md`, database tables are **not** created ahead of time as empty placeholder schemas. The following tables represent the planned domain schema designs and will be introduced via forward-only schema migrations as their production domains are built:

### Spatial bounds & layers (Phase 5)
- `layers` — semantic and editor grouping.
- `spatial_bounds` — SQLite R-tree or bounding records for spatial partitioning.

### Roads & lanes (Phases 7–8)
- `roads`
- `road_geometry_segments` (clothoids, arcs, lines)
- `road_elevation_segments`
- `road_superelevation_segments`
- `lane_sections`
- `lanes`
- `lane_width_segments`
- `road_markings`
- `road_connections`

### Junctions (Phase 8)
- `junctions`
- `junction_connections`
- `junction_lane_links`

### Infrastructure (Phase 11)
- `infrastructure_objects` (signals, signs, barriers, gantries)
- `infrastructure_bindings`
- `signal_controllers`
- `signal_controller_phases`

### Terrain & assets (Phases 6 & 10)
- `terrain_datasets` — **implemented** (migration 3, above)
- `terrain_tiles` — not needed as a table: derived tile cache files are
  self-describing and rebuildable (`cache/terrain/…`, ADR-0005); their
  provenance lives in the dataset row's `revision`
- `assets`
- `asset_instances`
- `environment_rules`

### Scenario & simulation (Phase 12)
- `scenarios`
- `scenario_entities`
- `scenario_events`
- `simulation_settings`

### Rail (Phase 13)
- Rail tables are introduced with the rail domain migration rather than pre-creating unused placeholder tables.

## Indexing

- Stable primary keys on entity IDs.
- Foreign-key enforcement enabled.
- Domain query indexes derived from measured query plans.
- SQLite R-tree virtual tables used for persisted spatial indexing where the concrete query workload justifies them.

## Binary policy

Large opaque asset/raster blobs are not stored in SQLite merely for convenience. Small structured binary values may be stored when transactional ownership benefits outweigh file-backed access.

## Revision policy

Canonical mutations increment `project_state.revision`. Domain rows may carry their last-modified revision to support incremental synchronization and dirty-region computation.