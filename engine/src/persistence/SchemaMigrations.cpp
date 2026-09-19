#include "infraforge/persistence/SchemaMigrations.hpp"

#include "infraforge/persistence/SqliteConnection.hpp"
#include "infraforge/runtime/Logging.hpp"
#include "infraforge/runtime/Timestamp.hpp"

#include <array>
#include <sqlite3.h>
#include <string>

namespace infraforge::persistence {
namespace {

constexpr std::array<MigrationDefinition, 14> kCanonicalMigrations{{
    {
        .id = 1,
        .name = "core project foundation",
        .sql = R"sql(
CREATE TABLE project_state (
    id INTEGER PRIMARY KEY CHECK (id = 1),
    project_uuid TEXT NOT NULL,
    display_name TEXT NOT NULL,
    traffic_side TEXT NOT NULL CHECK (traffic_side IN ('left','right')),
    revision INTEGER NOT NULL CHECK (revision >= 1),
    saved_revision INTEGER NOT NULL CHECK (saved_revision >= 0),
    created_at TEXT NOT NULL,
    modified_at TEXT NOT NULL
);

CREATE TABLE georeference (
    id INTEGER PRIMARY KEY CHECK (id = 1),
    horizontal_crs TEXT NOT NULL,
    linear_unit TEXT NOT NULL,
    axis_convention TEXT NOT NULL,
    origin_easting REAL NOT NULL,
    origin_northing REAL NOT NULL,
    vertical_crs TEXT NOT NULL
);
)sql",
    },
    {
        .id = 2,
        .name = "georeference origin height",
        .sql = R"sql(
ALTER TABLE georeference
    ADD COLUMN origin_height REAL NOT NULL DEFAULT 0.0;
)sql",
    },
    {
        .id = 3,
        .name = "terrain datasets",
        .sql = R"sql(
CREATE TABLE terrain_datasets (
    id TEXT PRIMARY KEY,
    display_name TEXT NOT NULL,
    storage_path TEXT NOT NULL,
    source_format TEXT NOT NULL,
    source_crs TEXT NOT NULL,
    raster_width INTEGER NOT NULL CHECK (raster_width > 0),
    raster_height INTEGER NOT NULL CHECK (raster_height > 0),
    origin_x REAL NOT NULL,
    origin_y REAL NOT NULL,
    cell_size_x REAL NOT NULL CHECK (cell_size_x > 0),
    cell_size_y REAL NOT NULL CHECK (cell_size_y > 0),
    elevation_unit TEXT NOT NULL,
    elevation_unit_to_metre REAL NOT NULL CHECK (elevation_unit_to_metre > 0),
    has_nodata INTEGER NOT NULL CHECK (has_nodata IN (0, 1)),
    nodata_value REAL NOT NULL DEFAULT 0.0,
    min_z REAL NOT NULL,
    max_z REAL NOT NULL,
    bounds_east REAL NOT NULL,
    bounds_west REAL NOT NULL,
    bounds_north REAL NOT NULL,
    bounds_south REAL NOT NULL,
    source_sha256 TEXT NOT NULL,
    source_bytes INTEGER NOT NULL CHECK (source_bytes > 0),
    revision INTEGER NOT NULL CHECK (revision >= 1),
    diagnostics TEXT NOT NULL DEFAULT '[]',
    created_at TEXT NOT NULL,
    modified_at TEXT NOT NULL
);
)sql",
    },
    {
        .id = 4,
        .name = "terrain source attribution",
        .sql = R"sql(
ALTER TABLE terrain_datasets
    ADD COLUMN source_attribution TEXT NOT NULL DEFAULT '';
)sql",
    },
    {
        .id = 5,
        .name = "terrain coverage pieces",
        .sql = R"sql(
CREATE TABLE terrain_dataset_coverage (
    dataset_id TEXT NOT NULL,
    piece_index INTEGER NOT NULL,
    min_easting REAL NOT NULL,
    min_northing REAL NOT NULL,
    max_easting REAL NOT NULL,
    max_northing REAL NOT NULL,
    PRIMARY KEY (dataset_id, piece_index),
    FOREIGN KEY (dataset_id) REFERENCES terrain_datasets(id) ON DELETE CASCADE
);
CREATE INDEX idx_terrain_coverage_dataset ON terrain_dataset_coverage(dataset_id);
)sql",
    },
    {
        .id = 6,
        .name = "terrain source unit semantics",
        .sql = R"sql(
ALTER TABLE terrain_datasets ADD COLUMN horizontal_unit_name TEXT NOT NULL DEFAULT 'unknown';
ALTER TABLE terrain_datasets ADD COLUMN horizontal_unit_symbol TEXT NOT NULL DEFAULT 'units';
ALTER TABLE terrain_datasets ADD COLUMN horizontal_unit_is_angular INTEGER NOT NULL DEFAULT 0 CHECK (horizontal_unit_is_angular IN (0, 1));
ALTER TABLE terrain_datasets ADD COLUMN elevation_unit_source TEXT NOT NULL DEFAULT 'legacy';
ALTER TABLE terrain_datasets ADD COLUMN sample_scale REAL NOT NULL DEFAULT 1.0;
ALTER TABLE terrain_datasets ADD COLUMN sample_offset REAL NOT NULL DEFAULT 0.0;
)sql",
    },
    {
        .id = 7,
        .name = "road canonical geometry",
        .sql = R"sql(
CREATE TABLE roads (
    id TEXT PRIMARY KEY,
    display_name TEXT NOT NULL,
    created_at TEXT NOT NULL,
    modified_at TEXT NOT NULL
);

CREATE TABLE road_segments (
    road_id TEXT NOT NULL,
    segment_index INTEGER NOT NULL CHECK (segment_index >= 0),
    segment_kind TEXT NOT NULL CHECK (segment_kind IN ('line','circular_arc','clothoid')),
    start_easting REAL NOT NULL,
    start_northing REAL NOT NULL,
    start_heading REAL NOT NULL,
    length REAL NOT NULL CHECK (length > 0),
    curvature REAL NOT NULL DEFAULT 0.0,
    start_curvature REAL NOT NULL DEFAULT 0.0,
    end_curvature REAL NOT NULL DEFAULT 0.0,
    PRIMARY KEY (road_id, segment_index),
    FOREIGN KEY (road_id) REFERENCES roads(id) ON DELETE CASCADE
);
CREATE INDEX idx_road_segments_road ON road_segments(road_id);

CREATE TABLE road_elevation_breakpoints (
    road_id TEXT NOT NULL,
    breakpoint_index INTEGER NOT NULL CHECK (breakpoint_index >= 0),
    station REAL NOT NULL,
    value REAL NOT NULL,
    PRIMARY KEY (road_id, breakpoint_index),
    FOREIGN KEY (road_id) REFERENCES roads(id) ON DELETE CASCADE
);

CREATE TABLE road_superelevation_breakpoints (
    road_id TEXT NOT NULL,
    breakpoint_index INTEGER NOT NULL CHECK (breakpoint_index >= 0),
    station REAL NOT NULL,
    value REAL NOT NULL,
    PRIMARY KEY (road_id, breakpoint_index),
    FOREIGN KEY (road_id) REFERENCES roads(id) ON DELETE CASCADE
);

CREATE TABLE road_source (
    road_id TEXT PRIMARY KEY,
    provider TEXT NOT NULL CHECK (provider IN ('osm','opendrive','authored','other')),
    source_id TEXT NOT NULL DEFAULT '',
    source_crs TEXT NOT NULL DEFAULT '',
    imported_at TEXT NOT NULL DEFAULT '',
    tags TEXT NOT NULL DEFAULT '[]',
    FOREIGN KEY (road_id) REFERENCES roads(id) ON DELETE CASCADE
);

CREATE TABLE road_source_vertices (
    road_id TEXT NOT NULL,
    vertex_index INTEGER NOT NULL CHECK (vertex_index >= 0),
    x REAL NOT NULL,
    y REAL NOT NULL,
    z REAL,
    PRIMARY KEY (road_id, vertex_index),
    FOREIGN KEY (road_id) REFERENCES road_source(road_id) ON DELETE CASCADE
);

CREATE TABLE road_protected_anchors (
    road_id TEXT NOT NULL,
    anchor_index INTEGER NOT NULL CHECK (anchor_index >= 0),
    station REAL NOT NULL,
    easting REAL NOT NULL,
    northing REAL NOT NULL,
    kind TEXT NOT NULL CHECK (kind IN ('junction','endpoint','user_pinned','semantic')),
    PRIMARY KEY (road_id, anchor_index),
    FOREIGN KEY (road_id) REFERENCES roads(id) ON DELETE CASCADE
);
)sql",
    },
    {
        .id = 8,
        .name = "road fitting contract",
        .sql = R"sql(
ALTER TABLE roads ADD COLUMN position_tolerance REAL NOT NULL DEFAULT 1.0;
ALTER TABLE roads ADD COLUMN max_curvature REAL;
)sql",
    },
    {
        .id = 9,
        .name = "road control vertices",
        .sql = R"sql(
CREATE TABLE road_control_vertices (
    road_id TEXT NOT NULL,
    vertex_index INTEGER NOT NULL CHECK (vertex_index >= 0),
    x REAL NOT NULL,
    y REAL NOT NULL,
    z REAL,
    PRIMARY KEY (road_id, vertex_index),
    FOREIGN KEY (road_id) REFERENCES roads(id) ON DELETE CASCADE
);

-- Seed control vertices from the current source polyline so existing
-- roads remain editable. Source evidence is preserved unchanged in
-- road_source_vertices.
INSERT INTO road_control_vertices (road_id, vertex_index, x, y, z)
SELECT road_id, vertex_index, x, y, z FROM road_source_vertices;
)sql",
    },
    {
        .id = 10,
        .name = "road protected anchor continuity boundaries",
        .sql = R"sql(
ALTER TABLE roads ADD COLUMN anchor_boundary_segments TEXT NOT NULL DEFAULT '[]';
)sql",
    },
    {
        .id = 11,
        .name = "road surface width profile",
        .sql = R"sql(
CREATE TABLE road_width_breakpoints (
    road_id TEXT NOT NULL,
    breakpoint_index INTEGER NOT NULL CHECK (breakpoint_index >= 0),
    station REAL NOT NULL CHECK (station >= 0),
    left_width REAL NOT NULL CHECK (left_width >= 0),
    right_width REAL NOT NULL CHECK (right_width >= 0),
    PRIMARY KEY (road_id, breakpoint_index),
    FOREIGN KEY (road_id) REFERENCES roads(id) ON DELETE CASCADE
);
)sql",
    },
    {
        .id = 12,
        .name = "road lane sections and lanes",
        .sql = R"sql(
CREATE TABLE road_lane_sections (
    road_id TEXT NOT NULL,
    section_index INTEGER NOT NULL CHECK (section_index >= 0),
    start_station REAL NOT NULL CHECK (start_station >= 0),
    end_station REAL NOT NULL CHECK (end_station >= start_station),
    PRIMARY KEY (road_id, section_index),
    FOREIGN KEY (road_id) REFERENCES roads(id) ON DELETE CASCADE
);

CREATE TABLE road_lanes (
    road_id TEXT NOT NULL,
    section_index INTEGER NOT NULL,
    lane_id TEXT NOT NULL,
    side TEXT NOT NULL CHECK (side IN ('left', 'right')),
    lane_index INTEGER NOT NULL CHECK (lane_index >= 1),
    type TEXT NOT NULL,
    direction TEXT NOT NULL,
    width REAL NOT NULL CHECK (width >= 0),
    PRIMARY KEY (road_id, section_index, side, lane_index),
    FOREIGN KEY (road_id) REFERENCES roads(id) ON DELETE CASCADE
);
)sql",
    },
    {
        .id = 13,
        .name = "junctions topology and connections",
        .sql = R"sql(
CREATE TABLE junctions (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    type TEXT NOT NULL,
    pos_x REAL NOT NULL,
    pos_y REAL NOT NULL,
    elevation REAL NOT NULL DEFAULT 0.0,
    revision INTEGER NOT NULL CHECK (revision >= 1)
);

CREATE TABLE junction_approaches (
    junction_id TEXT NOT NULL,
    approach_index INTEGER NOT NULL CHECK (approach_index >= 0),
    road_id TEXT NOT NULL,
    contact_point TEXT NOT NULL CHECK (contact_point IN ('start', 'end')),
    entry_point_x REAL NOT NULL,
    entry_point_y REAL NOT NULL,
    heading REAL NOT NULL,
    lane_ids TEXT NOT NULL DEFAULT '[]',
    PRIMARY KEY (junction_id, approach_index),
    FOREIGN KEY (junction_id) REFERENCES junctions(id) ON DELETE CASCADE
);

CREATE TABLE junction_connections (
    id TEXT PRIMARY KEY,
    junction_id TEXT NOT NULL,
    from_road_id TEXT NOT NULL,
    from_lane_id TEXT NOT NULL,
    to_road_id TEXT NOT NULL,
    to_lane_id TEXT NOT NULL,
    movement_type TEXT NOT NULL,
    allowed INTEGER NOT NULL CHECK (allowed IN (0, 1)),
    FOREIGN KEY (junction_id) REFERENCES junctions(id) ON DELETE CASCADE
);
)sql",
    },
    {
        .id = 14,
        .name = "road construction kind",
        .sql = R"sql(
ALTER TABLE roads ADD COLUMN construction_kind TEXT NOT NULL DEFAULT 'fitted';
)sql",
    },
}};

} // namespace

std::span<const MigrationDefinition> canonicalMigrations() {
    return kCanonicalMigrations;
}

std::int64_t latestSupportedSchemaVersion(const std::span<const MigrationDefinition> migrations) {
    return migrations.empty() ? 0 : migrations.back().id;
}

void createSchemaMigrationsTable(SqliteConnection& connection) {
    connection.exec(R"sql(
CREATE TABLE IF NOT EXISTS schema_migrations (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    applied_at TEXT NOT NULL
);
)sql");
}

std::int64_t readAppliedSchemaVersion(const SqliteConnection& connection) {
    SqliteStatement tableExists{connection,
        "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = 'schema_migrations'"};
    if (!tableExists.step()) {
        throw SqliteError("sqlite_master probe returned no row", SQLITE_CORRUPT);
    }
    if (tableExists.columnInt64(0) == 0) {
        return 0;
    }

    SqliteStatement versionStatement{connection, "SELECT COALESCE(MAX(id), 0) FROM schema_migrations"};
    if (!versionStatement.step()) {
        throw SqliteError("schema_migrations returned no version row", SQLITE_CORRUPT);
    }
    return versionStatement.columnInt64(0);
}

void applyPendingMigrations(SqliteConnection& connection, const std::span<const MigrationDefinition> migrations) {
    createSchemaMigrationsTable(connection);

    SqliteStatement versionStatement{connection, "SELECT COALESCE(MAX(id), 0) FROM schema_migrations"};
    if (!versionStatement.step()) {
        throw SqliteError("schema_migrations returned no version row", SQLITE_CORRUPT);
    }
    const std::int64_t appliedVersion = versionStatement.columnInt64(0);

    const std::int64_t latestSupported = latestSupportedSchemaVersion(migrations);
    if (appliedVersion > latestSupported) {
        throw SqliteError(
            "project database schema version " + std::to_string(appliedVersion)
                + " is newer than the supported version " + std::to_string(latestSupported),
            SQLITE_CANTOPEN);
    }

    for (const MigrationDefinition& migration : migrations) {
        if (migration.id <= appliedVersion) {
            continue;
        }

        SqliteTransaction transaction{connection};
        connection.exec(std::string{migration.sql});
        SqliteStatement insert{connection,
            "INSERT INTO schema_migrations (id, name, applied_at) VALUES (?, ?, ?)"};
        insert.bindInt64(1, migration.id);
        insert.bindText(2, migration.name);
        insert.bindText(3, runtime::utcTimestampNow());
        // INSERT statements report success via SQLITE_DONE (step() == false);
        // error codes still throw through SqliteError.
        (void)insert.step();
        transaction.commit();

        const std::string migrationIdText = std::to_string(migration.id);
        const std::string migrationName{migration.name};
        runtime::logInfo("persistence", "schema.migration_applied",
            {{"migration", migrationIdText}, {"name", migrationName}});
    }
}

} // namespace infraforge::persistence
