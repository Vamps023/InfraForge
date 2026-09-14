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
    [[nodiscard]] domain::project::ProjectRecord updateGeoreference(
        const domain::geo::GeoreferenceConfig& georeference) override;
    [[nodiscard]] std::vector<domain::terrain::TerrainDataset> terrainDatasets() const override;
    [[nodiscard]] ports::TerrainDatasetInsertResult insertTerrainDataset(
        const domain::terrain::TerrainDataset& dataset) override;
    void removeTerrainDataset(const std::string& datasetId) override;
    [[nodiscard]] std::vector<domain::road::RoadRecord> roads() const override;
    [[nodiscard]] domain::road::RoadRecord insertRoad(
        const domain::road::RoadRecord& road) override;
    [[nodiscard]] domain::road::RoadRecord updateRoad(
        const domain::road::RoadRecord& road) override;
    void removeRoad(const std::string& roadId) override;
    void close() override;

private:
    template <typename Operation>
    auto withinStoreBoundary(Operation&& operation) const -> decltype(operation()) {
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
    [[nodiscard]] domain::project::ProjectRecord updateGeoreferenceImpl(
        const domain::geo::GeoreferenceConfig& georeference);
    [[nodiscard]] std::vector<domain::terrain::TerrainDataset> terrainDatasetsImpl() const;
    [[nodiscard]] ports::TerrainDatasetInsertResult insertTerrainDatasetImpl(
        const domain::terrain::TerrainDataset& dataset);
    void removeTerrainDatasetImpl(const std::string& datasetId);
    [[nodiscard]] std::vector<domain::road::RoadRecord> roadsImpl() const;
    [[nodiscard]] domain::road::RoadRecord insertRoadImpl(
        const domain::road::RoadRecord& road);
    [[nodiscard]] domain::road::RoadRecord updateRoadImpl(
        const domain::road::RoadRecord& road);
    void removeRoadImpl(const std::string& roadId);
    void closeImpl();

    [[nodiscard]] domain::project::ProjectRecord readRecord(const SqliteConnection& connection) const;
    [[nodiscard]] std::int64_t latestSupportedSchemaVersion() const;
    // Set after schema setup on create/open: whether the live georeference
    // table carries the migration-2 origin_height column. Older schemas
    // (and test stores pinned to earlier migration lists) keep v1 behavior
    // so reads and writes stay consistent with the actual database.
    [[nodiscard]] bool georeferenceHasOriginHeight() const { return originHeightSupported_; }

    std::span<const MigrationDefinition> migrations_;
    bool originHeightSupported_{false};
    std::optional<SqliteConnection> connection_;
    domain::project::ProjectRecord record_;
    std::filesystem::path directory_;
};

} // namespace infraforge::persistence
