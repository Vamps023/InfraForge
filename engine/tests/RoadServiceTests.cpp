#include "TestHelpers.hpp"
#include "infraforge/application/CommandFailure.hpp"
#include "infraforge/application/RoadService.hpp"
#include "infraforge/application/WorldState.hpp"
#include "infraforge/domain/geo/GeoTransformService.hpp"
#include "infraforge/domain/geo/ProjectGeoreference.hpp"
#include "infraforge/domain/road/Road.hpp"
#include "infraforge/domain/road/RoadRecord.hpp"
#include "infraforge/domain/road/RoadTypes.hpp"
#include "infraforge/persistence/SqliteProjectStore.hpp"

#include <doctest/doctest.h>
#include <cmath>
#include <filesystem>
#include <memory>
#include <vector>

namespace infraforge::application {
namespace {

using namespace infraforge::domain::road;

struct RoadServiceTestFixture {
    infraforge::testhelpers::ScratchDirectory scratch;
    infraforge::persistence::SqliteProjectStore store;
    infraforge::domain::geo::GeoTransformService transforms;
    WorldState world;
    std::optional<RoadService> roadService;
    std::vector<RoadServiceEvent> events;
    std::filesystem::path projectDirectory;

    RoadServiceTestFixture() {
        infraforge::domain::project::CreateProjectSpec createSpec;
        createSpec.displayName = "Road Service Tests";
        createSpec.parentDirectory = scratch.path();
        createSpec.trafficSide = infraforge::domain::project::TrafficSide::Right;
        createSpec.georeference.horizontalCrs = "EPSG:32633";
        createSpec.georeference.linearUnit = "metre";
        createSpec.georeference.axisConvention = infraforge::domain::geo::AxisConvention::EastingNorthingUp;
        createSpec.georeference.originEasting = 0.0;
        createSpec.georeference.originNorthing = 0.0;
        createSpec.georeference.originHeight = 0.0;
        (void)store.create(createSpec);
        store.close();

        projectDirectory = scratch.path() / "Road Service Tests.iforge";
        (void)store.open(projectDirectory);

        auto project = transforms.resolveProjectGeoreference(store.current().georeference);
        world.resetForProject(project);

        roadService.emplace(store, world,
            [this](const RoadServiceEvent& e) { events.push_back(e); });
        roadService->onProjectOpened();
    }

    ~RoadServiceTestFixture() {
        roadService.reset();
        store.close();
    }

    std::vector<AlignmentPoint> makeStraightPolyline(
        double startX, double startY, double heading, double length, int n) {
        std::vector<AlignmentPoint> pts;
        const double dx = std::cos(heading) * length / (n - 1);
        const double dy = std::sin(heading) * length / (n - 1);
        for (int i = 0; i < n; ++i) {
            pts.push_back({startX + dx * i, startY + dy * i});
        }
        return pts;
    }
};

} // namespace

TEST_CASE_FIXTURE(RoadServiceTestFixture, "create road produces a valid road") {
    CreateRoadInput input;
    input.name = "Test Road";
    input.sourcePoints = makeStraightPolyline(0.0, 0.0, 0.0, 100.0, 10);
    input.positionTolerance = 1.0;

    auto summary = roadService->createRoad(input);
    CHECK_FALSE(summary.roadId.empty());
    CHECK(summary.name == "Test Road");
    CHECK(summary.alignmentSegmentCount >= 1);
    CHECK(summary.length > 0.0);
    CHECK(summary.sourceProvider == "authored");

    REQUIRE(events.size() >= 1);
    CHECK(events.back().kind == RoadServiceEvent::Kind::Created);
}

TEST_CASE_FIXTURE(RoadServiceTestFixture, "list roads returns created roads") {
    CreateRoadInput input;
    input.name = "Road A";
    input.sourcePoints = makeStraightPolyline(0.0, 0.0, 0.0, 100.0, 10);
    input.positionTolerance = 1.0;
    (void)roadService->createRoad(input);

    input.name = "Road B";
    input.sourcePoints = makeStraightPolyline(200.0, 0.0, 0.0, 100.0, 10);
    (void)roadService->createRoad(input);

    auto roads = roadService->listRoads();
    CHECK(roads.size() == 2);
}

TEST_CASE_FIXTURE(RoadServiceTestFixture, "get road returns details") {
    CreateRoadInput input;
    input.name = "Detail Road";
    input.sourcePoints = makeStraightPolyline(0.0, 0.0, 0.0, 100.0, 10);
    input.positionTolerance = 1.0;
    auto summary = roadService->createRoad(input);

    auto details = roadService->getRoad(summary.roadId);
    REQUIRE(details.has_value());
    CHECK(details->name == "Detail Road");
    CHECK(details->alignmentSegmentCount >= 1);
    CHECK(details->isValid);
}

TEST_CASE_FIXTURE(RoadServiceTestFixture, "delete road removes it") {
    CreateRoadInput input;
    input.name = "To Delete";
    input.sourcePoints = makeStraightPolyline(0.0, 0.0, 0.0, 100.0, 10);
    input.positionTolerance = 1.0;
    auto summary = roadService->createRoad(input);

    (void)roadService->deleteRoad(summary.roadId);

    auto roads = roadService->listRoads();
    CHECK(roads.empty());

    auto details = roadService->getRoad(summary.roadId);
    CHECK_FALSE(details.has_value());
}

TEST_CASE_FIXTURE(RoadServiceTestFixture, "rename road changes name") {
    CreateRoadInput input;
    input.name = "Original Name";
    input.sourcePoints = makeStraightPolyline(0.0, 0.0, 0.0, 100.0, 10);
    input.positionTolerance = 1.0;
    auto summary = roadService->createRoad(input);

    auto renamed = roadService->renameRoad(summary.roadId, "New Name");
    CHECK(renamed.name == "New Name");

    auto details = roadService->getRoad(summary.roadId);
    REQUIRE(details.has_value());
    CHECK(details->name == "New Name");
}

TEST_CASE_FIXTURE(RoadServiceTestFixture, "undo create removes the road") {
    CreateRoadInput input;
    input.name = "Undo Me";
    input.sourcePoints = makeStraightPolyline(0.0, 0.0, 0.0, 100.0, 10);
    input.positionTolerance = 1.0;
    auto summary = roadService->createRoad(input);

    REQUIRE(roadService->canUndo(summary.roadId));
    REQUIRE(roadService->undo(summary.roadId));

    auto roads = roadService->listRoads();
    CHECK(roads.empty());
}

TEST_CASE_FIXTURE(RoadServiceTestFixture, "redo re-creates the road") {
    CreateRoadInput input;
    input.name = "Redo Me";
    input.sourcePoints = makeStraightPolyline(0.0, 0.0, 0.0, 100.0, 10);
    input.positionTolerance = 1.0;
    auto summary = roadService->createRoad(input);

    (void)roadService->undo(summary.roadId);
    REQUIRE(roadService->canRedo(summary.roadId));
    REQUIRE(roadService->redo(summary.roadId));

    auto roads = roadService->listRoads();
    CHECK(roads.size() == 1);
    CHECK(roads[0].name == "Redo Me");
}

TEST_CASE_FIXTURE(RoadServiceTestFixture, "undo rename restores original name") {
    CreateRoadInput input;
    input.name = "Original";
    input.sourcePoints = makeStraightPolyline(0.0, 0.0, 0.0, 100.0, 10);
    input.positionTolerance = 1.0;
    auto summary = roadService->createRoad(input);

    (void)roadService->renameRoad(summary.roadId, "Renamed");
    (void)roadService->undo(summary.roadId);

    auto details = roadService->getRoad(summary.roadId);
    REQUIRE(details.has_value());
    CHECK(details->name == "Original");
}

TEST_CASE_FIXTURE(RoadServiceTestFixture, "undo delete restores the road") {
    CreateRoadInput input;
    input.name = "Delete Me";
    input.sourcePoints = makeStraightPolyline(0.0, 0.0, 0.0, 100.0, 10);
    input.positionTolerance = 1.0;
    auto summary = roadService->createRoad(input);

    (void)roadService->deleteRoad(summary.roadId);
    (void)roadService->undo(summary.roadId);

    auto roads = roadService->listRoads();
    CHECK(roads.size() == 1);
    CHECK(roads[0].name == "Delete Me");
}

TEST_CASE_FIXTURE(RoadServiceTestFixture, "create road with too few points fails") {
    CreateRoadInput input;
    input.name = "Bad Road";
    input.sourcePoints = {{0.0, 0.0}};
    input.positionTolerance = 1.0;

    CHECK_THROWS_AS([&]{ (void)roadService->createRoad(input); }(), CommandFailure);
}

TEST_CASE_FIXTURE(RoadServiceTestFixture, "create road with non-finite coords fails") {
    CreateRoadInput input;
    input.name = "NaN Road";
    input.sourcePoints = {{0.0, 0.0}, {std::numeric_limits<double>::quiet_NaN(), 100.0}};
    input.positionTolerance = 1.0;

    CHECK_THROWS_AS([&]{ (void)roadService->createRoad(input); }(), CommandFailure);
}

TEST_CASE_FIXTURE(RoadServiceTestFixture, "road persists across save and reopen") {
    CreateRoadInput input;
    input.name = "Persistent Road";
    input.sourcePoints = makeStraightPolyline(0.0, 0.0, 0.0, 100.0, 10);
    input.positionTolerance = 1.0;
    auto summary = roadService->createRoad(input);

    (void)store.save();
    store.close();
    roadService.reset();

    (void)store.open(projectDirectory);
    auto project = transforms.resolveProjectGeoreference(store.current().georeference);
    world.resetForProject(project);
    roadService.emplace(store, world,
        [this](const RoadServiceEvent& e) { events.push_back(e); });
    roadService->onProjectOpened();

    auto roads = roadService->listRoads();
    CHECK(roads.size() == 1);
    CHECK(roads[0].name == "Persistent Road");
}

TEST_CASE_FIXTURE(RoadServiceTestFixture, "update elevation profile") {
    CreateRoadInput input;
    input.name = "Elevation Road";
    input.sourcePoints = makeStraightPolyline(0.0, 0.0, 0.0, 100.0, 10);
    input.positionTolerance = 1.0;
    auto summary = roadService->createRoad(input);

    UpdateElevationInput elevInput;
    elevInput.roadId = summary.roadId;
    elevInput.stations = {0.0, 50.0, 100.0};
    elevInput.elevations = {0.0, 5.0, 10.0};
    (void)roadService->updateElevation(elevInput);

    auto details = roadService->getRoad(summary.roadId);
    REQUIRE(details.has_value());
    CHECK(details->hasElevationProfile);
    CHECK(details->elevationBreakpointCount == 3);
}

TEST_CASE_FIXTURE(RoadServiceTestFixture, "update superelevation profile") {
    CreateRoadInput input;
    input.name = "Super Road";
    input.sourcePoints = makeStraightPolyline(0.0, 0.0, 0.0, 100.0, 10);
    input.positionTolerance = 1.0;
    auto summary = roadService->createRoad(input);

    UpdateSuperelevationInput supInput;
    supInput.roadId = summary.roadId;
    supInput.stations = {0.0, 50.0, 100.0};
    supInput.superelevations = {0.0, 0.02, 0.0};
    (void)roadService->updateSuperelevation(supInput);

    auto details = roadService->getRoad(summary.roadId);
    REQUIRE(details.has_value());
    CHECK(details->hasSuperelevationProfile);
    CHECK(details->superelevationBreakpointCount == 3);
}

TEST_CASE_FIXTURE(RoadServiceTestFixture, "save and reopen preserves road") {
    CreateRoadInput input;
    input.name = "Persistent Road";
    input.sourcePoints = makeStraightPolyline(0.0, 0.0, 0.0, 200.0, 20);
    input.positionTolerance = 1.0;
    auto summary = roadService->createRoad(input);

    // Close the store and road service, then reopen.
    roadService.reset();
    store.close();

    (void)store.open(projectDirectory);
    auto project = transforms.resolveProjectGeoreference(store.current().georeference);
    world.resetForProject(project);
    roadService.emplace(store, world,
        [this](const RoadServiceEvent& e) { events.push_back(e); });
    roadService->onProjectOpened();

    // Verify the road survived save/reopen.
    auto roads = roadService->listRoads();
    REQUIRE(roads.size() == 1);
    CHECK(roads[0].roadId == summary.roadId);
    CHECK(roads[0].name == "Persistent Road");
    CHECK(roads[0].length > 0.0);

    // Verify road details are accessible after reopen.
    auto details = roadService->getRoad(summary.roadId);
    REQUIRE(details.has_value());
    CHECK(details->name == "Persistent Road");
    CHECK(details->alignmentSegmentCount >= 1);
}

TEST_CASE_FIXTURE(RoadServiceTestFixture, "road scene projection produces mesh data") {
    CreateRoadInput input;
    input.name = "Scene Road";
    input.sourcePoints = makeStraightPolyline(0.0, 0.0, 0.0, 100.0, 10);
    input.positionTolerance = 1.0;
    (void)roadService->createRoad(input);

    auto projection = roadService->roadSceneProjection();
    CHECK(projection.meshes.size() == 1);
    CHECK_FALSE(projection.meshes[0].vertices.empty());
    CHECK_FALSE(projection.meshes[0].indices.empty());
    CHECK_FALSE(projection.meshes[0].roadId.empty());

    // Verify vertices are in render-local coordinates (relative to origin).
    // The origin is (0,0,0) in this test, so vertices should be near the
    // source polyline coordinates.
    const auto& v0 = projection.meshes[0].vertices[0];
    CHECK(v0.x != 0.0f || v0.y != 0.0f);  // At least one coordinate is non-zero.
}

TEST_CASE_FIXTURE(RoadServiceTestFixture, "undo redo after reopen") {
    CreateRoadInput input;
    input.name = "Undo Road";
    input.sourcePoints = makeStraightPolyline(0.0, 0.0, 0.0, 100.0, 10);
    input.positionTolerance = 1.0;
    auto summary = roadService->createRoad(input);

    // Rename the road.
    (void)roadService->renameRoad(summary.roadId, "Renamed Road");
    auto roads = roadService->listRoads();
    REQUIRE(roads.size() == 1);
    CHECK(roads[0].name == "Renamed Road");

    // Undo the rename.
    roadService->undo(summary.roadId);
    roads = roadService->listRoads();
    REQUIRE(roads.size() == 1);
    CHECK(roads[0].name == "Undo Road");

    // Redo the rename.
    roadService->redo(summary.roadId);
    roads = roadService->listRoads();
    REQUIRE(roads.size() == 1);
    CHECK(roads[0].name == "Renamed Road");

    // Close and reopen, verify redo state is preserved.
    roadService.reset();
    store.close();

    (void)store.open(projectDirectory);
    auto project = transforms.resolveProjectGeoreference(store.current().georeference);
    world.resetForProject(project);
    roadService.emplace(store, world,
        [this](const RoadServiceEvent& e) { events.push_back(e); });
    roadService->onProjectOpened();

    roads = roadService->listRoads();
    REQUIRE(roads.size() == 1);
    CHECK(roads[0].name == "Renamed Road");
}

TEST_CASE_FIXTURE(RoadServiceTestFixture, "delete and undo delete after reopen") {
    CreateRoadInput input;
    input.name = "Delete Me";
    input.sourcePoints = makeStraightPolyline(0.0, 0.0, 0.0, 100.0, 10);
    input.positionTolerance = 1.0;
    auto summary = roadService->createRoad(input);

    // Delete the road.
    (void)roadService->deleteRoad(summary.roadId);
    auto roads = roadService->listRoads();
    CHECK(roads.empty());

    // Undo the delete.
    roadService->undo(summary.roadId);
    roads = roadService->listRoads();
    REQUIRE(roads.size() == 1);
    CHECK(roads[0].roadId == summary.roadId);

    // Close and reopen, verify the road survived.
    roadService.reset();
    store.close();

    (void)store.open(projectDirectory);
    auto project = transforms.resolveProjectGeoreference(store.current().georeference);
    world.resetForProject(project);
    roadService.emplace(store, world,
        [this](const RoadServiceEvent& e) { events.push_back(e); });
    roadService->onProjectOpened();

    roads = roadService->listRoads();
    REQUIRE(roads.size() == 1);
    CHECK(roads[0].roadId == summary.roadId);
    CHECK(roads[0].name == "Delete Me");
}

TEST_CASE_FIXTURE(RoadServiceTestFixture, "tessellation is deterministic across calls") {
    CreateRoadInput input;
    input.name = "Deterministic Road";
    input.sourcePoints = makeStraightPolyline(0.0, 0.0, 0.0, 100.0, 10);
    input.positionTolerance = 1.0;
    (void)roadService->createRoad(input);

    auto roads = roadService->listRoads();
    REQUIRE(roads.size() == 1);

    auto tess1 = roadService->getRoadTessellation(roads[0].roadId);
    auto tess2 = roadService->getRoadTessellation(roads[0].roadId);
    REQUIRE(tess1.has_value());
    REQUIRE(tess2.has_value());
    CHECK(tess1->crossSections.size() == tess2->crossSections.size());
    CHECK(tess1->indices.size() == tess2->indices.size());
}

} // namespace infraforge::application
