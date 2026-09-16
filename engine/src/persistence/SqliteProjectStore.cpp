#include "infraforge/persistence/SqliteProjectStore.hpp"

#include "infraforge/domain/terrain/TerrainDataset.hpp"
#include "infraforge/domain/terrain/TerrainTypes.hpp"
#include "infraforge/persistence/ProjectManifest.hpp"
#include "infraforge/runtime/FileSystemUtf8.hpp"
#include "infraforge/runtime/Logging.hpp"
#include "infraforge/runtime/Timestamp.hpp"
#include "infraforge/runtime/Uuid.hpp"
#include "infraforge/version.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace infraforge::persistence {
namespace {

using domain::project::ProjectRecord;

[[noreturn]] void fail(ports::StoreErrorCategory category, std::string message) {
    throw ports::StoreError(category, std::move(message));
}

constexpr std::array<std::string_view, 9> kProjectSubdirectories{
    "assets/models",
    "assets/textures",
    "assets/materials",
    "terrain/elevation",
    "terrain/imagery",
    "scenarios",
    "cache",
    "autosave",
    "logs",
};

ProjectManifest manifestFromRecord(const ProjectRecord& record, const std::string& createdAt) {
    ProjectManifest manifest;
    manifest.projectUuid = record.uuid;
    manifest.displayName = record.displayName;
    manifest.createdAt = createdAt;
    manifest.databasePath = std::string{kDatabaseFileName};
    manifest.minimumApplicationVersion = std::string{infraforge::kEngineVersion};
    manifest.georeference = record.georeference;
    return manifest;
}

void createDirectorySkeleton(const std::filesystem::path& directory) {
    std::error_code ioError;
    for (const std::string_view relative : kProjectSubdirectories) {
        const auto subdirectory = directory / relative;
        std::filesystem::create_directories(subdirectory, ioError);
        if (ioError) {
            fail(ports::StoreErrorCategory::PersistenceFailure,
                "cannot create project subdirectory '" + std::string{relative} + "': " + ioError.message());
        }
    }
}

void insertProjectRows(SqliteConnection& connection, const ProjectRecord& record, const bool withOriginHeight) {
    SqliteTransaction transaction{connection};

    SqliteStatement state{connection,
        "INSERT INTO project_state "
        "(id, project_uuid, display_name, traffic_side, revision, saved_revision, created_at, modified_at) "
        "VALUES (1, ?, ?, ?, ?, ?, ?, ?)"};
    state.bindText(1, record.uuid);
    state.bindText(2, record.displayName);
    state.bindText(3, domain::project::trafficSideName(record.trafficSide));
    state.bindInt64(4, static_cast<std::int64_t>(record.revision));
    state.bindInt64(5, static_cast<std::int64_t>(record.savedRevision));
    state.bindText(6, record.createdAt);
    state.bindText(7, record.modifiedAt);
    (void)state.step();

    SqliteStatement georeference{connection,
        withOriginHeight
            ? "INSERT INTO georeference "
              "(id, horizontal_crs, linear_unit, axis_convention, origin_easting, origin_northing, origin_height, vertical_crs) "
              "VALUES (1, ?, ?, ?, ?, ?, ?, ?)"
            : "INSERT INTO georeference "
              "(id, horizontal_crs, linear_unit, axis_convention, origin_easting, origin_northing, vertical_crs) "
              "VALUES (1, ?, ?, ?, ?, ?, ?)"};
    georeference.bindText(1, record.georeference.horizontalCrs);
    georeference.bindText(2, record.georeference.linearUnit);
    georeference.bindText(3, domain::geo::axisConventionName(record.georeference.axisConvention));
    georeference.bindDouble(4, record.georeference.originEasting);
    georeference.bindDouble(5, record.georeference.originNorthing);
    if (withOriginHeight) {
        georeference.bindDouble(6, record.georeference.originHeight);
        georeference.bindText(7, record.georeference.verticalCrs);
    } else {
        georeference.bindText(6, record.georeference.verticalCrs);
    }
    (void)georeference.step();

    transaction.commit();
}

// Whether the georeference table in this database carries the migration-2
// origin_height column. Probed by preparing a read; statement preparation
// fails when the column is absent.
[[nodiscard]] bool probeOriginHeightColumn(const SqliteConnection& connection) {
    try {
        SqliteStatement probe{connection, "SELECT origin_height FROM georeference WHERE 0"};
        return true;
    } catch (const SqliteError&) {
        return false;
    }
}

} // namespace

SqliteProjectStore::SqliteProjectStore(const std::span<const MigrationDefinition> migrations)
    : migrations_(migrations) {}

std::int64_t SqliteProjectStore::latestSupportedSchemaVersion() const {
    return persistence::latestSupportedSchemaVersion(migrations_);
}

bool SqliteProjectStore::isOpen() const {
    return connection_.has_value();
}

const ProjectRecord& SqliteProjectStore::current() const {
    if (!connection_.has_value()) {
        throw std::logic_error("no project session is open");
    }
    return record_;
}

ProjectRecord SqliteProjectStore::create(const domain::project::CreateProjectSpec& spec) {
    return withinStoreBoundary([&] { return createImpl(spec); });
}

ProjectRecord SqliteProjectStore::open(const std::filesystem::path& projectDirectory) {
    return withinStoreBoundary([&] { return openImpl(projectDirectory); });
}

ProjectRecord SqliteProjectStore::save() {
    return withinStoreBoundary([&] { return saveImpl(); });
}

ProjectRecord SqliteProjectStore::saveAs(const domain::project::SaveAsSpec& spec) {
    return withinStoreBoundary([&] { return saveAsImpl(spec); });
}

void SqliteProjectStore::close() {
    withinStoreBoundary([&] { closeImpl(); });
}

ProjectRecord SqliteProjectStore::updateGeoreference(const domain::geo::GeoreferenceConfig& georeference) {
    return withinStoreBoundary([&] { return updateGeoreferenceImpl(georeference); });
}

ProjectRecord SqliteProjectStore::createImpl(const domain::project::CreateProjectSpec& spec) {
    if (connection_.has_value()) {
        throw std::logic_error("cannot create a project while another session is open");
    }
    if (const auto nameError = domain::project::validateDisplayName(spec.displayName); nameError.has_value()) {
        fail(ports::StoreErrorCategory::DirectoryInvalid, nameError->field + ": " + nameError->message);
    }

    std::error_code ioError;
    if (!std::filesystem::is_directory(spec.parentDirectory, ioError)) {
        fail(ports::StoreErrorCategory::DirectoryInvalid,
            "parent directory does not exist: " + spec.parentDirectory.string());
    }

    const auto directory = spec.parentDirectory / (spec.displayName + ".iforge");
    if (std::filesystem::exists(directory, ioError)) {
        fail(ports::StoreErrorCategory::DirectoryInvalid,
            "project directory already exists: " + directory.string());
    }
    if (!std::filesystem::create_directory(directory, ioError) || ioError) {
        fail(ports::StoreErrorCategory::PersistenceFailure,
            "cannot create project directory '" + directory.string() + "': " + ioError.message());
    }

    try {
        createDirectorySkeleton(directory);

        ProjectRecord record;
        record.uuid = runtime::generateUuidV4();
        record.displayName = spec.displayName;
        record.revision = 1;
        record.savedRevision = 1;
        record.trafficSide = spec.trafficSide;
        record.georeference = spec.georeference;
        record.createdAt = runtime::utcTimestampNow();
        record.modifiedAt = record.createdAt;

        writeProjectManifest(directory, manifestFromRecord(record, record.createdAt));

        auto connection = SqliteConnection::open(directory / kDatabaseFileName, SqliteOpenMode::ReadWriteCreate);
        applyPendingMigrations(connection, migrations_);
        originHeightSupported_ = probeOriginHeightColumn(connection);
        insertProjectRows(connection, record, originHeightSupported_);

        connection_ = std::move(connection);
        directory_ = directory;
        record_ = record;
        record_.directory = runtime::utf8String(directory);
        return record_;
    } catch (...) {
        // The directory was created by this call and is incomplete; remove it
        // so a retried create does not hit "already exists".
        std::error_code cleanupError;
        (void)std::filesystem::remove_all(directory, cleanupError);
        throw;
    }
}

ProjectRecord SqliteProjectStore::openImpl(const std::filesystem::path& projectDirectory) {
    if (connection_.has_value()) {
        throw std::logic_error("cannot open a project while another session is open");
    }

    const ProjectManifest manifest = readProjectManifest(projectDirectory);
    const auto databaseFile = projectDirectory / manifest.databasePath;
    std::error_code ioError;
    if (!std::filesystem::is_regular_file(databaseFile, ioError)) {
        fail(ports::StoreErrorCategory::PersistenceFailure,
            "project database is missing: " + databaseFile.string());
    }

    // Version probe happens on a read-only connection so an unsupported
    // newer schema is rejected without modifying the project. The database
    // is the single authority for the schema version; the manifest carries
    // no copy that migrations would have to keep in sync.
    {
        auto probe = SqliteConnection::open(databaseFile, SqliteOpenMode::ReadOnly);
        const std::int64_t appliedVersion = readAppliedSchemaVersion(probe);
        if (appliedVersion > latestSupportedSchemaVersion()) {
            fail(ports::StoreErrorCategory::SchemaUnsupported,
                "project database schema version " + std::to_string(appliedVersion)
                    + " is newer than the supported version " + std::to_string(latestSupportedSchemaVersion())
                    + "; the project was left unmodified");
        }
    }

    auto connection = SqliteConnection::open(databaseFile, SqliteOpenMode::ReadWrite);
    applyPendingMigrations(connection, migrations_);
    originHeightSupported_ = probeOriginHeightColumn(connection);

    ProjectRecord record = readRecord(connection);
    if (record.uuid != manifest.projectUuid) {
        fail(ports::StoreErrorCategory::PersistenceFailure,
            "project identity mismatch between manifest and database (corrupt project)");
    }
    if (record.displayName != manifest.displayName) {
        fail(ports::StoreErrorCategory::PersistenceFailure,
            "project display name mismatch between manifest and database (corrupt project)");
    }
    if (!(record.georeference == manifest.georeference)) {
        // Deterministic crash recovery: a georeference update writes the
        // manifest first and commits the database row second. A crash in
        // between leaves the manifest ahead of the canonical database.
        // The database row is the authority, so the manifest is rewritten
        // from it and the divergence is logged; the open then proceeds
        // with canonical state. Any other identity mismatch above still
        // fails as corruption.
        runtime::logError("persistence", "project.manifest_georeference_repaired",
            {{"detail", "manifest georeference diverged from the canonical database row; "
                "restoring the manifest from the database (possible interrupted update)"}});
        ProjectManifest repaired = manifest;
        repaired.georeference = record.georeference;
        writeProjectManifest(projectDirectory, repaired);
    }
    if (record.createdAt != manifest.createdAt) {
        fail(ports::StoreErrorCategory::PersistenceFailure,
            "creation timestamp mismatch between manifest and database (corrupt project)");
    }

    connection_ = std::move(connection);
    directory_ = projectDirectory;
    record_ = record;
    record_.directory = runtime::utf8String(projectDirectory);
    return record_;
}

ProjectRecord SqliteProjectStore::saveImpl() {
    if (!connection_.has_value()) {
        throw std::logic_error("cannot save without an open project session");
    }
    if (!record_.isDirty()) {
        return record_;
    }

    // One transactional resource: the database owns revision, saved_revision
    // and modified_at, so a save either fully lands or fails without any
    // half-persisted state. project.json is immutable discovery metadata and
    // is deliberately not rewritten here (docs/02_DATA/PROJECT_FORMAT.md).
    const std::string modifiedAt = runtime::utcTimestampNow();
    {
        SqliteTransaction transaction{*connection_};
        SqliteStatement update{*connection_,
            "UPDATE project_state SET saved_revision = revision, modified_at = ? WHERE id = 1"};
        update.bindText(1, modifiedAt);
        (void)update.step();
        if (connection_->lastChanges() != 1) {
            fail(ports::StoreErrorCategory::PersistenceFailure,
                "project_state row went missing during save (corrupt project database)");
        }
        transaction.commit();
    }

    record_.savedRevision = record_.revision;
    record_.modifiedAt = modifiedAt;
    return record_;
}

ProjectRecord SqliteProjectStore::saveAsImpl(const domain::project::SaveAsSpec& spec) {
    if (!connection_.has_value()) {
        throw std::logic_error("cannot save-as without an open project session");
    }
    if (const auto nameError = domain::project::validateDisplayName(spec.displayName); nameError.has_value()) {
        fail(ports::StoreErrorCategory::DirectoryInvalid, nameError->field + ": " + nameError->message);
    }

    std::error_code ioError;
    if (!std::filesystem::is_directory(spec.parentDirectory, ioError)) {
        fail(ports::StoreErrorCategory::DirectoryInvalid,
            "parent directory does not exist: " + spec.parentDirectory.string());
    }
    const auto targetDirectory = spec.parentDirectory / (spec.displayName + ".iforge");
    if (std::filesystem::exists(targetDirectory, ioError)) {
        fail(ports::StoreErrorCategory::DirectoryInvalid,
            "target project directory already exists: " + targetDirectory.string());
    }
    if (!std::filesystem::create_directory(targetDirectory, ioError) || ioError) {
        fail(ports::StoreErrorCategory::PersistenceFailure,
            "cannot create target project directory '" + targetDirectory.string() + "': " + ioError.message());
    }

    try {
        createDirectorySkeleton(targetDirectory);

        const std::string newUuid = runtime::generateUuidV4();
        const std::string now = runtime::utcTimestampNow();

        {
            SqliteStatement vacuum{*connection_, "VACUUM INTO ?"};
            vacuum.bindText(1, runtime::utf8String(targetDirectory / kDatabaseFileName));
            (void)vacuum.step();
        }

        ProjectRecord targetRecord = record_;
        targetRecord.uuid = newUuid;
        targetRecord.displayName = spec.displayName;
        targetRecord.modifiedAt = now;
        targetRecord.savedRevision = targetRecord.revision;
        targetRecord.createdAt = now;

        writeProjectManifest(targetDirectory, manifestFromRecord(targetRecord, now));

        auto targetConnection = SqliteConnection::open(targetDirectory / kDatabaseFileName, SqliteOpenMode::ReadWrite);
        {
            SqliteTransaction transaction{targetConnection};
            SqliteStatement update{targetConnection,
                "UPDATE project_state SET project_uuid = ?, display_name = ?, saved_revision = revision, created_at = ?, modified_at = ? WHERE id = 1"};
            update.bindText(1, newUuid);
            update.bindText(2, spec.displayName);
            update.bindText(3, now);
            update.bindText(4, now);
            (void)update.step();
            if (targetConnection.lastChanges() != 1) {
                fail(ports::StoreErrorCategory::PersistenceFailure,
                    "copied project database is missing its project_state row");
            }
            transaction.commit();
        }

        originHeightSupported_ = probeOriginHeightColumn(targetConnection);
        targetRecord = readRecord(targetConnection);
        if (targetRecord.uuid != newUuid || targetRecord.displayName != spec.displayName
            || targetRecord.revision != record_.revision
            || !(targetRecord.georeference == record_.georeference)) {
            fail(ports::StoreErrorCategory::PersistenceFailure,
                "copied project database failed identity verification");
        }

        connection_ = std::move(targetConnection);
        directory_ = targetDirectory;
        record_ = targetRecord;
        record_.directory = runtime::utf8String(targetDirectory);
        return record_;
    } catch (...) {
        // The target directory was created by this call and is incomplete;
        // remove it and keep the source session untouched.
        std::error_code cleanupError;
        (void)std::filesystem::remove_all(targetDirectory, cleanupError);
        throw;
    }
}

ProjectRecord SqliteProjectStore::updateGeoreferenceImpl(const domain::geo::GeoreferenceConfig& georeference) {
    if (!connection_.has_value()) {
        throw std::logic_error("cannot update the georeference without an open project session");
    }

    // The manifest carries a discovery copy of the georeference and is
    // verified against the database on open. Rewrite it first so a database
    // failure can be compensated by restoring the previous manifest; the
    // database transaction is the canonical write.
    const ProjectRecord previousRecord = record_;
    ProjectRecord updated = record_;
    updated.georeference = georeference;
    writeProjectManifest(directory_, manifestFromRecord(updated, updated.createdAt));

    const std::string modifiedAt = runtime::utcTimestampNow();
    try {
        SqliteTransaction transaction{*connection_};

        SqliteStatement update{*connection_,
            georeferenceHasOriginHeight()
                ? "UPDATE georeference SET horizontal_crs = ?, linear_unit = ?, axis_convention = ?, "
                  "origin_easting = ?, origin_northing = ?, origin_height = ?, vertical_crs = ? WHERE id = 1"
                : "UPDATE georeference SET horizontal_crs = ?, linear_unit = ?, axis_convention = ?, "
                  "origin_easting = ?, origin_northing = ?, vertical_crs = ? WHERE id = 1"};
        update.bindText(1, georeference.horizontalCrs);
        update.bindText(2, georeference.linearUnit);
        update.bindText(3, domain::geo::axisConventionName(georeference.axisConvention));
        update.bindDouble(4, georeference.originEasting);
        update.bindDouble(5, georeference.originNorthing);
        if (georeferenceHasOriginHeight()) {
            update.bindDouble(6, georeference.originHeight);
            update.bindText(7, georeference.verticalCrs);
        } else {
            update.bindText(6, georeference.verticalCrs);
        }
        (void)update.step();
        if (connection_->lastChanges() != 1) {
            fail(ports::StoreErrorCategory::PersistenceFailure,
                "georeference row went missing during update (corrupt project database)");
        }

        SqliteStatement state{*connection_,
            "UPDATE project_state SET revision = revision + 1, modified_at = ? WHERE id = 1"};
        state.bindText(1, modifiedAt);
        (void)state.step();
        if (connection_->lastChanges() != 1) {
            fail(ports::StoreErrorCategory::PersistenceFailure,
                "project_state row went missing during georeference update (corrupt project database)");
        }
        transaction.commit();
    } catch (...) {
        // Compensate the manifest rewrite so both files still describe the
        // same canonical georeference after the failed mutation.
        try {
            writeProjectManifest(directory_, manifestFromRecord(previousRecord, previousRecord.createdAt));
        } catch (const std::exception& restoreError) {
            runtime::logError("persistence", "project.manifest_restore_failed",
                {{"detail", restoreError.what()}});
        }
        throw;
    }

    record_ = updated;
    record_.revision += 1;
    record_.modifiedAt = modifiedAt;
    return record_;
}

void SqliteProjectStore::closeImpl() {
    if (!connection_.has_value()) {
        return;
    }
    connection_->close();
    connection_.reset();
    record_ = {};
    directory_.clear();
}

std::vector<domain::terrain::TerrainDataset> SqliteProjectStore::terrainDatasets() const {
    return withinStoreBoundary([&] { return terrainDatasetsImpl(); });
}

ports::TerrainDatasetInsertResult SqliteProjectStore::insertTerrainDataset(
    const domain::terrain::TerrainDataset& dataset) {
    return withinStoreBoundary([&] { return insertTerrainDatasetImpl(dataset); });
}

void SqliteProjectStore::removeTerrainDataset(const std::string& datasetId) {
    withinStoreBoundary([&] { removeTerrainDatasetImpl(datasetId); });
}

std::vector<domain::road::RoadRecord> SqliteProjectStore::roads() const {
    return withinStoreBoundary([&] { return roadsImpl(); });
}

domain::road::RoadRecord SqliteProjectStore::insertRoad(const domain::road::RoadRecord& road) {
    return withinStoreBoundary([&] { return insertRoadImpl(road); });
}

domain::road::RoadRecord SqliteProjectStore::updateRoad(const domain::road::RoadRecord& road) {
    return withinStoreBoundary([&] { return updateRoadImpl(road); });
}

void SqliteProjectStore::removeRoad(const std::string& roadId) {
    withinStoreBoundary([&] { removeRoadImpl(roadId); });
}

void SqliteProjectStore::removeTerrainDatasetImpl(const std::string& datasetId) {
    if (!connection_.has_value()) {
        throw std::logic_error("cannot remove a terrain dataset without an open project session");
    }
    const std::string modifiedAt = runtime::utcTimestampNow();
    {
        SqliteTransaction transaction{*connection_};
        SqliteStatement remove{*connection_,
            "DELETE FROM terrain_datasets WHERE id = ?"};
        remove.bindText(1, datasetId);
        (void)remove.step();
        if (connection_->lastChanges() != 1) {
            fail(ports::StoreErrorCategory::NotFound,
                "terrain dataset row not found for rollback: " + datasetId);
        }
        SqliteStatement state{*connection_,
            "UPDATE project_state SET revision = revision + 1, modified_at = ? WHERE id = 1"};
        state.bindText(1, modifiedAt);
        (void)state.step();
        if (connection_->lastChanges() != 1) {
            fail(ports::StoreErrorCategory::PersistenceFailure,
                "project_state row went missing during terrain dataset remove (corrupt project database)");
        }
        transaction.commit();
    }
    record_.revision += 1;
    record_.modifiedAt = modifiedAt;
}

std::vector<domain::terrain::TerrainDataset> SqliteProjectStore::terrainDatasetsImpl() const {
    if (!connection_.has_value()) {
        throw std::logic_error("cannot read terrain datasets without an open project session");
    }

    std::vector<domain::terrain::TerrainDataset> datasets;
    SqliteStatement rows{*connection_,
        "SELECT id, display_name, storage_path, source_format, source_crs, raster_width, raster_height, "
        "origin_x, origin_y, cell_size_x, cell_size_y, elevation_unit, elevation_unit_to_metre, "
        "has_nodata, nodata_value, min_z, max_z, bounds_east, bounds_west, bounds_north, bounds_south, "
        "source_sha256, source_bytes, revision, diagnostics, created_at, modified_at, source_attribution, "
        "horizontal_unit_name, horizontal_unit_symbol, horizontal_unit_is_angular, elevation_unit_source, sample_scale, sample_offset "
        "FROM terrain_datasets ORDER BY created_at, id"};
    while (rows.step()) {
        domain::terrain::TerrainDataset dataset;
        dataset.id = domain::terrain::entityIdFromUuidText(std::string{rows.columnText(0)});
        dataset.displayName = std::string{rows.columnText(1)};
        dataset.storagePath = std::string{rows.columnText(2)};
        dataset.sourceFormat = std::string{rows.columnText(3)};
        dataset.sourceCrs = std::string{rows.columnText(4)};
        dataset.rasterWidth = rows.columnInt64(5);
        dataset.rasterHeight = rows.columnInt64(6);
        dataset.originX = rows.columnDouble(7);
        dataset.originY = rows.columnDouble(8);
        dataset.cellSizeX = rows.columnDouble(9);
        dataset.cellSizeY = rows.columnDouble(10);
        dataset.elevationUnit = std::string{rows.columnText(11)};
        dataset.elevationUnitToMetre = rows.columnDouble(12);
        dataset.hasNodata = rows.columnInt64(13) != 0;
        dataset.nodataValue = rows.columnDouble(14);
        dataset.minZ = rows.columnDouble(15);
        dataset.maxZ = rows.columnDouble(16);
        dataset.bounds = domain::world::SpatialBounds::ofEdges(
            rows.columnDouble(18) /* west */, rows.columnDouble(20) /* south */,
            rows.columnDouble(17) /* east */, rows.columnDouble(19) /* north */);
        dataset.sourceSha256 = std::string{rows.columnText(21)};
        dataset.sourceBytes = static_cast<std::uint64_t>(rows.columnInt64(22));
        dataset.revision = static_cast<std::uint64_t>(rows.columnInt64(23));
        dataset.createdAt = std::string{rows.columnText(25)};
        dataset.modifiedAt = std::string{rows.columnText(26)};
        dataset.sourceAttribution = std::string{rows.columnText(27)};
        dataset.horizontalUnitName = std::string{rows.columnText(28)};
        dataset.horizontalUnitSymbol = std::string{rows.columnText(29)};
        dataset.horizontalUnitIsAngular = rows.columnInt64(30) != 0;
        dataset.elevationUnitSource = std::string{rows.columnText(31)};
        dataset.sampleScale = rows.columnDouble(32);
        dataset.sampleOffset = rows.columnDouble(33);

        // Stored diagnostics are a JSON array of {code, message}; unknown
        // code text means a corrupt row, not an ignorable warning.
        const auto parsed = nlohmann::json::parse(std::string{rows.columnText(24)}, nullptr, false);
        if (parsed.is_discarded() || !parsed.is_array()) {
            fail(ports::StoreErrorCategory::PersistenceFailure,
                "terrain dataset diagnostics are not valid JSON (corrupt project database)");
        }
        for (const auto& entry : parsed) {
            if (!entry.is_object() || !entry.contains("code") || !entry.contains("message")
                || !entry["code"].is_string() || !entry["message"].is_string()) {
                fail(ports::StoreErrorCategory::PersistenceFailure,
                    "terrain dataset diagnostic entry is malformed (corrupt project database)");
            }
            const auto code = domain::terrain::terrainErrorCodeFromName(entry["code"].get<std::string>());
            if (!code.has_value()) {
                fail(ports::StoreErrorCategory::PersistenceFailure,
                    "terrain dataset diagnostic code is not recognized (corrupt project database)");
            }
            dataset.diagnostics.push_back(
                {*code, entry["message"].get<std::string>()});
        }

        if (const auto validationError = domain::terrain::validateTerrainDataset(dataset);
            validationError.has_value()) {
            fail(ports::StoreErrorCategory::PersistenceFailure,
                "persisted terrain dataset failed validation: " + *validationError
                    + " (corrupt project database)");
        }

        // BLOCKER 6: Load canonical coverage pieces for sparse/disconnected
        // terrain. If the table has no rows for this dataset, coveragePieces
        // stays empty and the enclosing bounds is used (legacy behavior for
        // projects created before migration 5).
        {
            SqliteStatement pieces{*connection_,
                "SELECT min_easting, min_northing, max_easting, max_northing "
                "FROM terrain_dataset_coverage WHERE dataset_id = ? "
                "ORDER BY piece_index"};
            pieces.bindText(1, domain::terrain::uuidTextFromEntityId(dataset.id));
            while (pieces.step()) {
                dataset.coveragePieces.push_back(domain::world::SpatialBounds::ofEdges(
                    pieces.columnDouble(0), pieces.columnDouble(1),
                    pieces.columnDouble(2), pieces.columnDouble(3)));
            }
        }

        datasets.push_back(std::move(dataset));
    }
    return datasets;
}

ports::TerrainDatasetInsertResult SqliteProjectStore::insertTerrainDatasetImpl(
    const domain::terrain::TerrainDataset& dataset) {
    if (!connection_.has_value()) {
        throw std::logic_error("cannot insert a terrain dataset without an open project session");
    }
    if (const auto validationError = domain::terrain::validateTerrainDataset(dataset);
        validationError.has_value()) {
        fail(ports::StoreErrorCategory::PersistenceFailure,
            "terrain dataset failed validation: " + *validationError);
    }

    // Diagnostics serialize as a JSON array with stable code names.
    nlohmann::json diagnosticsJson = nlohmann::json::array();
    for (const domain::terrain::TerrainDiagnostic& diagnostic : dataset.diagnostics) {
        diagnosticsJson.push_back({
            {"code", std::string{domain::terrain::terrainErrorCodeName(diagnostic.code)}},
            {"message", diagnostic.message},
        });
    }

    const std::string modifiedAt = runtime::utcTimestampNow();
    {
        SqliteTransaction transaction{*connection_};

        SqliteStatement insert{*connection_,
            "INSERT INTO terrain_datasets "
            "(id, display_name, storage_path, source_format, source_crs, raster_width, raster_height, "
            "origin_x, origin_y, cell_size_x, cell_size_y, elevation_unit, elevation_unit_to_metre, "
            "has_nodata, nodata_value, min_z, max_z, bounds_east, bounds_west, bounds_north, bounds_south, "
            "source_sha256, source_bytes, revision, diagnostics, created_at, modified_at, source_attribution, "
            "horizontal_unit_name, horizontal_unit_symbol, horizontal_unit_is_angular, elevation_unit_source, sample_scale, sample_offset) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"};
        insert.bindText(1, domain::terrain::uuidTextFromEntityId(dataset.id));
        insert.bindText(2, dataset.displayName);
        insert.bindText(3, dataset.storagePath);
        insert.bindText(4, dataset.sourceFormat);
        insert.bindText(5, dataset.sourceCrs);
        insert.bindInt64(6, dataset.rasterWidth);
        insert.bindInt64(7, dataset.rasterHeight);
        insert.bindDouble(8, dataset.originX);
        insert.bindDouble(9, dataset.originY);
        insert.bindDouble(10, dataset.cellSizeX);
        insert.bindDouble(11, dataset.cellSizeY);
        insert.bindText(12, dataset.elevationUnit);
        insert.bindDouble(13, dataset.elevationUnitToMetre);
        insert.bindInt64(14, dataset.hasNodata ? 1 : 0);
        insert.bindDouble(15, dataset.nodataValue);
        insert.bindDouble(16, dataset.minZ);
        insert.bindDouble(17, dataset.maxZ);
        insert.bindDouble(18, dataset.bounds.maxEasting);
        insert.bindDouble(19, dataset.bounds.minEasting);
        insert.bindDouble(20, dataset.bounds.maxNorthing);
        insert.bindDouble(21, dataset.bounds.minNorthing);
        insert.bindText(22, dataset.sourceSha256);
        insert.bindInt64(23, static_cast<std::int64_t>(dataset.sourceBytes));
        insert.bindInt64(24, static_cast<std::int64_t>(dataset.revision));
        insert.bindText(25, diagnosticsJson.dump());
        insert.bindText(26, dataset.createdAt);
        insert.bindText(27, dataset.modifiedAt);
        insert.bindText(28, dataset.sourceAttribution);
        insert.bindText(29, dataset.horizontalUnitName);
        insert.bindText(30, dataset.horizontalUnitSymbol);
        insert.bindInt64(31, dataset.horizontalUnitIsAngular ? 1 : 0);
        insert.bindText(32, dataset.elevationUnitSource);
        insert.bindDouble(33, dataset.sampleScale);
        insert.bindDouble(34, dataset.sampleOffset);
        (void)insert.step();

        // BLOCKER 6: Persist canonical coverage pieces so sparse/disconnected
        // terrain coverage survives save/reopen. Each piece is a project-global
        // rectangle. Local imports have one piece; remote sparse imports have
        // one piece per selected application tile.
        if (!dataset.coveragePieces.empty()) {
            for (std::size_t i = 0; i < dataset.coveragePieces.size(); ++i) {
                const auto& piece = dataset.coveragePieces[i];
                SqliteStatement pieceStmt{*connection_,
                    "INSERT INTO terrain_dataset_coverage "
                    "(dataset_id, piece_index, min_easting, min_northing, max_easting, max_northing) "
                    "VALUES (?, ?, ?, ?, ?, ?)"};
                pieceStmt.bindText(1, domain::terrain::uuidTextFromEntityId(dataset.id));
                pieceStmt.bindInt64(2, static_cast<std::int64_t>(i));
                pieceStmt.bindDouble(3, piece.minEasting);
                pieceStmt.bindDouble(4, piece.minNorthing);
                pieceStmt.bindDouble(5, piece.maxEasting);
                pieceStmt.bindDouble(6, piece.maxNorthing);
                (void)pieceStmt.step();
            }
        }

        SqliteStatement state{*connection_,
            "UPDATE project_state SET revision = revision + 1, modified_at = ? WHERE id = 1"};
        state.bindText(1, modifiedAt);
        (void)state.step();
        if (connection_->lastChanges() != 1) {
            fail(ports::StoreErrorCategory::PersistenceFailure,
                "project_state row went missing during terrain dataset insert (corrupt project database)");
        }
        transaction.commit();
    }

    record_.revision += 1;
    record_.modifiedAt = modifiedAt;
    return {.record = record_, .dataset = dataset};
}

// ---- Road persistence ----

std::vector<domain::road::RoadRecord> SqliteProjectStore::roadsImpl() const {
    if (!connection_.has_value()) {
        throw std::logic_error("cannot read roads without an open project session");
    }
    std::vector<domain::road::RoadRecord> roads;
    SqliteStatement roadRows{*connection_,
        "SELECT id, display_name, created_at, modified_at, position_tolerance, max_curvature, "
        "anchor_boundary_segments "
        "FROM roads ORDER BY created_at"};
    while (roadRows.step()) {
        domain::road::RoadRecord road;
        road.id = domain::road::roadIdFromUuidText(roadRows.columnText(0));
        road.displayName = std::string{roadRows.columnText(1)};
        road.createdAt = std::string{roadRows.columnText(2)};
        road.modifiedAt = std::string{roadRows.columnText(3)};
        // Blocker 7: load the persisted fitting contract.
        road.positionTolerance = roadRows.columnDouble(4);
        if (!roadRows.columnIsNull(5)) {
            road.maxCurvature = roadRows.columnDouble(5);
        }
        const auto boundaryJson = nlohmann::json::parse(roadRows.columnText(6));
        if (!boundaryJson.is_array()) {
            fail(ports::StoreErrorCategory::PersistenceFailure,
                "roads.anchor_boundary_segments must be a JSON array");
        }
        for (const auto& value : boundaryJson) {
            if (!value.is_number_unsigned()) {
                fail(ports::StoreErrorCategory::PersistenceFailure,
                    "roads.anchor_boundary_segments entries must be unsigned integers");
            }
            road.anchorBoundarySegments.insert(value.get<std::size_t>());
        }

        // Segments.
        SqliteStatement segRows{*connection_,
            "SELECT segment_index, segment_kind, start_easting, start_northing, start_heading, "
            "length, curvature, start_curvature, end_curvature "
            "FROM road_segments WHERE road_id = ? ORDER BY segment_index"};
        segRows.bindText(1, roadRows.columnText(0));
        while (segRows.step()) {
            domain::road::RoadSegmentRecord sr;
            sr.segmentIndex = static_cast<std::uint64_t>(segRows.columnInt64(0));
            const auto kind = domain::road::alignmentSegmentKindFromName(segRows.columnText(1));
            if (!kind.has_value()) {
                fail(ports::StoreErrorCategory::PersistenceFailure,
                    "road_segments.segment_kind is not a recognized enum value for road " + std::string{roadRows.columnText(0)});
            }
            sr.kind = *kind;
            sr.start.easting = segRows.columnDouble(2);
            sr.start.northing = segRows.columnDouble(3);
            sr.startHeading = segRows.columnDouble(4);
            sr.length = segRows.columnDouble(5);
            sr.curvature = segRows.columnDouble(6);
            sr.startCurvature = segRows.columnDouble(7);
            sr.endCurvature = segRows.columnDouble(8);
            road.segments.push_back(sr);
        }

        // Elevation breakpoints.
        SqliteStatement elevRows{*connection_,
            "SELECT station, value FROM road_elevation_breakpoints "
            "WHERE road_id = ? ORDER BY breakpoint_index"};
        elevRows.bindText(1, roadRows.columnText(0));
        while (elevRows.step()) {
            road.elevationBreakpoints.push_back(
                {elevRows.columnDouble(0), elevRows.columnDouble(1)});
        }

        // Superelevation breakpoints.
        SqliteStatement supRows{*connection_,
            "SELECT station, value FROM road_superelevation_breakpoints "
            "WHERE road_id = ? ORDER BY breakpoint_index"};
        supRows.bindText(1, roadRows.columnText(0));
        while (supRows.step()) {
            road.superelevationBreakpoints.push_back(
                {supRows.columnDouble(0), supRows.columnDouble(1)});
        }

        // Source.
        SqliteStatement srcRows{*connection_,
            "SELECT provider, source_id, source_crs, imported_at, tags "
            "FROM road_source WHERE road_id = ?"};
        srcRows.bindText(1, roadRows.columnText(0));
        if (srcRows.step()) {
            road.hasSource = true;
            const auto provider = domain::road::sourceProviderFromName(srcRows.columnText(0));
            if (!provider.has_value()) {
                fail(ports::StoreErrorCategory::PersistenceFailure,
                    "road_source.provider is not a recognized enum value for road " + std::string{roadRows.columnText(0)});
            }
            road.provider = *provider;
            road.sourceId = std::string{srcRows.columnText(1)};
            road.sourceCrs = std::string{srcRows.columnText(2)};
            road.importedAt = std::string{srcRows.columnText(3)};
            // Tags are stored as JSON array of {"key":..,"value":..} objects.
            const auto tagsJson = nlohmann::json::parse(srcRows.columnText(4));
            for (const auto& tag : tagsJson) {
                road.sourceTags.push_back({tag["key"], tag["value"]});
            }

            // Source vertices.
            SqliteStatement vtxRows{*connection_,
                "SELECT x, y, z FROM road_source_vertices "
                "WHERE road_id = ? ORDER BY vertex_index"};
            vtxRows.bindText(1, roadRows.columnText(0));
            std::uint64_t vIdx = 0;
            while (vtxRows.step()) {
                domain::road::RoadSourceVertexRecord v;
                v.index = vIdx++;
                v.x = vtxRows.columnDouble(0);
                v.y = vtxRows.columnDouble(1);
                if (!vtxRows.columnIsNull(2)) {
                    v.z = vtxRows.columnDouble(2);
                }
                road.sourceVertices.push_back(v);
            }
        }

        // Control vertices (editable geometry; separate from immutable
        // source evidence). If the table has no rows for this road (e.g.
        // an older project before migration 9), fall back to source
        // vertices so the road remains editable.
        {
            SqliteStatement ctrlRows{*connection_,
                "SELECT x, y, z FROM road_control_vertices "
                "WHERE road_id = ? ORDER BY vertex_index"};
            ctrlRows.bindText(1, roadRows.columnText(0));
            std::uint64_t cIdx = 0;
            while (ctrlRows.step()) {
                domain::road::RoadSourceVertexRecord v;
                v.index = cIdx++;
                v.x = ctrlRows.columnDouble(0);
                v.y = ctrlRows.columnDouble(1);
                if (!ctrlRows.columnIsNull(2)) {
                    v.z = ctrlRows.columnDouble(2);
                }
                road.controlVertices.push_back(v);
            }
            if (road.controlVertices.empty() && !road.sourceVertices.empty()) {
                road.controlVertices = road.sourceVertices;
            }
        }

        // Protected anchors.
        SqliteStatement anchorRows{*connection_,
            "SELECT station, easting, northing, kind "
            "FROM road_protected_anchors WHERE road_id = ? ORDER BY anchor_index"};
        anchorRows.bindText(1, roadRows.columnText(0));
        std::uint64_t aIdx = 0;
        while (anchorRows.step()) {
            const auto anchorKind = domain::road::anchorKindFromName(anchorRows.columnText(3));
            if (!anchorKind.has_value()) {
                fail(ports::StoreErrorCategory::PersistenceFailure,
                    "road_protected_anchors.kind is not a recognized enum value for road " + std::string{roadRows.columnText(0)});
            }
            road.protectedAnchors.push_back({aIdx++,
                anchorRows.columnDouble(0),
                {anchorRows.columnDouble(1), anchorRows.columnDouble(2)},
                *anchorKind});
        }

        roads.push_back(std::move(road));
    }
    return roads;
}

domain::road::RoadRecord SqliteProjectStore::insertRoadImpl(
    const domain::road::RoadRecord& road) {
    if (!connection_.has_value()) {
        throw std::logic_error("cannot insert a road without an open project session");
    }
    const std::string modifiedAt = runtime::utcTimestampNow();
    const std::string roadIdText = domain::road::uuidTextFromRoadId(road.id);
    {
        SqliteTransaction transaction{*connection_};

        SqliteStatement insertRoad{*connection_,
            "INSERT INTO roads (id, display_name, created_at, modified_at, "
            "position_tolerance, max_curvature, anchor_boundary_segments) "
            "VALUES (?, ?, ?, ?, ?, ?, ?)"};
        insertRoad.bindText(1, roadIdText);
        insertRoad.bindText(2, road.displayName);
        insertRoad.bindText(3, road.createdAt.empty() ? modifiedAt : road.createdAt);
        insertRoad.bindText(4, modifiedAt);
        // Blocker 7: persist the fitting contract.
        insertRoad.bindDouble(5, road.positionTolerance);
        if (road.maxCurvature.has_value()) {
            insertRoad.bindDouble(6, *road.maxCurvature);
        } else {
            insertRoad.bindNull(6);
        }
        insertRoad.bindText(7, nlohmann::json(road.anchorBoundarySegments).dump());
        (void)insertRoad.step();

        // Segments.
        for (const auto& sr : road.segments) {
            SqliteStatement insertSeg{*connection_,
                "INSERT INTO road_segments "
                "(road_id, segment_index, segment_kind, start_easting, start_northing, "
                "start_heading, length, curvature, start_curvature, end_curvature) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"};
            insertSeg.bindText(1, roadIdText);
            insertSeg.bindInt64(2, static_cast<std::int64_t>(sr.segmentIndex));
            insertSeg.bindText(3, domain::road::alignmentSegmentKindName(sr.kind));
            insertSeg.bindDouble(4, sr.start.easting);
            insertSeg.bindDouble(5, sr.start.northing);
            insertSeg.bindDouble(6, sr.startHeading);
            insertSeg.bindDouble(7, sr.length);
            insertSeg.bindDouble(8, sr.curvature);
            insertSeg.bindDouble(9, sr.startCurvature);
            insertSeg.bindDouble(10, sr.endCurvature);
            (void)insertSeg.step();
        }

        // Elevation breakpoints.
        for (std::size_t i = 0; i < road.elevationBreakpoints.size(); ++i) {
            const auto& bp = road.elevationBreakpoints[i];
            SqliteStatement insertBp{*connection_,
                "INSERT INTO road_elevation_breakpoints "
                "(road_id, breakpoint_index, station, value) VALUES (?, ?, ?, ?)"};
            insertBp.bindText(1, roadIdText);
            insertBp.bindInt64(2, static_cast<std::int64_t>(i));
            insertBp.bindDouble(3, bp.station);
            insertBp.bindDouble(4, bp.value);
            (void)insertBp.step();
        }

        // Superelevation breakpoints.
        for (std::size_t i = 0; i < road.superelevationBreakpoints.size(); ++i) {
            const auto& bp = road.superelevationBreakpoints[i];
            SqliteStatement insertBp{*connection_,
                "INSERT INTO road_superelevation_breakpoints "
                "(road_id, breakpoint_index, station, value) VALUES (?, ?, ?, ?)"};
            insertBp.bindText(1, roadIdText);
            insertBp.bindInt64(2, static_cast<std::int64_t>(i));
            insertBp.bindDouble(3, bp.station);
            insertBp.bindDouble(4, bp.value);
            (void)insertBp.step();
        }

        // Source.
        if (road.hasSource) {
            nlohmann::json tagsJson = nlohmann::json::array();
            for (const auto& tag : road.sourceTags) {
                tagsJson.push_back({{"key", tag.key}, {"value", tag.value}});
            }
            SqliteStatement insertSrc{*connection_,
                "INSERT INTO road_source "
                "(road_id, provider, source_id, source_crs, imported_at, tags) "
                "VALUES (?, ?, ?, ?, ?, ?)"};
            insertSrc.bindText(1, roadIdText);
            insertSrc.bindText(2, domain::road::sourceProviderName(road.provider));
            insertSrc.bindText(3, road.sourceId);
            insertSrc.bindText(4, road.sourceCrs);
            insertSrc.bindText(5, road.importedAt);
            insertSrc.bindText(6, tagsJson.dump());
            (void)insertSrc.step();

            for (const auto& v : road.sourceVertices) {
                SqliteStatement insertVtx{*connection_,
                    "INSERT INTO road_source_vertices "
                    "(road_id, vertex_index, x, y, z) VALUES (?, ?, ?, ?, ?)"};
                insertVtx.bindText(1, roadIdText);
                insertVtx.bindInt64(2, static_cast<std::int64_t>(v.index));
                insertVtx.bindDouble(3, v.x);
                insertVtx.bindDouble(4, v.y);
                if (v.z.has_value()) {
                    insertVtx.bindDouble(5, *v.z);
                } else {
                    insertVtx.bindNull(5);
                }
                (void)insertVtx.step();
            }
        }

        // Control vertices (editable geometry; separate from immutable
        // source evidence). Always inserted for every road.
        for (const auto& v : road.controlVertices) {
            SqliteStatement insertCtrl{*connection_,
                "INSERT INTO road_control_vertices "
                "(road_id, vertex_index, x, y, z) VALUES (?, ?, ?, ?, ?)"};
            insertCtrl.bindText(1, roadIdText);
            insertCtrl.bindInt64(2, static_cast<std::int64_t>(v.index));
            insertCtrl.bindDouble(3, v.x);
            insertCtrl.bindDouble(4, v.y);
            if (v.z.has_value()) {
                insertCtrl.bindDouble(5, *v.z);
            } else {
                insertCtrl.bindNull(5);
            }
            (void)insertCtrl.step();
        }

        // Protected anchors.
        for (const auto& a : road.protectedAnchors) {
            SqliteStatement insertAnchor{*connection_,
                "INSERT INTO road_protected_anchors "
                "(road_id, anchor_index, station, easting, northing, kind) "
                "VALUES (?, ?, ?, ?, ?, ?)"};
            insertAnchor.bindText(1, roadIdText);
            insertAnchor.bindInt64(2, static_cast<std::int64_t>(a.index));
            insertAnchor.bindDouble(3, a.station);
            insertAnchor.bindDouble(4, a.position.easting);
            insertAnchor.bindDouble(5, a.position.northing);
            insertAnchor.bindText(6, domain::road::anchorKindName(a.kind));
            (void)insertAnchor.step();
        }

        SqliteStatement state{*connection_,
            "UPDATE project_state SET revision = revision + 1, modified_at = ? WHERE id = 1"};
        state.bindText(1, modifiedAt);
        (void)state.step();
        if (connection_->lastChanges() != 1) {
            fail(ports::StoreErrorCategory::PersistenceFailure,
                "project_state row went missing during road insert (corrupt project database)");
        }
        transaction.commit();
    }

    record_.revision += 1;
    record_.modifiedAt = modifiedAt;
    auto result = road;
    result.modifiedAt = modifiedAt;
    return result;
}

domain::road::RoadRecord SqliteProjectStore::updateRoadImpl(
    const domain::road::RoadRecord& road) {
    if (!connection_.has_value()) {
        throw std::logic_error("cannot update a road without an open project session");
    }
    const std::string modifiedAt = runtime::utcTimestampNow();
    const std::string roadIdText = domain::road::uuidTextFromRoadId(road.id);
    {
        SqliteTransaction transaction{*connection_};

        // Delete all existing road data, then re-insert the full record.
        // This is a full canonical replacement within one transaction.
        SqliteStatement delAnchors{*connection_,
            "DELETE FROM road_protected_anchors WHERE road_id = ?"};
        delAnchors.bindText(1, roadIdText);
        (void)delAnchors.step();

        SqliteStatement delSrcVtx{*connection_,
            "DELETE FROM road_source_vertices WHERE road_id = ?"};
        delSrcVtx.bindText(1, roadIdText);
        (void)delSrcVtx.step();

        SqliteStatement delCtrlVtx{*connection_,
            "DELETE FROM road_control_vertices WHERE road_id = ?"};
        delCtrlVtx.bindText(1, roadIdText);
        (void)delCtrlVtx.step();

        SqliteStatement delSrc{*connection_,
            "DELETE FROM road_source WHERE road_id = ?"};
        delSrc.bindText(1, roadIdText);
        (void)delSrc.step();

        SqliteStatement delSup{*connection_,
            "DELETE FROM road_superelevation_breakpoints WHERE road_id = ?"};
        delSup.bindText(1, roadIdText);
        (void)delSup.step();

        SqliteStatement delElev{*connection_,
            "DELETE FROM road_elevation_breakpoints WHERE road_id = ?"};
        delElev.bindText(1, roadIdText);
        (void)delElev.step();

        SqliteStatement delSeg{*connection_,
            "DELETE FROM road_segments WHERE road_id = ?"};
        delSeg.bindText(1, roadIdText);
        (void)delSeg.step();

        // Update the road row itself.
        SqliteStatement updateRoadRow{*connection_,
            "UPDATE roads SET display_name = ?, modified_at = ?, "
            "position_tolerance = ?, max_curvature = ?, "
            "anchor_boundary_segments = ? WHERE id = ?"};
        updateRoadRow.bindText(1, road.displayName);
        updateRoadRow.bindText(2, modifiedAt);
        // Blocker 7: persist the fitting contract on update.
        updateRoadRow.bindDouble(3, road.positionTolerance);
        if (road.maxCurvature.has_value()) {
            updateRoadRow.bindDouble(4, *road.maxCurvature);
        } else {
            updateRoadRow.bindNull(4);
        }
        updateRoadRow.bindText(5, nlohmann::json(road.anchorBoundarySegments).dump());
        updateRoadRow.bindText(6, roadIdText);
        (void)updateRoadRow.step();
        if (connection_->lastChanges() != 1) {
            fail(ports::StoreErrorCategory::NotFound,
                "road not found during update: " + roadIdText);
        }

        // Re-insert segments.
        for (const auto& sr : road.segments) {
            SqliteStatement insertSeg{*connection_,
                "INSERT INTO road_segments "
                "(road_id, segment_index, segment_kind, start_easting, start_northing, "
                "start_heading, length, curvature, start_curvature, end_curvature) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"};
            insertSeg.bindText(1, roadIdText);
            insertSeg.bindInt64(2, static_cast<std::int64_t>(sr.segmentIndex));
            insertSeg.bindText(3, domain::road::alignmentSegmentKindName(sr.kind));
            insertSeg.bindDouble(4, sr.start.easting);
            insertSeg.bindDouble(5, sr.start.northing);
            insertSeg.bindDouble(6, sr.startHeading);
            insertSeg.bindDouble(7, sr.length);
            insertSeg.bindDouble(8, sr.curvature);
            insertSeg.bindDouble(9, sr.startCurvature);
            insertSeg.bindDouble(10, sr.endCurvature);
            (void)insertSeg.step();
        }

        // Re-insert elevation breakpoints.
        for (std::size_t i = 0; i < road.elevationBreakpoints.size(); ++i) {
            const auto& bp = road.elevationBreakpoints[i];
            SqliteStatement insertBp{*connection_,
                "INSERT INTO road_elevation_breakpoints "
                "(road_id, breakpoint_index, station, value) VALUES (?, ?, ?, ?)"};
            insertBp.bindText(1, roadIdText);
            insertBp.bindInt64(2, static_cast<std::int64_t>(i));
            insertBp.bindDouble(3, bp.station);
            insertBp.bindDouble(4, bp.value);
            (void)insertBp.step();
        }

        // Re-insert superelevation breakpoints.
        for (std::size_t i = 0; i < road.superelevationBreakpoints.size(); ++i) {
            const auto& bp = road.superelevationBreakpoints[i];
            SqliteStatement insertBp{*connection_,
                "INSERT INTO road_superelevation_breakpoints "
                "(road_id, breakpoint_index, station, value) VALUES (?, ?, ?, ?)"};
            insertBp.bindText(1, roadIdText);
            insertBp.bindInt64(2, static_cast<std::int64_t>(i));
            insertBp.bindDouble(3, bp.station);
            insertBp.bindDouble(4, bp.value);
            (void)insertBp.step();
        }

        // Re-insert source.
        if (road.hasSource) {
            nlohmann::json tagsJson = nlohmann::json::array();
            for (const auto& tag : road.sourceTags) {
                tagsJson.push_back({{"key", tag.key}, {"value", tag.value}});
            }
            SqliteStatement insertSrc{*connection_,
                "INSERT INTO road_source "
                "(road_id, provider, source_id, source_crs, imported_at, tags) "
                "VALUES (?, ?, ?, ?, ?, ?)"};
            insertSrc.bindText(1, roadIdText);
            insertSrc.bindText(2, domain::road::sourceProviderName(road.provider));
            insertSrc.bindText(3, road.sourceId);
            insertSrc.bindText(4, road.sourceCrs);
            insertSrc.bindText(5, road.importedAt);
            insertSrc.bindText(6, tagsJson.dump());
            (void)insertSrc.step();

            for (const auto& v : road.sourceVertices) {
                SqliteStatement insertVtx{*connection_,
                    "INSERT INTO road_source_vertices "
                    "(road_id, vertex_index, x, y, z) VALUES (?, ?, ?, ?, ?)"};
                insertVtx.bindText(1, roadIdText);
                insertVtx.bindInt64(2, static_cast<std::int64_t>(v.index));
                insertVtx.bindDouble(3, v.x);
                insertVtx.bindDouble(4, v.y);
                if (v.z.has_value()) {
                    insertVtx.bindDouble(5, *v.z);
                } else {
                    insertVtx.bindNull(5);
                }
                (void)insertVtx.step();
            }
        }

        // Re-insert control vertices (editable geometry; separate from
        // immutable source evidence).
        for (const auto& v : road.controlVertices) {
            SqliteStatement insertCtrl{*connection_,
                "INSERT INTO road_control_vertices "
                "(road_id, vertex_index, x, y, z) VALUES (?, ?, ?, ?, ?)"};
            insertCtrl.bindText(1, roadIdText);
            insertCtrl.bindInt64(2, static_cast<std::int64_t>(v.index));
            insertCtrl.bindDouble(3, v.x);
            insertCtrl.bindDouble(4, v.y);
            if (v.z.has_value()) {
                insertCtrl.bindDouble(5, *v.z);
            } else {
                insertCtrl.bindNull(5);
            }
            (void)insertCtrl.step();
        }

        // Re-insert protected anchors.
        for (const auto& a : road.protectedAnchors) {
            SqliteStatement insertAnchor{*connection_,
                "INSERT INTO road_protected_anchors "
                "(road_id, anchor_index, station, easting, northing, kind) "
                "VALUES (?, ?, ?, ?, ?, ?)"};
            insertAnchor.bindText(1, roadIdText);
            insertAnchor.bindInt64(2, static_cast<std::int64_t>(a.index));
            insertAnchor.bindDouble(3, a.station);
            insertAnchor.bindDouble(4, a.position.easting);
            insertAnchor.bindDouble(5, a.position.northing);
            insertAnchor.bindText(6, domain::road::anchorKindName(a.kind));
            (void)insertAnchor.step();
        }

        SqliteStatement state{*connection_,
            "UPDATE project_state SET revision = revision + 1, modified_at = ? WHERE id = 1"};
        state.bindText(1, modifiedAt);
        (void)state.step();
        if (connection_->lastChanges() != 1) {
            fail(ports::StoreErrorCategory::PersistenceFailure,
                "project_state row went missing during road update (corrupt project database)");
        }
        transaction.commit();
    }

    record_.revision += 1;
    record_.modifiedAt = modifiedAt;
    auto result = road;
    result.modifiedAt = modifiedAt;
    return result;
}

void SqliteProjectStore::removeRoadImpl(const std::string& roadId) {
    if (!connection_.has_value()) {
        throw std::logic_error("cannot remove a road without an open project session");
    }
    const std::string modifiedAt = runtime::utcTimestampNow();
    {
        SqliteTransaction transaction{*connection_};
        SqliteStatement remove{*connection_, "DELETE FROM roads WHERE id = ?"};
        remove.bindText(1, roadId);
        (void)remove.step();
        if (connection_->lastChanges() != 1) {
            fail(ports::StoreErrorCategory::NotFound,
                "road row not found for removal: " + roadId);
        }
        SqliteStatement state{*connection_,
            "UPDATE project_state SET revision = revision + 1, modified_at = ? WHERE id = 1"};
        state.bindText(1, modifiedAt);
        (void)state.step();
        if (connection_->lastChanges() != 1) {
            fail(ports::StoreErrorCategory::PersistenceFailure,
                "project_state row went missing during road remove (corrupt project database)");
        }
        transaction.commit();
    }
    record_.revision += 1;
    record_.modifiedAt = modifiedAt;
}

ProjectRecord SqliteProjectStore::readRecord(const SqliteConnection& connection) const {
    ProjectRecord record;

    SqliteStatement state{connection,
        "SELECT project_uuid, display_name, traffic_side, revision, saved_revision, created_at, modified_at "
        "FROM project_state WHERE id = 1"};
    if (!state.step()) {
        fail(ports::StoreErrorCategory::PersistenceFailure,
            "project_state row is missing (corrupt project database)");
    }
    record.uuid = std::string{state.columnText(0)};
    record.displayName = std::string{state.columnText(1)};
    const auto trafficSide = domain::project::trafficSideFromName(state.columnText(2));
    if (!trafficSide.has_value()) {
        fail(ports::StoreErrorCategory::PersistenceFailure,
            "project_state traffic_side value is not recognized (corrupt project database)");
    }
    record.trafficSide = *trafficSide;
    record.revision = static_cast<std::uint64_t>(state.columnInt64(3));
    record.savedRevision = static_cast<std::uint64_t>(state.columnInt64(4));
    record.createdAt = std::string{state.columnText(5)};
    record.modifiedAt = std::string{state.columnText(6)};

    SqliteStatement georeference{connection,
        georeferenceHasOriginHeight()
            ? "SELECT horizontal_crs, linear_unit, axis_convention, origin_easting, origin_northing, origin_height, vertical_crs "
              "FROM georeference WHERE id = 1"
            : "SELECT horizontal_crs, linear_unit, axis_convention, origin_easting, origin_northing, vertical_crs "
              "FROM georeference WHERE id = 1"};
    if (!georeference.step()) {
        fail(ports::StoreErrorCategory::PersistenceFailure,
            "georeference row is missing (corrupt project database)");
    }
    record.georeference.horizontalCrs = std::string{georeference.columnText(0)};
    record.georeference.linearUnit = std::string{georeference.columnText(1)};
    const auto axisConvention = domain::geo::axisConventionFromName(georeference.columnText(2));
    if (!axisConvention.has_value()) {
        fail(ports::StoreErrorCategory::PersistenceFailure,
            "georeference axis_convention value is not recognized (corrupt project database)");
    }
    record.georeference.axisConvention = *axisConvention;
    record.georeference.originEasting = georeference.columnDouble(3);
    record.georeference.originNorthing = georeference.columnDouble(4);
    if (georeferenceHasOriginHeight()) {
        record.georeference.originHeight = georeference.columnDouble(5);
        record.georeference.verticalCrs = std::string{georeference.columnText(6)};
    } else {
        record.georeference.originHeight = 0.0;
        record.georeference.verticalCrs = std::string{georeference.columnText(5)};
    }

    if (record.revision < 1 || record.savedRevision > record.revision) {
        fail(ports::StoreErrorCategory::PersistenceFailure,
            "project_state revision counters are inconsistent (corrupt project database)");
    }
    return record;
}

} // namespace infraforge::persistence
