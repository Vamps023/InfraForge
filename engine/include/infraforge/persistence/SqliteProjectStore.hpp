#pragma once

#include "infraforge/ports/ProjectStore.hpp"
#include "infraforge/persistence/SchemaMigrations.hpp"
#include "infraforge/persistence/SqliteConnection.hpp"

#include <filesystem>
#include <optional>
#include <utility>

namespace infraforge::persistence {

// SQLite-backed implementation of the canonical project session store.
// Follows docs/02_DATA/PROJECT_FORMAT.md (project.json + project.db) and the
// forward-only migration policy of docs/02_DATA/DATABASE_SCHEMA.md.
//
// Failure boundary: every public method translates SQLite-level errors into
// ports::StoreError(PersistenceFailure) so protocol callers always see the
// persistence taxonomy instead of engine-internal codes.
class SqliteProjectStore final : public ports::ProjectStore {
public:
    // Production uses the canonical migration set; tests inject custom sets
    // to exercise old-project upgrade paths.
    explicit SqliteProjectStore(
        std::span<const MigrationDefinition> migrations = canonicalMigrations());
    ~SqliteProjectStore() override = default;

    SqliteProjectStore(const SqliteProjectStore&) = delete;
    SqliteProjectStore& operator=(const SqliteProjectStore&) = delete;

    [[nodiscard]] bool isOpen() const override;
    [[nodiscard]] const domain::project::ProjectRecord& current() const override;

    [[nodiscard]] domain::project::ProjectRecord create(const domain::project::CreateProjectSpec& spec) override;
    [[nodiscard]] domain::project::ProjectRecord open(const std::filesystem::path& projectDirectory) override;
    [[nodiscard]] domain::project::ProjectRecord save() override;
    [[nodiscard]] domain::project::ProjectRecord saveAs(const domain::project::SaveAsSpec& spec) override;
    void close() override;

private:
    template <typename Operation>
    auto withinStoreBoundary(Operation&& operation) -> decltype(operation()) {
        try {
            return operation();
        } catch (const SqliteError& error) {
            throw ports::StoreError(ports::StoreErrorCategory::PersistenceFailure, error.what());
        }
    }

    [[nodiscard]] domain::project::ProjectRecord createImpl(const domain::project::CreateProjectSpec& spec);
    [[nodiscard]] domain::project::ProjectRecord openImpl(const std::filesystem::path& projectDirectory);
    [[nodiscard]] domain::project::ProjectRecord saveImpl();
    [[nodiscard]] domain::project::ProjectRecord saveAsImpl(const domain::project::SaveAsSpec& spec);
    void closeImpl();

    [[nodiscard]] domain::project::ProjectRecord readRecord(const SqliteConnection& connection) const;
    [[nodiscard]] std::int64_t latestSupportedSchemaVersion() const;

    std::span<const MigrationDefinition> migrations_;
    std::optional<SqliteConnection> connection_;
    domain::project::ProjectRecord record_;
    std::filesystem::path directory_;
};

} // namespace infraforge::persistence
