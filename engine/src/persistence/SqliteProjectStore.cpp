#include "infraforge/persistence/SqliteProjectStore.hpp"

#include "infraforge/persistence/ProjectManifest.hpp"
#include "infraforge/persistence/SchemaMigrations.hpp"
#include "infraforge/runtime/FileSystemUtf8.hpp"
#include "infraforge/runtime/Logging.hpp"
#include "infraforge/runtime/Timestamp.hpp"
#include "infraforge/runtime/Uuid.hpp"
#include "infraforge/version.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
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

ProjectManifest manifestFromRecord(
    const ProjectRecord& record,
    const std::int64_t schemaVersion,
    const std::string& createdAt,
    const std::string& modifiedAt) {
    ProjectManifest manifest;
    manifest.projectUuid = record.uuid;
    manifest.displayName = record.displayName;
    manifest.createdAt = createdAt;
    manifest.modifiedAt = modifiedAt;
    manifest.databasePath = std::string{kDatabaseFileName};
    manifest.projectSchemaVersion = static_cast<int>(schemaVersion);
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

void insertProjectRows(SqliteConnection& connection, const ProjectRecord& record) {
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
        "INSERT INTO georeference "
        "(id, horizontal_crs, linear_unit, axis_convention, origin_easting, origin_northing, vertical_crs) "
        "VALUES (1, ?, ?, ?, ?, ?, ?)"};
    georeference.bindText(1, record.georeference.horizontalCrs);
    georeference.bindText(2, record.georeference.linearUnit);
    georeference.bindText(3, domain::project::axisConventionName(record.georeference.axisConvention));
    georeference.bindDouble(4, record.georeference.originEasting);
    georeference.bindDouble(5, record.georeference.originNorthing);
    georeference.bindText(6, record.georeference.verticalCrs);
    (void)georeference.step();

    transaction.commit();
}

} // namespace

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

        writeProjectManifest(directory, manifestFromRecord(record, kLatestSchemaVersion, record.createdAt, record.modifiedAt));

        auto connection = SqliteConnection::open(directory / kDatabaseFileName, SqliteOpenMode::ReadWriteCreate);
        applyPendingMigrations(connection);
        insertProjectRows(connection, record);

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

ProjectRecord SqliteProjectStore::open(const std::filesystem::path& projectDirectory) {
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
    // newer schema is rejected without modifying the project.
    {
        auto probe = SqliteConnection::open(databaseFile, SqliteOpenMode::ReadOnly);
        const std::int64_t appliedVersion = readAppliedSchemaVersion(probe);
        if (appliedVersion > kLatestSchemaVersion) {
            fail(ports::StoreErrorCategory::SchemaUnsupported,
                "project database schema version " + std::to_string(appliedVersion)
                    + " is newer than the supported version " + std::to_string(kLatestSchemaVersion)
                    + "; the project was left unmodified");
        }
        if (appliedVersion != manifest.projectSchemaVersion) {
            fail(ports::StoreErrorCategory::PersistenceFailure,
                "project manifest schema version (" + std::to_string(manifest.projectSchemaVersion)
                    + ") does not match the database schema version (" + std::to_string(appliedVersion) + ")");
        }
    }

    auto connection = SqliteConnection::open(databaseFile, SqliteOpenMode::ReadWrite);
    applyPendingMigrations(connection);

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
        fail(ports::StoreErrorCategory::PersistenceFailure,
            "georeference mismatch between manifest and database (corrupt project)");
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

ProjectRecord SqliteProjectStore::save() {
    if (!connection_.has_value()) {
        throw std::logic_error("cannot save without an open project session");
    }
    if (!record_.isDirty()) {
        return record_;
    }

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

    writeProjectManifest(directory_, manifestFromRecord(record_, kLatestSchemaVersion, record_.createdAt, modifiedAt));

    record_.savedRevision = record_.revision;
    record_.modifiedAt = modifiedAt;
    return record_;
}

ProjectRecord SqliteProjectStore::saveAs(const domain::project::SaveAsSpec& spec) {
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

        writeProjectManifest(targetDirectory, manifestFromRecord(targetRecord, kLatestSchemaVersion, now, now));

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

void SqliteProjectStore::close() {
    if (!connection_.has_value()) {
        return;
    }
    connection_->close();
    connection_.reset();
    record_ = {};
    directory_.clear();
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
        "SELECT horizontal_crs, linear_unit, axis_convention, origin_easting, origin_northing, vertical_crs "
        "FROM georeference WHERE id = 1"};
    if (!georeference.step()) {
        fail(ports::StoreErrorCategory::PersistenceFailure,
            "georeference row is missing (corrupt project database)");
    }
    record.georeference.horizontalCrs = std::string{georeference.columnText(0)};
    record.georeference.linearUnit = std::string{georeference.columnText(1)};
    const auto axisConvention = domain::project::axisConventionFromName(georeference.columnText(2));
    if (!axisConvention.has_value()) {
        fail(ports::StoreErrorCategory::PersistenceFailure,
            "georeference axis_convention value is not recognized (corrupt project database)");
    }
    record.georeference.axisConvention = *axisConvention;
    record.georeference.originEasting = georeference.columnDouble(3);
    record.georeference.originNorthing = georeference.columnDouble(4);
    record.georeference.verticalCrs = std::string{georeference.columnText(5)};

    if (record.revision < 1 || record.savedRevision > record.revision) {
        fail(ports::StoreErrorCategory::PersistenceFailure,
            "project_state revision counters are inconsistent (corrupt project database)");
    }
    return record;
}

} // namespace infraforge::persistence
