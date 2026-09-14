#include <doctest/doctest.h>

#include "TestHelpers.hpp"
#include "infraforge/domain/road/Road.hpp"
#include "infraforge/domain/road/RoadRecord.hpp"
#include "infraforge/domain/road/RoadTypes.hpp"
#include "infraforge/ports/ProjectStore.hpp"
#include "infraforge/persistence/SqliteProjectStore.hpp"

#include <filesystem>
#include <string>

namespace {

using infraforge::domain::road::Road;
using infraforge::domain::road::RoadId;
using infraforge::domain::road::RoadRecord;
using infraforge::domain::road::LineSegment;
using infraforge::domain::road::CircularArcSegment;
using infraforge::domain::road::ClothoidSegment;
using infraforge::domain::road::ReferenceAlignment;
using infraforge::domain::road::AlignmentSegmentKind;
using infraforge::domain::road::SourceProvider;
using infraforge::domain::road::AnchorKind;
using infraforge::domain::road::ProtectedAnchor;
using infraforge::domain::road::toRecord;
using infraforge::domain::road::fromRecord;
using infraforge::domain::road::buildElevationProfile;
using infraforge::domain::road::buildSuperelevationProfile;

RoadId makeRoadId(const std::string& uuid) {
    return infraforge::domain::road::roadIdFromUuidText(uuid);
}

Road makeLineRoad() {
    LineSegment line{.start = {100.0, 200.0}, .heading = 0.5, .length = 150.0};
    auto alignment = ReferenceAlignment::build({line});
    REQUIRE(alignment.has_value());
    Road::BuildInput input{
        .id = makeRoadId("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"),
        .displayName = "Test Road",
        .alignment = *alignment,
    };
    auto road = Road::build(std::move(input));
    REQUIRE(road.has_value());
    return *road;
}

Road makeMultiSegmentRoad() {
    // Line -> Clothoid -> Arc
    LineSegment line{.start = {0.0, 0.0}, .heading = 0.0, .length = 100.0};
    const auto lineEnd = line.endSample();
    const double kappa = 0.01;
    ClothoidSegment spiral{.start = lineEnd.position, .startHeading = lineEnd.heading,
        .startCurvature = 0.0, .endCurvature = kappa, .length = 50.0};
    const auto spiralEnd = spiral.endSample();
    CircularArcSegment arc{.start = spiralEnd.position, .startHeading = spiralEnd.heading,
        .curvature = kappa, .length = 50.0};
    auto alignment = ReferenceAlignment::build({line, spiral, arc});
    REQUIRE(alignment.has_value());

    auto elev = buildElevationProfile({{0.0, 100.0}, {200.0, 120.0}});
    REQUIRE(elev.has_value());
    auto sup = buildSuperelevationProfile({{0.0, 0.0}, {100.0, 0.05}, {200.0, 0.0}});
    REQUIRE(sup.has_value());

    Road::BuildInput input{
        .id = makeRoadId("11111111-2222-3333-4444-555555555555"),
        .displayName = "Multi-Segment Road",
        .alignment = *alignment,
        .elevation = *elev,
        .superelevation = *sup,
    };
    auto road = Road::build(std::move(input));
    REQUIRE(road.has_value());
    return *road;
}

} // namespace

TEST_SUITE("road persistence") {

TEST_CASE("road record round-trips through toRecord/fromRecord") {
    const auto original = makeMultiSegmentRoad();
    const RoadRecord record = toRecord(original);
    auto restored = fromRecord(record);
    REQUIRE(restored.has_value());

    CHECK(restored->id() == original.id());
    CHECK(restored->displayName() == original.displayName());
    CHECK(restored->alignment().segmentCount() == original.alignment().segmentCount());
    CHECK(restored->alignment().totalLength() == doctest::Approx(original.alignment().totalLength()));

    // Verify segment types match.
    const auto& origSegs = original.alignment().segments();
    const auto& restSegs = restored->alignment().segments();
    for (std::size_t i = 0; i < origSegs.size(); ++i) {
        const auto origKind = infraforge::domain::road::segmentKind(origSegs[i].segment);
        const auto restKind = infraforge::domain::road::segmentKind(restSegs[i].segment);
        CHECK(origKind == restKind);
    }

    // Verify evaluation matches at several stations.
    for (double s = 0.0; s <= original.alignment().totalLength(); s += 25.0) {
        const auto origSample = original.alignment().evaluate(s);
        const auto restSample = restored->alignment().evaluate(s);
        CHECK(origSample.position.easting == doctest::Approx(restSample.position.easting).epsilon(1e-9));
        CHECK(origSample.position.northing == doctest::Approx(restSample.position.northing).epsilon(1e-9));
        CHECK(origSample.heading == doctest::Approx(restSample.heading).epsilon(1e-9));
    }

    // Verify profiles match.
    CHECK(restored->elevation().evaluate(100.0) == doctest::Approx(original.elevation().evaluate(100.0)));
    CHECK(restored->superelevation().evaluate(100.0) == doctest::Approx(original.superelevation().evaluate(100.0)));
}

TEST_CASE("road save and reopen reconstructs identical parameters") {
    infraforge::testhelpers::ScratchDirectory scratch;
    infraforge::persistence::SqliteProjectStore store;
    (void)store.create(infraforge::testhelpers::sampleCreateSpec(scratch.path()));

    const auto road = makeMultiSegmentRoad();
    const RoadRecord record = toRecord(road);
    (void)store.insertRoad(record);

    (void)store.save();
    (void)store.close();

    // Reopen and verify.
    const auto projectDirectory = scratch.path() / "Test Project.iforge";
    (void)store.open(projectDirectory);
    auto roads = store.roads();
    REQUIRE(roads.size() == 1);
    const auto& restored = roads[0];

    CHECK(restored.id == road.id());
    CHECK(restored.displayName == road.displayName());
    REQUIRE(restored.segments.size() == 3);
    CHECK(restored.segments[0].kind == AlignmentSegmentKind::Line);
    CHECK(restored.segments[1].kind == AlignmentSegmentKind::Clothoid);
    CHECK(restored.segments[2].kind == AlignmentSegmentKind::CircularArc);
    CHECK(restored.segments[0].start.easting == doctest::Approx(0.0));
    CHECK(restored.segments[0].length == doctest::Approx(100.0));
    CHECK(restored.segments[2].curvature == doctest::Approx(0.01));

    // Reconstruct the road and verify evaluation.
    auto rebuilt = fromRecord(restored);
    REQUIRE(rebuilt.has_value());
    for (double s = 0.0; s <= road.alignment().totalLength(); s += 25.0) {
        const auto origSample = road.alignment().evaluate(s);
        const auto rebuiltSample = rebuilt->alignment().evaluate(s);
        CHECK(origSample.position.easting == doctest::Approx(rebuiltSample.position.easting).epsilon(1e-9));
        CHECK(origSample.position.northing == doctest::Approx(rebuiltSample.position.northing).epsilon(1e-9));
    }
    (void)store.close();
}

TEST_CASE("multiple roads persist and reopen in order") {
    infraforge::testhelpers::ScratchDirectory scratch;
    infraforge::persistence::SqliteProjectStore store;
    (void)store.create(infraforge::testhelpers::sampleCreateSpec(scratch.path()));

    (void)store.insertRoad(toRecord(makeLineRoad()));
    (void)store.insertRoad(toRecord(makeMultiSegmentRoad()));

    (void)store.save();
    (void)store.close();

    const auto projectDirectory = scratch.path() / "Test Project.iforge";
    (void)store.open(projectDirectory);
    auto roads = store.roads();
    CHECK(roads.size() == 2);
    (void)store.close();
}

TEST_CASE("road removal advances revision and removes data") {
    infraforge::testhelpers::ScratchDirectory scratch;
    infraforge::persistence::SqliteProjectStore store;
    (void)store.create(infraforge::testhelpers::sampleCreateSpec(scratch.path()));

    const auto road = makeLineRoad();
    const auto record = toRecord(road);
    (void)store.insertRoad(record);
    const auto revisionBefore = store.current().revision;

    (void)store.removeRoad(infraforge::domain::road::uuidTextFromRoadId(road.id()));
    CHECK(store.current().revision == revisionBefore + 1);
    CHECK(store.roads().empty());
    (void)store.close();
}

TEST_CASE("imported road with source geometry persists and reopens") {
    infraforge::testhelpers::ScratchDirectory scratch;
    infraforge::persistence::SqliteProjectStore store;
    (void)store.create(infraforge::testhelpers::sampleCreateSpec(scratch.path()));

    LineSegment line{.start = {0.0, 0.0}, .heading = 0.0, .length = 100.0};
    auto alignment = ReferenceAlignment::build({line});
    REQUIRE(alignment.has_value());

    infraforge::domain::road::RoadSource source;
    source.geometry.sourceCrs = "EPSG:4326";
    source.geometry.vertices = {{-122.4, 37.8, 10.0}, {-122.41, 37.81, 12.0}};
    source.provenance.provider = SourceProvider::Osm;
    source.provenance.sourceId = "way/123456";
    source.provenance.tags = {{"highway", "motorway"}};
    source.provenance.importedAt = "2026-09-14T00:00:00Z";
    source.protectedAnchors = {
        {0.0, {0.0, 0.0}, AnchorKind::Endpoint},
        {100.0, {100.0, 0.0}, AnchorKind::Endpoint}};

    Road::BuildInput input{
        .id = makeRoadId("cccccccc-dddd-eeee-ffff-000000000000"),
        .displayName = "Imported Road",
        .alignment = *alignment,
        .source = source,
    };
    auto road = Road::build(std::move(input));
    REQUIRE(road.has_value());

    (void)store.insertRoad(toRecord(*road));
    (void)store.save();
    (void)store.close();

    const auto projectDirectory = scratch.path() / "Test Project.iforge";
    (void)store.open(projectDirectory);
    auto roads = store.roads();
    REQUIRE(roads.size() == 1);
    const auto& restored = roads[0];
    CHECK(restored.hasSource);
    CHECK(restored.provider == SourceProvider::Osm);
    CHECK(restored.sourceId == "way/123456");
    CHECK(restored.sourceCrs == "EPSG:4326");
    REQUIRE(restored.sourceVertices.size() == 2);
    CHECK(restored.sourceVertices[0].x == doctest::Approx(-122.4));
    REQUIRE(restored.sourceTags.size() == 1);
    CHECK(restored.sourceTags[0].key == "highway");
    CHECK(restored.sourceTags[0].value == "motorway");
    REQUIRE(restored.protectedAnchors.size() == 2);
    CHECK(restored.protectedAnchors[0].kind == AnchorKind::Endpoint);
    (void)store.close();
}

} // TEST_SUITE
