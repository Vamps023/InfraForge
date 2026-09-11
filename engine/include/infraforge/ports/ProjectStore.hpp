#pragma once

#include "infraforge/domain/project/ProjectModel.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>

namespace infraforge::ports {

// Failure categories reported by the project persistence adapter. The
// application layer maps these to protocol error codes.
enum class StoreErrorCategory {
    DirectoryInvalid,
    FormatUnsupported,
    SchemaUnsupported,
    PersistenceFailure,
};

class StoreError : public std::runtime_error {
public:
    StoreError(StoreErrorCategory category, std::string message)
        : std::runtime_error(std::move(message)),
          category_(category) {}

    [[nodiscard]] StoreErrorCategory category() const noexcept { return category_; }

private:
    StoreErrorCategory category_;
};

// Owns the SQLite connection and manifest of the currently open project.
// Exactly one project session is active at a time. All methods are called
// from the single application executor thread; implementations are not
// required to be thread-safe.
class ProjectStore {
public:
    virtual ~ProjectStore() = default;

    [[nodiscard]] virtual bool isOpen() const = 0;
    // Returns the active record; throws std::logic_error when no project is
    // open (callers check isOpen first).
    [[nodiscard]] virtual const domain::project::ProjectRecord& current() const = 0;

    // Creates <parent>/<name>.iforge with manifest + migrated database and
    // leaves the new project open. Fails when the target directory exists.
    [[nodiscard]] virtual domain::project::ProjectRecord create(const domain::project::CreateProjectSpec& spec) = 0;

    // Opens and validates an existing project directory. Newer format/schema
    // versions are rejected without modification.
    [[nodiscard]] virtual domain::project::ProjectRecord open(const std::filesystem::path& projectDirectory) = 0;

    // Persists the save marker (saved_revision) and manifest timestamps.
    [[nodiscard]] virtual domain::project::ProjectRecord save() = 0;

    // Copies the open project to a new project directory with a fresh
    // identity and switches the session to it. The source stays untouched
    // when the copy fails.
    [[nodiscard]] virtual domain::project::ProjectRecord saveAs(const domain::project::SaveAsSpec& spec) = 0;

    // Flushes and closes the active project session.
    virtual void close() = 0;
};

} // namespace infraforge::ports
