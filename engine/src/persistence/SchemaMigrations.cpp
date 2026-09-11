#include "infraforge/persistence/SchemaMigrations.hpp"

#include "infraforge/persistence/SqliteConnection.hpp"
#include "infraforge/runtime/Logging.hpp"
#include "infraforge/runtime/Timestamp.hpp"

#include <array>
#include <sqlite3.h>
#include <string>

namespace infraforge::persistence {
namespace {

struct MigrationDefinition {
    std::int64_t id;
    std::string_view name;
    std::string_view sql;
};

constexpr std::array<MigrationDefinition, 1> kMigrations{{
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
    }},
};

} // namespace

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

void applyPendingMigrations(SqliteConnection& connection) {
    createSchemaMigrationsTable(connection);

    SqliteStatement versionStatement{connection, "SELECT COALESCE(MAX(id), 0) FROM schema_migrations"};
    if (!versionStatement.step()) {
        throw SqliteError("schema_migrations returned no version row", SQLITE_CORRUPT);
    }
    const std::int64_t appliedVersion = versionStatement.columnInt64(0);

    if (appliedVersion > kLatestSchemaVersion) {
        throw SqliteError(
            "project database schema version " + std::to_string(appliedVersion)
                + " is newer than the supported version " + std::to_string(kLatestSchemaVersion),
            SQLITE_CANTOPEN);
    }

    for (const auto& migration : kMigrations) {
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
