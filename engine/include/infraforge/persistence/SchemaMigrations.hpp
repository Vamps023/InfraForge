#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace infraforge::persistence {

class SqliteConnection;

struct MigrationDefinition {
    std::int64_t id;
    std::string_view name;
    std::string_view sql;
};

// The schema this engine ships. Ordered by id; the last entry's id is the
// latest supported schema version.
[[nodiscard]] std::span<const MigrationDefinition> canonicalMigrations();

[[nodiscard]] std::int64_t latestSupportedSchemaVersion(std::span<const MigrationDefinition> migrations);

// Forward-only schema migrations. Each migration applies inside its own
// transaction and records its id and application time in schema_migrations.
// The migration list is a parameter so tests can exercise old-project
// upgrade paths; production always passes canonicalMigrations().
// Opening a database whose applied version exceeds the list's latest id must
// fail without modifying the file; callers therefore perform the version
// probe on a read-only connection before enabling writes.
void createSchemaMigrationsTable(SqliteConnection& connection);

// Highest applied migration id, or 0 when none are applied yet. Read-only.
[[nodiscard]] std::int64_t readAppliedSchemaVersion(const SqliteConnection& connection);

// Applies pending migrations from the given list; throws when the database
// is newer than the list's latest version. Must not be called with a version
// above the supported latest.
void applyPendingMigrations(SqliteConnection& connection, std::span<const MigrationDefinition> migrations);

} // namespace infraforge::persistence
