#pragma once

#include "infraforge/domain/project/ProjectModel.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace infraforge::persistence {

inline constexpr std::string_view kProjectFormatId = "infraforge-project";
inline constexpr int kProjectFormatVersion = 1;
inline constexpr std::string_view kManifestFileName = "project.json";
inline constexpr std::string_view kDatabaseFileName = "project.db";

// Strict representation of project.json. Unknown keys, unknown format ids and
// unsupported versions are rejected; the manifest is never auto-repaired.
struct ProjectManifest {
    int formatVersion{kProjectFormatVersion};
    std::string projectUuid;
    std::string displayName;
    std::string createdAt;
    std::string modifiedAt;
    std::string databasePath{kDatabaseFileName};
    int projectSchemaVersion{1};
    std::string minimumApplicationVersion;
    domain::project::GeoreferenceConfig georeference;
};

// Parses and validates project.json inside the given project directory.
// Throws StoreError (DirectoryInvalid / FormatUnsupported / PersistenceFailure).
[[nodiscard]] ProjectManifest readProjectManifest(const std::filesystem::path& projectDirectory);

// Writes project.json atomically (temp file + rename) with deterministic key
// order. Throws StoreError on I/O failure.
void writeProjectManifest(const std::filesystem::path& projectDirectory, const ProjectManifest& manifest);

// Removes the manifest file during rollback of a failed project creation.
void removeManifestFile(const std::filesystem::path& projectDirectory);

} // namespace infraforge::persistence
