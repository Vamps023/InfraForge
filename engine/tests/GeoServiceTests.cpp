#include <doctest/doctest.h>

#include "TestHelpers.hpp"
#include "infraforge/application/GeoService.hpp"
#include "infraforge/persistence/SqliteProjectStore.hpp"

#include <cmath>
#include <vector>

namespace {

namespace app = infraforge::application;
namespace geo = infraforge::domain::geo;
using infraforge::testhelpers::sampleGeoreference;

infraforge::domain::project::CreateProjectSpec specFor(
    const infraforge::testhelpers::ScratchDirectory& scratch,
    const geo::GeoreferenceConfig& georeference) {
    infraforge::domain::project::CreateProjectSpec spec;
    spec.displayName = "Geo Service Project";
    spec.parentDirectory = scratch.path();
    spec.georeference = georeference;
    return spec;
}

} // namespace

TEST_SUITE("geo application service") {

TEST_CASE("getGeoreference resolves canonical info for the open project") {
    infraforge::testhelpers::ScratchDirectory scratch;
    infraforge::persistence::SqliteProjectStore store;
    geo::GeoTransformService transforms;
    app::GeoService service(store, transforms);
    app::ProjectService projects(store, transforms);

    (void)projects.create(specFor(scratch, sampleGeoreference()));

    const auto info = service.getGeoreference();
    CHECK(info.config.horizontalCrs == "EPSG:32633");
    CHECK(info.resolved.horizontalCrs.identifier == "EPSG:32633");
    CHECK(info.resolved.horizontalCrs.kind == geo::CrsKind::Projected);
    CHECK(std::abs(info.resolved.linearUnit.toMetre - 1.0) < 1e-9);
    CHECK(info.revision == 1);
}

TEST_CASE("setGeoreference validates, persists, and emits events") {
    infraforge::testhelpers::ScratchDirectory scratch;
    infraforge::persistence::SqliteProjectStore store;
    geo::GeoTransformService transforms;
    app::GeoService service(store, transforms);
    app::ProjectService projects(store, transforms);

    (void)projects.create(specFor(scratch, sampleGeoreference()));

    auto next = sampleGeoreference();
    next.horizontalCrs = "urn:ogc:def:crs:EPSG::32632";
    next.originEasting = 300000.0;
    next.originNorthing = 5500000.0;
    next.originHeight = 15.0;
    next.verticalCrs = "EPSG:3855";

    const auto result = service.setGeoreference(next, std::nullopt);

    // Persisted form is canonical: the URN becomes the authority code.
    CHECK(result.record.georeference.horizontalCrs == "EPSG:32632");
    CHECK(result.record.georeference.originHeight == doctest::Approx(15.0));
    CHECK(result.record.revision == 2);
    CHECK(result.record.isDirty());
    CHECK(store.current().georeference.horizontalCrs == "EPSG:32632");

    REQUIRE(result.events.size() == 3);
    CHECK(result.events[0].kind == app::ProjectEventKind::GeoreferenceChanged);
    CHECK(result.events[1].kind == app::ProjectEventKind::RevisionChanged);
    CHECK(result.events[2].kind == app::ProjectEventKind::DirtyStateChanged);
    REQUIRE(result.events[0].georeference.has_value());
    CHECK(result.events[0].georeference->horizontalCrs.identifier == "EPSG:32632");
}

TEST_CASE("setGeoreference rejects invalid and unsupported input without mutation") {
    infraforge::testhelpers::ScratchDirectory scratch;
    infraforge::persistence::SqliteProjectStore store;
    geo::GeoTransformService transforms;
    app::GeoService service(store, transforms);
    app::ProjectService projects(store, transforms);

    (void)projects.create(specFor(scratch, sampleGeoreference()));
    const auto before = store.current();

    auto garbage = sampleGeoreference();
    garbage.horizontalCrs = "bogus";
    try {
        (void)service.setGeoreference(garbage, std::nullopt);
        FAIL("expected CommandFailure");
    } catch (const app::CommandFailure& failure) {
        CHECK(failure.code() == app::CommandFailureCode::InvalidArgument);
    }

    auto geographic = sampleGeoreference();
    geographic.horizontalCrs = "EPSG:4326";
    try {
        (void)service.setGeoreference(geographic, std::nullopt);
        FAIL("expected CommandFailure");
    } catch (const app::CommandFailure& failure) {
        CHECK(failure.code() == app::CommandFailureCode::GeoUnsupported);
    }

    auto stale = sampleGeoreference();
    try {
        (void)service.setGeoreference(stale, 42);
        FAIL("expected CommandFailure");
    } catch (const app::CommandFailure& failure) {
        CHECK(failure.code() == app::CommandFailureCode::InvalidArgument);
    }

    CHECK(store.current() == before);
}

TEST_CASE("transformToProjectGlobal goes through the canonical service") {
    infraforge::testhelpers::ScratchDirectory scratch;
    infraforge::persistence::SqliteProjectStore store;
    geo::GeoTransformService transforms;
    app::GeoService service(store, transforms);
    app::ProjectService projects(store, transforms);

    (void)projects.create(specFor(scratch, sampleGeoreference()));

    const geo::SourceSpatialReference source{"EPSG:4326", ""};
    const std::vector<geo::GeoCoordinate> coordinates{{15.0, 55.0, 0.0}, {12.0, 55.5, 0.0}};

    const auto positions = service.transformToProjectGlobal(source, coordinates);
    REQUIRE(positions.size() == 2);
    CHECK(std::abs(positions[0].easting - 500000.0) < 0.001);
    CHECK(std::abs(positions[0].northing - 6094791.42) < 0.01);
}

TEST_CASE("unavailable grid transforms surface as GEO_UNSUPPORTED") {
    infraforge::testhelpers::ScratchDirectory scratch;
    infraforge::persistence::SqliteProjectStore store;
    geo::GeoTransformService transforms;
    app::GeoService service(store, transforms);
    app::ProjectService projects(store, transforms);

    auto config = sampleGeoreference();
    config.verticalCrs = "EPSG:6360"; // NAVD88 height (ftUS)
    (void)projects.create(specFor(scratch, config));

    // EGM2008 -> NAVD88 requires a geoid grid missing from the deployment.
    // The application layer must report the capability failure as
    // GeoUnsupported (COMMAND_ERROR_CODE_GEO_UNSUPPORTED), not as invalid
    // user input.
    const geo::SourceSpatialReference source{"EPSG:4326", "EPSG:3855"};
    const std::vector<geo::GeoCoordinate> coordinates{{15.0, 55.0, 10.0}};
    try {
        (void)service.transformToProjectGlobal(source, coordinates);
        FAIL("expected CommandFailure");
    } catch (const app::CommandFailure& failure) {
        CHECK(failure.code() == app::CommandFailureCode::GeoUnsupported);
    }
}

TEST_CASE("geo use cases fail cleanly without an open project") {
    infraforge::persistence::SqliteProjectStore store;
    geo::GeoTransformService transforms;
    app::GeoService service(store, transforms);

    CHECK_THROWS_AS((void)service.getGeoreference(), const app::CommandFailure&);
    CHECK_THROWS_AS((void)service.setGeoreference(sampleGeoreference(), std::nullopt),
        const app::CommandFailure&);
    CHECK_THROWS_AS((void)service.transformToProjectGlobal({"EPSG:4326", ""}, {}),
        const app::CommandFailure&);
}

} // TEST_SUITE
