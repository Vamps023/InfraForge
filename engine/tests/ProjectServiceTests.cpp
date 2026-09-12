#include <doctest/doctest.h>

#include "TestHelpers.hpp"
#include "infraforge/application/ProjectService.hpp"
#include "infraforge/persistence/SqliteProjectStore.hpp"

#include <filesystem>
#include <stdexcept>

TEST_SUITE("project service") {
    TEST_CASE("create opens the session and emits an opened event") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        infraforge::domain::geo::GeoTransformService transforms;
        infraforge::application::ProjectService service(store, transforms);

        const auto result = service.create(infraforge::testhelpers::sampleCreateSpec(scratch.path()));
        REQUIRE(result.events.size() == 1);
        CHECK(result.events[0].kind == infraforge::application::ProjectEventKind::Opened);
        CHECK_EQ(result.record.revision, 1);
        CHECK_EQ(result.record.uuid, store.current().uuid);
        store.close();
    }

    TEST_CASE("commands reject operation without an open project") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        infraforge::domain::geo::GeoTransformService transforms;
        infraforge::application::ProjectService service(store, transforms);

        using infraforge::application::CommandFailure;
        using infraforge::application::CommandFailureCode;
        using infraforge::testhelpers::captureException;

        for (const auto& failure : {captureException<CommandFailure>([&] { (void)service.save(); }),
                 captureException<CommandFailure>([&] { (void)service.close(); }),
                 captureException<CommandFailure>([&] { (void)service.getSummary(); })}) {
            REQUIRE(failure.has_value());
            CHECK(failure->code() == CommandFailureCode::ProjectNotOpen);
        }
    }

    TEST_CASE("create and open reject a second concurrent session") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        infraforge::domain::geo::GeoTransformService transforms;
        infraforge::application::ProjectService service(store, transforms);

        (void)service.create(infraforge::testhelpers::sampleCreateSpec(scratch.path()));

        using infraforge::application::CommandFailure;
        using infraforge::application::CommandFailureCode;

        bool createConflict = false;
        try {
            (void)service.create(infraforge::testhelpers::sampleCreateSpec(scratch.path()));
        } catch (const CommandFailure& failure) {
            createConflict = failure.code() == CommandFailureCode::ProjectAlreadyOpen;
        }
        CHECK(createConflict);

        bool openConflict = false;
        try {
            (void)service.open(std::filesystem::path(store.current().directory));
        } catch (const CommandFailure& failure) {
            openConflict = failure.code() == CommandFailureCode::ProjectAlreadyOpen;
        }
        CHECK(openConflict);
        CHECK(store.isOpen());
        store.close();
    }

    TEST_CASE("close ends the session and emits a closed event; reopen works") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        infraforge::domain::geo::GeoTransformService transforms;
        infraforge::application::ProjectService service(store, transforms);

        const auto created = service.create(infraforge::testhelpers::sampleCreateSpec(scratch.path()));
        const auto closed = service.close();

        REQUIRE(closed.events.size() == 1);
        CHECK(closed.events[0].kind == infraforge::application::ProjectEventKind::Closed);
        CHECK(closed.sessionClosed);
        CHECK_EQ(closed.record.uuid, created.record.uuid);
        CHECK_FALSE(store.isOpen());

        const auto reopened = service.open(std::filesystem::path(created.record.directory));
        REQUIRE(reopened.events.size() == 1);
        CHECK(reopened.events[0].kind == infraforge::application::ProjectEventKind::Opened);
        CHECK_EQ(reopened.record.uuid, created.record.uuid);
        CHECK_EQ(reopened.record.revision, created.record.revision);

        const auto summary = service.getSummary();
        CHECK(summary.events.empty());
        CHECK_FALSE(summary.sessionClosed);
        CHECK_EQ(summary.record.uuid, created.record.uuid);
        store.close();
    }

    TEST_CASE("save on a clean session is a no-op without events") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        infraforge::domain::geo::GeoTransformService transforms;
        infraforge::application::ProjectService service(store, transforms);
        (void)service.create(infraforge::testhelpers::sampleCreateSpec(scratch.path()));

        // No mutation commands exist yet, so a fresh session is never dirty.
        // The dirty transition is exercised at store level in
        // SqliteProjectStoreTests; this pins the service contract for the
        // clean case.
        const auto cleanSave = service.save();
        CHECK(cleanSave.events.empty());
        CHECK_FALSE(cleanSave.sessionClosed);
        CHECK_FALSE(cleanSave.record.isDirty());
        store.close();
    }

    TEST_CASE("open rejects a project whose persisted CRS cannot be resolved") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        infraforge::domain::geo::GeoTransformService transforms;
        infraforge::application::ProjectService service(store, transforms);

        // Persist an invalid CRS directly through the store (the store does
        // not validate georeferences; the service boundary does).
        auto spec = infraforge::testhelpers::sampleCreateSpec(scratch.path());
        spec.georeference.horizontalCrs = "not-a-crs";
        const auto created = store.create(spec);
        store.close();

        using infraforge::application::CommandFailure;
        using infraforge::application::CommandFailureCode;
        bool rejected = false;
        try {
            (void)service.open(std::filesystem::path(created.directory));
        } catch (const CommandFailure& failure) {
            rejected = failure.code() == CommandFailureCode::InvalidArgument;
        }
        CHECK(rejected);
        CHECK_FALSE(store.isOpen());

        // The failed open leaves no half-open session: a valid project
        // creates/opens normally afterwards.
        auto validSpec = infraforge::testhelpers::sampleCreateSpec(scratch.path());
        validSpec.displayName = "Valid Project";
        (void)service.create(validSpec);
        CHECK(store.isOpen());
        store.close();
    }

    TEST_CASE("open rejects a project whose persisted CRS resolves but is unsupported") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        infraforge::domain::geo::GeoTransformService transforms;
        infraforge::application::ProjectService service(store, transforms);

        // A geographic-only horizontal CRS is well-formed but unsupported
        // as a project frame.
        auto spec = infraforge::testhelpers::sampleCreateSpec(scratch.path());
        spec.georeference.horizontalCrs = "EPSG:4326";
        const auto created = store.create(spec);
        store.close();

        using infraforge::application::CommandFailure;
        using infraforge::application::CommandFailureCode;
        bool rejected = false;
        try {
            (void)service.open(std::filesystem::path(created.directory));
        } catch (const CommandFailure& failure) {
            rejected = failure.code() == CommandFailureCode::GeoUnsupported;
        }
        CHECK(rejected);
        CHECK_FALSE(store.isOpen());
    }

    TEST_CASE("open rejects a project whose persisted vertical CRS is invalid") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        infraforge::domain::geo::GeoTransformService transforms;
        infraforge::application::ProjectService service(store, transforms);

        auto spec = infraforge::testhelpers::sampleCreateSpec(scratch.path());
        spec.georeference.verticalCrs = "not-a-vertical-crs";
        const auto created = store.create(spec);
        store.close();

        using infraforge::application::CommandFailure;
        CHECK_THROWS_AS(
            (void)service.open(std::filesystem::path(created.directory)),
            const CommandFailure&);
        CHECK_FALSE(store.isOpen());
    }

    TEST_CASE("save-as emits closed and opened events") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        infraforge::domain::geo::GeoTransformService transforms;
        infraforge::application::ProjectService service(store, transforms);
        const auto created = service.create(infraforge::testhelpers::sampleCreateSpec(scratch.path()));

        infraforge::domain::project::SaveAsSpec saveAsSpec;
        saveAsSpec.displayName = "Forked";
        saveAsSpec.parentDirectory = scratch.path();
        const auto result = service.saveAs(saveAsSpec);

        REQUIRE(result.events.size() == 2);
        CHECK(result.events[0].kind == infraforge::application::ProjectEventKind::Closed);
        CHECK_EQ(result.events[0].record.uuid, created.record.uuid);
        CHECK(result.events[1].kind == infraforge::application::ProjectEventKind::Opened);
        CHECK(result.events[1].record.uuid != created.record.uuid);
        CHECK_FALSE(result.sessionClosed);
        store.close();
    }
}
