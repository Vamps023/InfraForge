#include <doctest/doctest.h>

#include "TestHelpers.hpp"
#include "infraforge/persistence/ProjectManifest.hpp"
#include "infraforge/runtime/FileSystemUtf8.hpp"
#include "infraforge/persistence/SchemaMigrations.hpp"
#include "infraforge/persistence/SqliteConnection.hpp"
#include "infraforge/persistence/SqliteProjectStore.hpp"

using infraforge::domain::project::CreateProjectSpec;
using infraforge::persistence::SqliteProjectStore;

namespace {

CreateProjectSpec specWithGeo(
    const infraforge::testhelpers::ScratchDirectory& scratch,
    const infraforge::domain::geo::GeoreferenceConfig& georeference) {
    CreateProjectSpec spec;
    spec.displayName = "Geo Project";
    spec.parentDirectory = scratch.path();
    spec.georeference = georeference;
    return spec;
}

} // namespace

TEST_SUITE("georeference persistence") {

TEST_CASE("canonical georeference survives close and reopen") {
    infraforge::testhelpers::ScratchDirectory scratch;
    std::filesystem::path projectDirectory;

    infraforge::domain::geo::GeoreferenceConfig configured =
        infraforge::testhelpers::sampleGeoreference();
    configured.originEasting = 392472.91;
    configured.originNorthing = 5820310.44;
    configured.originHeight = 34.8;
    configured.verticalCrs = "EPSG:3855";

    {
        SqliteProjectStore store;
        const auto created = store.create(specWithGeo(scratch, configured));
        CHECK(created.georeference == configured);
        projectDirectory = created.directory;
    }
    {
        SqliteProjectStore store;
        const auto reopened = store.open(projectDirectory);
        CHECK(reopened.georeference == configured);
        CHECK(reopened.georeference.originHeight == doctest::Approx(34.8));
        CHECK(reopened.georeference.verticalCrs == "EPSG:3855");

        const auto manifest =
            infraforge::persistence::readProjectManifest(projectDirectory);
        CHECK(manifest.georeference == configured);
    }
}

TEST_CASE("updateGeoreference rewrites the canonical configuration") {
    infraforge::testhelpers::ScratchDirectory scratch;
    SqliteProjectStore store;

    auto created = store.create(specWithGeo(scratch, infraforge::testhelpers::sampleGeoreference()));
    CHECK(created.revision == 1);
    CHECK_FALSE(created.isDirty());

    auto updated = infraforge::testhelpers::sampleGeoreference();
    updated.horizontalCrs = "EPSG:32632";
    updated.originEasting = 1000000.0;
    updated.originNorthing = 2000000.0;
    updated.originHeight = 12.5;
    updated.verticalCrs = "EPSG:5773";

    const auto record = store.updateGeoreference(updated);
    CHECK(record.georeference == updated);
    CHECK(record.revision == 2);
    CHECK(record.isDirty());
    CHECK(store.current().georeference == updated);

    // The manifest mirrors the database row immediately.
    const auto manifest = infraforge::persistence::readProjectManifest(
        infraforge::runtime::pathFromUtf8(record.directory));
    CHECK(manifest.georeference == updated);

    (void)store.save();
    const std::filesystem::path directory = infraforge::runtime::pathFromUtf8(record.directory);
    store.close();
    {
        SqliteProjectStore reopener;
        const auto reopened = reopener.open(directory);
        CHECK(reopened.georeference == updated);
        CHECK(reopened.revision == 2);
        CHECK_FALSE(reopened.isDirty());
    }
}

TEST_CASE("a manifest georeference divergence is repaired from the canonical database") {
    infraforge::testhelpers::ScratchDirectory scratch;

    const auto canonical = infraforge::testhelpers::sampleGeoreference();
    std::filesystem::path projectDirectory;
    {
        SqliteProjectStore store;
        projectDirectory = store.create(specWithGeo(scratch, canonical)).directory;
    }

    // Simulate a crash between the manifest rewrite and the database commit
    // of a georeference update: the manifest is ahead of (or diverged from)
    // the canonical database row.
    auto diverged = infraforge::persistence::readProjectManifest(projectDirectory);
    diverged.georeference.horizontalCrs = "EPSG:32632";
    diverged.georeference.originEasting = 999999.0;
    infraforge::persistence::writeProjectManifest(projectDirectory, diverged);

    {
        SqliteProjectStore store;
        const auto reopened = store.open(projectDirectory);
        // The database row is the canonical authority and wins.
        CHECK(reopened.georeference == canonical);

        // The manifest was restored to canonical state on disk as well.
        const auto repaired = infraforge::persistence::readProjectManifest(projectDirectory);
        CHECK(repaired.georeference == canonical);
    }
}

TEST_CASE("a manifest identity divergence still fails as corruption") {
    infraforge::testhelpers::ScratchDirectory scratch;

    std::filesystem::path projectDirectory;
    {
        SqliteProjectStore store;
        projectDirectory = store.create(specWithGeo(scratch, infraforge::testhelpers::sampleGeoreference()))
                               .directory;
    }

    auto corrupted = infraforge::persistence::readProjectManifest(projectDirectory);
    corrupted.displayName = "Renamed Elsewhere";
    infraforge::persistence::writeProjectManifest(projectDirectory, corrupted);

    {
        SqliteProjectStore store;
        const auto error = infraforge::testhelpers::captureException<infraforge::ports::StoreError>(
            [&] { (void)store.open(projectDirectory); });
        REQUIRE(error.has_value());
        CHECK(error->category() == infraforge::ports::StoreErrorCategory::PersistenceFailure);
        CHECK_FALSE(store.isOpen());
    }
}

TEST_CASE("schema v1 databases gain origin_height through migration") {
    infraforge::testhelpers::ScratchDirectory scratch;
    std::filesystem::path projectDirectory;
    std::string projectUuid;

    const auto configured = infraforge::testhelpers::sampleGeoreference();
    {
        SqliteProjectStore store;
        const auto created = store.create(specWithGeo(scratch, configured));
        projectDirectory = created.directory;
        projectUuid = created.uuid;
    }

    // Roll the database back to schema v1: drop the migration-2 column and
    // mark only migration 1 applied.
    {
        auto connection = infraforge::persistence::SqliteConnection::open(
            projectDirectory / "project.db",
            infraforge::persistence::SqliteOpenMode::ReadWrite);
        connection.exec("ALTER TABLE georeference DROP COLUMN origin_height");
        connection.exec("DELETE FROM schema_migrations WHERE id = 2");
    }

    {
        SqliteProjectStore store;
        const auto reopened = store.open(projectDirectory);
        CHECK(reopened.georeference.horizontalCrs == configured.horizontalCrs);
        CHECK(reopened.georeference.originHeight == doctest::Approx(0.0));
    }
    {
        const auto verify = infraforge::persistence::SqliteConnection::open(
            projectDirectory / "project.db",
            infraforge::persistence::SqliteOpenMode::ReadOnly);
        CHECK(infraforge::persistence::readAppliedSchemaVersion(verify) == 2);
    }
}

} // TEST_SUITE
