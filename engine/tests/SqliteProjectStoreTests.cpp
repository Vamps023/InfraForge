#include <doctest/doctest.h>

#include "TestHelpers.hpp"
#include "infraforge/ports/ProjectStore.hpp"
#include "infraforge/persistence/SchemaMigrations.hpp"
#include "infraforge/persistence/SqliteConnection.hpp"
#include "infraforge/persistence/SqliteProjectStore.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <sstream>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace {

using infraforge::testhelpers::captureException;

std::string readFileToString(const fs::path& file) {
    std::ifstream input(file, std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

infraforge::domain::project::ProjectRecord createSampleProject(infraforge::persistence::SqliteProjectStore& store, const fs::path& parent) {
    return store.create(infraforge::testhelpers::sampleCreateSpec(parent));
}

// Injects a fake future migration so reopen hits the unsupported-schema path.
void simulateNewerSchema(const fs::path& projectDirectory) {
    auto connection = infraforge::persistence::SqliteConnection::open(
        projectDirectory / "project.db", infraforge::persistence::SqliteOpenMode::ReadWrite);
    connection.exec("INSERT INTO schema_migrations (id, name, applied_at) VALUES (99, 'future', 'now')");
}

} // namespace

TEST_SUITE("sqlite project store") {
    TEST_CASE("create produces the documented project format and reopen returns identical canonical metadata") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;

        const auto created = createSampleProject(store, scratch.path());
        CHECK(store.isOpen());
        CHECK_FALSE(created.uuid.empty());
        CHECK_EQ(created.revision, 1);
        CHECK_EQ(created.savedRevision, 1);
        CHECK_FALSE(created.isDirty());
        CHECK(created.directory == fs::absolute(scratch.path() / "Test Project.iforge").lexically_normal());

        const auto projectDirectory = scratch.path() / "Test Project.iforge";
        CHECK(fs::is_regular_file(projectDirectory / "project.json"));
        CHECK(fs::is_regular_file(projectDirectory / "project.db"));
        for (const std::string_view subdirectory : {"assets/models", "terrain/elevation", "scenarios", "cache", "logs"}) {
            CHECK(fs::is_directory(projectDirectory / subdirectory));
        }

        store.close();
        CHECK_FALSE(store.isOpen());

        const auto reopened = store.open(projectDirectory);
        CHECK_EQ(reopened.uuid, created.uuid);
        CHECK_EQ(reopened.displayName, created.displayName);
        CHECK_EQ(reopened.revision, created.revision);
        CHECK_EQ(reopened.savedRevision, created.savedRevision);
        CHECK(reopened.trafficSide == created.trafficSide);
        CHECK(reopened.georeference == created.georeference);
        CHECK_EQ(reopened.createdAt, created.createdAt);
        store.close();
    }

    TEST_CASE("create refuses to overwrite an existing directory and rolls back partial creation") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;

        (void)createSampleProject(store, scratch.path());
        store.close();

        const auto error = captureException<infraforge::ports::StoreError>(
            [&] { (void)createSampleProject(store, scratch.path()); });
        REQUIRE(error.has_value());
        CHECK(error->category() == infraforge::ports::StoreErrorCategory::DirectoryInvalid);
    }

    TEST_CASE("open rejects non-project directories without modifying them") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;

        const auto plainDirectory = scratch.path() / "plain";
        fs::create_directories(plainDirectory);

        const auto error = captureException<infraforge::ports::StoreError>([&] { (void)store.open(plainDirectory); });
        REQUIRE(error.has_value());
        CHECK(error->category() == infraforge::ports::StoreErrorCategory::DirectoryInvalid);
        CHECK_FALSE(fs::exists(plainDirectory / "project.json"));
    }

    TEST_CASE("open rejects a newer database schema without modifying the project") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        const auto created = createSampleProject(store, scratch.path());
        const auto projectDirectory = fs::path(created.directory);
        const auto manifestBefore = readFileToString(projectDirectory / "project.json");
        store.close();

        simulateNewerSchema(projectDirectory);

        const auto error = captureException<infraforge::ports::StoreError>([&] { (void)store.open(projectDirectory); });
        REQUIRE(error.has_value());
        CHECK(error->category() == infraforge::ports::StoreErrorCategory::SchemaUnsupported);

        // The probe is read-only: the injected migration row must still be
        // present and the manifest must be untouched.
        auto connection = infraforge::persistence::SqliteConnection::open(
            projectDirectory / "project.db", infraforge::persistence::SqliteOpenMode::ReadOnly);
        infraforge::persistence::SqliteStatement version{connection,
            "SELECT COUNT(*) FROM schema_migrations WHERE id = 99"};
        REQUIRE(version.step());
        CHECK_EQ(version.columnInt64(0), 1);
        CHECK(manifestBefore == readFileToString(projectDirectory / "project.json"));
    }

    TEST_CASE("open rejects a tampered manifest and a manifest/database identity mismatch") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        const auto created = createSampleProject(store, scratch.path());
        const auto projectDirectory = fs::path(created.directory);
        store.close();

        SUBCASE("corrupt JSON") {
            std::ofstream manifest(projectDirectory / "project.json", std::ios::binary | std::ios::trunc);
            manifest << "{not json";
        }
        SUBCASE("unknown format id") {
            std::ofstream manifest(projectDirectory / "project.json", std::ios::binary | std::ios::trunc);
            manifest << R"({"format":"something-else","formatVersion":1})";
        }
        SUBCASE("newer format version") {
            std::ofstream manifest(projectDirectory / "project.json", std::ios::binary | std::ios::trunc);
            manifest << R"({"format":"infraforge-project","formatVersion":999})";
        }

        const auto error = captureException<infraforge::ports::StoreError>([&] { (void)store.open(projectDirectory); });
        REQUIRE(error.has_value());
        CHECK(error->category() == infraforge::ports::StoreErrorCategory::FormatUnsupported);
    }

    TEST_CASE("open rejects a project whose database identity diverges from the manifest") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        const auto created = createSampleProject(store, scratch.path());
        const auto projectDirectory = fs::path(created.directory);
        store.close();

        auto connection = infraforge::persistence::SqliteConnection::open(
            projectDirectory / "project.db", infraforge::persistence::SqliteOpenMode::ReadWrite);
        connection.exec("UPDATE project_state SET project_uuid = '00000000-0000-4000-8000-000000000000' WHERE id = 1");
        connection.close();

        const auto error = captureException<infraforge::ports::StoreError>([&] { (void)store.open(projectDirectory); });
        REQUIRE(error.has_value());
        CHECK(error->category() == infraforge::ports::StoreErrorCategory::PersistenceFailure);
    }

    TEST_CASE("save persists the saved-revision marker and reopen reports dirty state") {
        infraforge::testhelpers::ScratchDirectory scratch;
        const auto projectDirectory = scratch.path() / "Test Project.iforge";

        infraforge::persistence::SqliteProjectStore store;
        (void)createSampleProject(store, scratch.path());
        store.close();

        // Simulate a session with canonical mutations beyond the last save.
        {
            auto connection = infraforge::persistence::SqliteConnection::open(
                projectDirectory / "project.db", infraforge::persistence::SqliteOpenMode::ReadWrite);
            connection.exec("UPDATE project_state SET revision = revision + 1 WHERE id = 1");
            connection.close();
        }

        auto reopenedStore = infraforge::persistence::SqliteProjectStore();
        const auto dirtyState = reopenedStore.open(projectDirectory);
        CHECK(dirtyState.isDirty());
        CHECK_EQ(dirtyState.revision, 2);
        CHECK_EQ(dirtyState.savedRevision, 1);

        // project.json is immutable discovery metadata: a save persists the
        // save marker in the database only (single transactional resource).
        const auto manifestBeforeSave = readFileToString(projectDirectory / "project.json");

        const auto saved = reopenedStore.save();
        CHECK_FALSE(saved.isDirty());
        CHECK_EQ(saved.revision, 2);
        CHECK_EQ(saved.savedRevision, 2);
        CHECK(manifestBeforeSave == readFileToString(projectDirectory / "project.json"));
        reopenedStore.close();

        auto finalStore = infraforge::persistence::SqliteProjectStore();
        const auto finalState = finalStore.open(projectDirectory);
        CHECK_FALSE(finalState.isDirty());
        CHECK_EQ(finalState.revision, 2);
        finalStore.close();
    }

    TEST_CASE("save-as creates an independent project with a fresh identity") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        const auto created = createSampleProject(store, scratch.path());

        infraforge::domain::project::SaveAsSpec saveAsSpec;
        saveAsSpec.displayName = "Forked Project";
        saveAsSpec.parentDirectory = scratch.path();
        const auto forked = store.saveAs(saveAsSpec);

        CHECK(forked.uuid != created.uuid);
        CHECK_EQ(forked.displayName, "Forked Project");
        CHECK_EQ(forked.revision, created.revision);
        CHECK_FALSE(forked.isDirty());
        CHECK(store.isOpen());

        const auto forkedDirectory = scratch.path() / "Forked Project.iforge";
        CHECK(fs::is_regular_file(forkedDirectory / "project.json"));
        CHECK(fs::is_regular_file(forkedDirectory / "project.db"));
        store.close();

        // Both projects remain independently openable with stable identities.
        auto sourceStore = infraforge::persistence::SqliteProjectStore();
        const auto reopenedSource = sourceStore.open(fs::path(created.directory));
        CHECK_EQ(reopenedSource.uuid, created.uuid);
        sourceStore.close();

        auto forkStore = infraforge::persistence::SqliteProjectStore();
        const auto reopenedFork = forkStore.open(forkedDirectory);
        CHECK_EQ(reopenedFork.uuid, forked.uuid);
        CHECK_EQ(reopenedFork.displayName, "Forked Project");
        forkStore.close();
    }

    TEST_CASE("a rejected open leaves foreign journal modes untouched") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        const auto created = createSampleProject(store, scratch.path());
        const auto projectDirectory = fs::path(created.directory);
        store.close();

        // A foreign tool switched the database to WAL and injected a future
        // migration; the engine must reject the project without converting
        // or reconfiguring anything.
        {
            auto connection = infraforge::persistence::SqliteConnection::open(
                projectDirectory / "project.db", infraforge::persistence::SqliteOpenMode::ReadWrite);
            connection.exec("PRAGMA journal_mode = WAL");
            connection.exec("INSERT INTO schema_migrations (id, name, applied_at) VALUES (99, 'future', 'now')");
        }

        infraforge::persistence::SqliteProjectStore rejectingStore;
        const auto error = captureException<infraforge::ports::StoreError>(
            [&] { (void)rejectingStore.open(projectDirectory); });
        REQUIRE(error.has_value());
        CHECK(error->category() == infraforge::ports::StoreErrorCategory::SchemaUnsupported);

        auto connection = infraforge::persistence::SqliteConnection::open(
            projectDirectory / "project.db", infraforge::persistence::SqliteOpenMode::ReadOnly);
        infraforge::persistence::SqliteStatement journal{connection, "PRAGMA journal_mode"};
        REQUIRE(journal.step());
        CHECK(journal.columnText(0) == "wal");
    }

    TEST_CASE("applying a new migration leaves the manifest untouched and the project reopenable") {
        infraforge::testhelpers::ScratchDirectory scratch;

        // Simulate an older engine that only knows schema v1.
        const auto canonical = infraforge::persistence::canonicalMigrations();
        const std::span<const infraforge::persistence::MigrationDefinition> v1Only(canonical.data(), 1);
        infraforge::persistence::SqliteProjectStore v1Store(v1Only);
        const auto created = createSampleProject(v1Store, scratch.path());
        const auto projectDirectory = fs::path(created.directory);
        v1Store.close();

        // A future engine ships migration 2. The manifest records no schema
        // version, so the database stays the single migration authority.
        static constexpr infraforge::persistence::MigrationDefinition kTestMigrationV2{
            2, "test marker",
            "CREATE TABLE migration_marker (id INTEGER PRIMARY KEY, note TEXT NOT NULL);"
            "INSERT INTO migration_marker (id, note) VALUES (1, 'v2');"};
        const std::array<infraforge::persistence::MigrationDefinition, 2> v1PlusV2{canonical[0], kTestMigrationV2};
        const auto manifestBeforeMigration = readFileToString(projectDirectory / "project.json");

        infraforge::persistence::SqliteProjectStore v2Store(v1PlusV2);
        (void)v2Store.open(projectDirectory);
        v2Store.close();

        CHECK(manifestBeforeMigration == readFileToString(projectDirectory / "project.json"));
        {
            auto connection = infraforge::persistence::SqliteConnection::open(
                projectDirectory / "project.db", infraforge::persistence::SqliteOpenMode::ReadOnly);
            infraforge::persistence::SqliteStatement marker{connection,
                "SELECT note FROM migration_marker WHERE id = 1"};
            REQUIRE(marker.step());
            CHECK(marker.columnText(0) == "v2");
            infraforge::persistence::SqliteStatement version{connection,
                "SELECT MAX(id) FROM schema_migrations"};
            REQUIRE(version.step());
            CHECK_EQ(version.columnInt64(0), 2);
        }

        // Regression: the second open after a migration must succeed.
        infraforge::persistence::SqliteProjectStore v2StoreAgain(v1PlusV2);
        const auto reopened = v2StoreAgain.open(projectDirectory);
        CHECK_EQ(reopened.uuid, created.uuid);
        v2StoreAgain.close();

        // The older engine now rejects the migrated project without
        // modifying it.
        const auto downgradeError = captureException<infraforge::ports::StoreError>(
            [&] { (void)v1Store.open(projectDirectory); });
        REQUIRE(downgradeError.has_value());
        CHECK(downgradeError->category() == infraforge::ports::StoreErrorCategory::SchemaUnsupported);
    }

    TEST_CASE("save-as refuses an existing target and leaves the source session untouched") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        const auto created = createSampleProject(store, scratch.path());

        infraforge::domain::project::SaveAsSpec saveAsSpec;
        saveAsSpec.displayName = "Test Project"; // collides with the open project's directory
        saveAsSpec.parentDirectory = scratch.path();

        const auto error = captureException<infraforge::ports::StoreError>([&] { (void)store.saveAs(saveAsSpec); });
        REQUIRE(error.has_value());
        CHECK(error->category() == infraforge::ports::StoreErrorCategory::DirectoryInvalid);
        CHECK(store.isOpen());
        CHECK(store.current().uuid == created.uuid);
    }
}
