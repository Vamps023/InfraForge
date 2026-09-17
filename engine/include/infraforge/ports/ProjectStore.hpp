#pragma once

#include "infraforge/domain/project/ProjectModel.hpp"
#include "infraforge/domain/road/RoadRecord.hpp"
#include "infraforge/domain/terrain/TerrainDataset.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace infraforge::ports {

// Failure categories reported by the project persistence adapter. The
// application layer maps these to protocol error codes.
enum class StoreErrorCategory {
    DirectoryInvalid,
    FormatUnsupported,
    SchemaUnsupported,
    PersistenceFailure,
    NotFound,
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

// Result of committing a new terrain dataset: the post-mutation project
// record (revision advanced) plus the persisted dataset.
struct TerrainDatasetInsertResult {
    domain::project::ProjectRecord record;
    domain::terrain::TerrainDataset dataset;
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

    // Replaces the canonical georeference of the open project: rewrites the
    // persisted configuration and the manifest discovery copy, advances the
    // revision, and marks the session dirty until the next save.
    [[nodiscard]] virtual domain::project::ProjectRecord updateGeoreference(
        const domain::geo::GeoreferenceConfig& georeference) = 0;

    // Canonical terrain datasets of the open project, ordered by creation.
    [[nodiscard]] virtual std::vector<domain::terrain::TerrainDataset> terrainDatasets() const = 0;

    // Persists a new terrain dataset and advances the project revision (a
    // canonical mutation; the session becomes dirty until the next save).
    // The dataset must already exist in project-owned storage; this call
    // commits its canonical record in one transaction.
    [[nodiscard]] virtual TerrainDatasetInsertResult insertTerrainDataset(
        const domain::terrain::TerrainDataset& dataset) = 0;

    // Removes a terrain dataset row and advances the project revision.
    // Used for transactional rollback when a canonical commit partially
    // fails after the DB row was inserted. The caller is responsible for
    // removing the project-owned raster file separately.
    virtual void removeTerrainDataset(const std::string& datasetId) = 0;

    // Canonical roads of the open project, ordered by creation.
    [[nodiscard]] virtual std::vector<domain::road::RoadRecord> roads() const = 0;

    // Persists a new canonical road and advances the project revision (a
    // canonical mutation; the session becomes dirty until the next save).
    // The road record must be validated; this call commits its canonical
    // record in one transaction.
    [[nodiscard]] virtual domain::road::RoadRecord insertRoad(
        const domain::road::RoadRecord& road) = 0;

    // Replaces an existing canonical road's full record (segments, profiles,
    // source, anchors) and advances the project revision. The road must
    // already exist; this call does not create it.
    [[nodiscard]] virtual domain::road::RoadRecord updateRoad(
        const domain::road::RoadRecord& road) = 0;

    // Removes a road and all its segments/profiles/source data, advancing
    // the project revision.
    virtual void removeRoad(const std::string& roadId) = 0;

    // Flushes and closes the active project session.
    virtual void close() = 0;
};

} // namespace infraforge::ports
