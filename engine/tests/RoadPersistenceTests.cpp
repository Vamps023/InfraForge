#include <doctest/doctest.h>

#include "TestHelpers.hpp"
#include "infraforge/domain/road/Road.hpp"
#include "infraforge/domain/road/RoadRecord.hpp"
#include "infraforge/domain/road/RoadTypes.hpp"
#include "infraforge/ports/ProjectStore.hpp"
#include "infraforge/persistence/SqliteProjectStore.hpp"

#include <filesystem>
#include <string>
#include <set>

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

TEST_CASE("protected-anchor curvature boundary survives SQLite reopen") {
    CircularArcSegment first{.start={0.0,0.0}, .startHeading=0.0,
        .curvature=0.01, .length=20.0};
    const auto boundary = first.endSample();
    CircularArcSegment second{.start=boundary.position, .startHeading=boundary.heading,
        .curvature=0.02, .length=20.0};
    auto alignment = ReferenceAlignment::build({first, second}, 1e-6, 1e-6, 1e-9, {1});
    REQUIRE(alignment.has_value());
    Road::BuildInput input{.id=makeRoadId("99999999-2222-3333-4444-555555555555"),
        .displayName="Anchored", .alignment=*alignment};
    input.source.protectedAnchors.push_back({20.0, boundary.position, AnchorKind::Junction});
    auto road = Road::build(std::move(input));
    REQUIRE(road.has_value());
    auto record = toRecord(*road);
    record.anchorBoundarySegments.insert(1);

    infraforge::testhelpers::ScratchDirectory scratch;
    infraforge::persistence::SqliteProjectStore store;
    (void)store.create(infraforge::testhelpers::sampleCreateSpec(scratch.path()));
    (void)store.insertRoad(record); (void)store.save(); (void)store.close();
    (void)store.open(scratch.path() / "Test Project.iforge");
    const auto records = store.roads();
    REQUIRE(records.size() == 1);
    CHECK(records[0].id == record.id);
    CHECK(records[0].anchorBoundarySegments == std::set<std::size_t>{1});
    REQUIRE(records[0].protectedAnchors.size() == 1);
    CHECK(records[0].protectedAnchors[0].position == boundary.position);
    const auto restored = fromRecord(records[0]);
    REQUIRE(restored.has_value());
    CHECK(restored->alignment().evaluate(20.0).position == boundary.position);
    CHECK(restored->alignment().evaluate(20.001).position.easting != boundary.position.easting);
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
    source.geometry.vertices = {
        {-122.4, 37.8, std::optional<double>{10.0}},
        {-122.41, 37.81, std::optional<double>{12.0}}};
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
    REQUIRE(restored.sourceVertices[0].z.has_value());
    CHECK(*restored.sourceVertices[0].z == doctest::Approx(10.0));
    REQUIRE(restored.sourceTags.size() == 1);
    CHECK(restored.sourceTags[0].key == "highway");
    CHECK(restored.sourceTags[0].value == "motorway");
    REQUIRE(restored.protectedAnchors.size() == 2);
    CHECK(restored.protectedAnchors[0].kind == AnchorKind::Endpoint);
    (void)store.close();
}

TEST_CASE("source vertex with missing elevation round-trips as absent z") {
    infraforge::testhelpers::ScratchDirectory scratch;
    infraforge::persistence::SqliteProjectStore store;
    (void)store.create(infraforge::testhelpers::sampleCreateSpec(scratch.path()));

    LineSegment line{.start = {0.0, 0.0}, .heading = 0.0, .length = 100.0};
    auto alignment = ReferenceAlignment::build({line});
    REQUIRE(alignment.has_value());

    infraforge::domain::road::RoadSource source;
    source.geometry.sourceCrs = "EPSG:4326";
    // First vertex has no elevation (z absent), second has elevation.
    source.geometry.vertices = {
        {-122.4, 37.8, std::optional<double>{}},
        {-122.41, 37.81, std::optional<double>{12.0}}};
    source.provenance.provider = SourceProvider::Osm;
    source.provenance.sourceId = "way/789";
    source.provenance.importedAt = "2026-09-14T00:00:00Z";

    Road::BuildInput input{
        .id = makeRoadId("dddddddd-eeee-ffff-0000-111111111111"),
        .displayName = "Missing Z Road",
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
    REQUIRE(restored.sourceVertices.size() == 2);
    CHECK_FALSE(restored.sourceVertices[0].z.has_value());  // missing z stays absent
    REQUIRE(restored.sourceVertices[1].z.has_value());
    CHECK(*restored.sourceVertices[1].z == doctest::Approx(12.0));
    (void)store.close();
}

TEST_CASE("lane sections and lanes round-trip through SQLite store") {
    infraforge::testhelpers::ScratchDirectory scratch;
    infraforge::persistence::SqliteProjectStore store;
    (void)store.create(infraforge::testhelpers::sampleCreateSpec(scratch.path()));

    LineSegment line{.start = {0.0, 0.0}, .heading = 0.0, .length = 200.0};
    auto alignment = ReferenceAlignment::build({line});
    REQUIRE(alignment.has_value());

    Road::BuildInput input{
        .id = makeRoadId("aaaaaaaa-1111-2222-3333-444444444444"),
        .displayName = "Lane Test Road",
        .alignment = *alignment,
    };
    auto road = Road::build(std::move(input));
    REQUIRE(road.has_value());

    auto record = toRecord(*road);
    REQUIRE(record.laneSections.size() == 1);
    REQUIRE(record.lanes.size() == 2);

    record.laneSections = {
        {0, 0.0, 100.0},
        {1, 100.0, 200.0},
    };
    record.lanes = {
        {"lane-l1", 0, "left", 1, "driving", "backward", 3.5},
        {"lane-r1", 0, "right", 1, "driving", "forward", 3.5},
        {"lane-l1-s2", 1, "left", 1, "driving", "backward", 3.75},
        {"lane-l2-s2", 1, "left", 2, "biking", "backward", 1.5},
        {"lane-r1-s2", 1, "right", 1, "driving", "forward", 3.75},
        {"lane-r2-s2", 1, "right", 2, "biking", "forward", 1.5},
    };

    (void)store.insertRoad(record);
    (void)store.save();
    (void)store.close();

    const auto projectDirectory = scratch.path() / "Test Project.iforge";
    (void)store.open(projectDirectory);
    auto roads = store.roads();
    REQUIRE(roads.size() == 1);
    const auto& restored = roads[0];
    REQUIRE(restored.laneSections.size() == 2);
    CHECK(restored.laneSections[0].startStation == 0.0);
    CHECK(restored.laneSections[0].endStation == 100.0);
    CHECK(restored.laneSections[1].startStation == 100.0);
    CHECK(restored.laneSections[1].endStation == 200.0);

    REQUIRE(restored.lanes.size() == 6);
    CHECK(restored.lanes[0].laneId == "lane-l1");
    CHECK(restored.lanes[0].side == "left");
    CHECK(restored.lanes[1].laneId == "lane-r1");
    CHECK(restored.lanes[1].side == "right");
    CHECK(restored.lanes[3].laneId == "lane-l2-s2");
    CHECK(restored.lanes[3].type == "biking");
    CHECK(restored.lanes[3].width == doctest::Approx(1.5));
    (void)store.close();
}

TEST_CASE("junctions round-trip through SQLite store") {
    infraforge::testhelpers::ScratchDirectory scratch;
    infraforge::persistence::SqliteProjectStore store;
    (void)store.create(infraforge::testhelpers::sampleCreateSpec(scratch.path()));

    infraforge::domain::road::JunctionRecord j;
    j.id = infraforge::domain::road::junctionIdFromUuidText("99999999-8888-7777-6666-555555555555");
    j.name = "Downtown Intersection";
    j.type = "four_way";
    j.posX = 150.0;
    j.posY = 250.0;
    j.elevation = 45.0;
    j.revision = 1;

    infraforge::domain::road::JunctionApproachRecord app1;
    app1.roadId = "road-1";
    app1.contactPoint = "end";
    app1.entryPointX = 150.0;
    app1.entryPointY = 240.0;
    app1.heading = 1.57;
    app1.laneIds = {"l1", "r1"};

    infraforge::domain::road::JunctionApproachRecord app2;
    app2.roadId = "road-2";
    app2.contactPoint = "start";
    app2.entryPointX = 150.0;
    app2.entryPointY = 260.0;
    app2.heading = 4.71;
    app2.laneIds = {"l1", "r1"};

    j.approaches = {app1, app2};

    infraforge::domain::road::JunctionConnectionRecord conn1;
    conn1.id = "conn-1";
    conn1.fromRoadId = "road-1";
    conn1.fromLaneId = "r1";
    conn1.toRoadId = "road-2";
    conn1.toLaneId = "r1";
    conn1.movementType = "straight";
    conn1.allowed = true;

    j.connections = {conn1};

    auto inserted = store.insertJunction(j);
    CHECK(inserted.name == "Downtown Intersection");
    auto junctions = store.junctions();
    REQUIRE(junctions.size() == 1);
    CHECK(junctions[0].approaches.size() == 2);
    CHECK(junctions[0].approaches[0].laneIds.size() == 2);
    CHECK(junctions[0].connections.size() == 1);
    CHECK(junctions[0].connections[0].movementType == "straight");

    // Update junction
    j.name = "Updated Intersection";
    j.elevation = 48.0;
    auto updated = store.updateJunction(j);
    CHECK(updated.name == "Updated Intersection");
    CHECK(updated.elevation == doctest::Approx(48.0));

    // Save and reopen
    (void)store.save();
    (void)store.close();

    const auto projectDirectory = scratch.path() / "Test Project.iforge";
    (void)store.open(projectDirectory);
    auto reopenedJunctions = store.junctions();
    REQUIRE(reopenedJunctions.size() == 1);
    CHECK(reopenedJunctions[0].name == "Updated Intersection");
    CHECK(reopenedJunctions[0].elevation == doctest::Approx(48.0));
    CHECK(reopenedJunctions[0].approaches.size() == 2);
    CHECK(reopenedJunctions[0].connections.size() == 1);

    // Delete junction
    store.removeJunction("99999999-8888-7777-6666-555555555555");
    CHECK(store.junctions().empty());
    (void)store.close();
}

} // TEST_SUITE
