#pragma once

#include "infraforge/ports/ProjectStore.hpp"
#include "infraforge/persistence/SqliteConnection.hpp"

#include <filesystem>
#include <optional>

namespace infraforge::persistence {

// SQLite-backed implementation of the canonical project session store.
// Follows docs/02_DATA/PROJECT_FORMAT.md (project.json + project.db) and the
// forward-only migration policy of docs/02_DATA/DATABASE_SCHEMA.md.
class SqliteProjectStore final : public ports::ProjectStore {
public:
    SqliteProjectStore() = default;
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
    [[nodiscard]] domain::project::ProjectRecord readRecord(const SqliteConnection& connection) const;

    std::optional<SqliteConnection> connection_;
    domain::project::ProjectRecord record_;
    std::filesystem::path directory_;
};

} // namespace infraforge::persistence
