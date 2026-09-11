#pragma once

#include <cstdint>
#include <string_view>

namespace infraforge::persistence {

class SqliteConnection;

inline constexpr std::int64_t kLatestSchemaVersion = 1;

// Forward-only schema migrations. Each migration applies inside its own
// transaction and records its id and application time in schema_migrations.
// Opening a database whose applied version exceeds kLatestSchemaVersion must
// fail without modifying the file; callers therefore perform the version
// probe on a read-only connection before enabling writes.
void createSchemaMigrationsTable(SqliteConnection& connection);

// Highest applied migration id, or 0 when none are applied yet. Read-only.
[[nodiscard]] std::int64_t readAppliedSchemaVersion(const SqliteConnection& connection);

// Applies pending migrations; throws when the database is newer than the
// supported schema. Must not be called with a version above supported.
void applyPendingMigrations(SqliteConnection& connection);

} // namespace infraforge::persistence
