#include <doctest/doctest.h>

#include "TestHelpers.hpp"
#include "infraforge/ports/ProjectStore.hpp"
#include "infraforge/persistence/SchemaMigrations.hpp"
#include "infraforge/persistence/SqliteConnection.hpp"
#include "infraforge/persistence/SqliteProjectStore.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
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

        const auto saved = reopenedStore.save();
        CHECK_FALSE(saved.isDirty());
        CHECK_EQ(saved.revision, 2);
        CHECK_EQ(saved.savedRevision, 2);
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
