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
#include <set>
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

TEST_CASE_FIXTURE(RoadServiceTestFixture, "normal refit reuses persisted fitting contract") {
    for (const double tolerance : {0.2, 5.0}) {
        CreateRoadInput input;
        input.name = "Contract";
        input.sourcePoints = makeStraightPolyline(tolerance * 100.0, 0.0, 0.0, 100.0, 10);
        input.positionTolerance = tolerance;
        input.maxCurvature = 0.05;
        const auto created = roadService->createRoad(input);
        FitSourceInput refit{.roadId = created.roadId, .positionTolerance = std::nullopt,
            .maxCurvature = std::nullopt, .replaceMaxCurvature = false};
        const auto result = roadService->fitSource(refit);
        CHECK(result.roadId == created.roadId);
        const auto records = store.roads();
        const auto found = std::find_if(records.begin(), records.end(), [&](const auto& record) {
            return uuidTextFromRoadId(record.id) == created.roadId;
        });
        REQUIRE(found != records.end());
        CHECK(found->positionTolerance == doctest::Approx(tolerance));
        REQUIRE(found->maxCurvature.has_value());
        CHECK(*found->maxCurvature == doctest::Approx(0.05));
    }
}

TEST_CASE_FIXTURE(RoadServiceTestFixture, "explicit refit settings replace persisted fitting contract") {
    CreateRoadInput input;
    input.name = "Contract update";
    input.sourcePoints = makeStraightPolyline(0.0, 0.0, 0.0, 100.0, 10);
    input.positionTolerance = 0.2;
    input.maxCurvature = 0.05;
    const auto created = roadService->createRoad(input);

    FitSourceInput refit{.roadId = created.roadId, .positionTolerance = 2.5,
        .maxCurvature = 0.02, .replaceMaxCurvature = true};
    (void)roadService->fitSource(refit);

    const auto records = store.roads();
    const auto found = std::find_if(records.begin(), records.end(), [&](const auto& record) {
        return uuidTextFromRoadId(record.id) == created.roadId;
    });
    REQUIRE(found != records.end());
    CHECK(found->positionTolerance == doctest::Approx(2.5));
    REQUIRE(found->maxCurvature.has_value());
    CHECK(*found->maxCurvature == doctest::Approx(0.02));
}

TEST_CASE_FIXTURE(RoadServiceTestFixture, "refit changes tolerance without clearing max curvature") {
    CreateRoadInput input;
    input.name = "Independent fitting constraints";
    input.sourcePoints = makeStraightPolyline(0.0, 0.0, 0.0, 100.0, 10);
    input.positionTolerance = 1.0;
    input.maxCurvature = 0.05;
    const auto created = roadService->createRoad(input);

    FitSourceInput fitInput;
    fitInput.roadId = created.roadId;
    fitInput.positionTolerance = 2.0;
    (void)roadService->fitSource(fitInput);

    const auto details = roadService->getRoad(created.roadId);
    REQUIRE(details.has_value());
    CHECK(details->positionTolerance == doctest::Approx(2.0));
    REQUIRE(details->maxCurvature.has_value());
    CHECK(*details->maxCurvature == doctest::Approx(0.05));
}

TEST_CASE_FIXTURE(RoadServiceTestFixture, "refit can replace or explicitly clear max curvature independently") {
    CreateRoadInput input;
    input.name = "Independent curvature operations";
    input.sourcePoints = makeStraightPolyline(0.0, 0.0, 0.0, 100.0, 10);
    input.positionTolerance = 1.0;
    input.maxCurvature = 0.05;
    const auto created = roadService->createRoad(input);

    FitSourceInput fitInput1;
    fitInput1.roadId = created.roadId;
    fitInput1.maxCurvature = 0.02;
    fitInput1.replaceMaxCurvature = true;
    (void)roadService->fitSource(fitInput1);
    auto details = roadService->getRoad(created.roadId);
    REQUIRE(details.has_value());
    CHECK(details->positionTolerance == doctest::Approx(1.0));
    REQUIRE(details->maxCurvature.has_value());
    CHECK(*details->maxCurvature == doctest::Approx(0.02));

    FitSourceInput fitInput2;
    fitInput2.roadId = created.roadId;
    fitInput2.replaceMaxCurvature = true;
    (void)roadService->fitSource(fitInput2);
    details = roadService->getRoad(created.roadId);
    REQUIRE(details.has_value());
    CHECK(details->positionTolerance == doctest::Approx(1.0));
    CHECK_FALSE(details->maxCurvature.has_value());
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
    CHECK(details->controlPoints.size() == input.sourcePoints.size());
    CHECK(details->positionTolerance == doctest::Approx(1.0));
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
    REQUIRE(details->elevationBreakpoints.size() == 3);
    CHECK(details->elevationBreakpoints[0].station == doctest::Approx(0.0));
    CHECK(details->elevationBreakpoints[1].value == doctest::Approx(5.0));
    CHECK(details->elevationBreakpoints[2].station == doctest::Approx(100.0));
    CHECK(details->elevationBreakpoints[2].value == doctest::Approx(10.0));
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
    REQUIRE(details->superelevationBreakpoints.size() == 3);
    CHECK(details->superelevationBreakpoints[0].station == doctest::Approx(0.0));
    CHECK(details->superelevationBreakpoints[1].value == doctest::Approx(0.02));
    CHECK(details->superelevationBreakpoints[2].station == doctest::Approx(100.0));
}

TEST_CASE_FIXTURE(RoadServiceTestFixture, "update width profile persists and participates in undo") {
    CreateRoadInput input;
    input.name = "Variable Width Road";
    input.sourcePoints = makeStraightPolyline(0.0, 0.0, 0.0, 100.0, 10);
    input.positionTolerance = 1.0;
    auto summary = roadService->createRoad(input);

    UpdateWidthInput widthInput;
    widthInput.roadId = summary.roadId;
    widthInput.stations = {0.0, 50.0, 100.0};
    widthInput.leftWidths = {3.0, 5.0, 7.0};
    widthInput.rightWidths = {4.0, 3.0, 2.0};
    (void)roadService->updateWidth(widthInput);

    auto details = roadService->getRoad(summary.roadId);
    REQUIRE(details.has_value());
    REQUIRE(details->widthBreakpoints.size() == 3);
    CHECK(details->widthBreakpoints[1].leftWidth == doctest::Approx(5.0));
    CHECK(details->widthBreakpoints[1].rightWidth == doctest::Approx(3.0));

    const auto undone = roadService->undo(summary.roadId);
    REQUIRE(undone);
    details = roadService->getRoad(summary.roadId);
    REQUIRE(details.has_value());
    CHECK(details->widthBreakpoints.empty());
    REQUIRE(roadService->redo(summary.roadId));

    (void)store.save();
    roadService.reset();
    store.close();
    (void)store.open(projectDirectory);
    auto project = transforms.resolveProjectGeoreference(store.current().georeference);
    world.resetForProject(project);
    roadService.emplace(store, world,
        [this](const RoadServiceEvent& e) { events.push_back(e); });
    roadService->onProjectOpened();

    details = roadService->getRoad(summary.roadId);
    REQUIRE(details.has_value());
    REQUIRE(details->widthBreakpoints.size() == 3);
    CHECK(details->widthBreakpoints[2].leftWidth == doctest::Approx(7.0));
    CHECK(details->widthBreakpoints[2].rightWidth == doctest::Approx(2.0));

}

TEST_CASE_FIXTURE(RoadServiceTestFixture, "terrain conformance authors an undoable elevation profile") {
    CreateRoadInput input;
    input.name = "Terrain Conformance Road";
    input.sourcePoints = makeStraightPolyline(0.0, 0.0, 0.0, 100.0, 10);
    input.positionTolerance = 1.0;
    const auto summary = roadService->createRoad(input);

    ConformRoadToTerrainInput conform;
    conform.roadId = summary.roadId;
    conform.stationInterval = 20.0;
    conform.verticalOffset = 0.25;
    (void)roadService->conformToTerrain(conform,
        [](const AlignmentPoint& point) -> std::expected<double, std::string> {
            return point.easting * 0.1;
        });

    auto details = roadService->getRoad(summary.roadId);
    REQUIRE(details.has_value());
    REQUIRE(details->elevationBreakpoints.size() == 6);
    CHECK(details->elevationBreakpoints.front().value == doctest::Approx(0.25));
    CHECK(details->elevationBreakpoints.back().value == doctest::Approx(10.25));

    REQUIRE(roadService->undo(summary.roadId));
    details = roadService->getRoad(summary.roadId);
    REQUIRE(details.has_value());
    CHECK(details->elevationBreakpoints.empty());
}

TEST_CASE_FIXTURE(RoadServiceTestFixture, "terrain conformance is atomic when coverage is incomplete") {
    CreateRoadInput input;
    input.name = "Partial Coverage Road";
    input.sourcePoints = makeStraightPolyline(0.0, 0.0, 0.0, 100.0, 10);
    input.positionTolerance = 1.0;
    const auto summary = roadService->createRoad(input);

    ConformRoadToTerrainInput conform;
    conform.roadId = summary.roadId;
    conform.stationInterval = 20.0;
    bool threw = false;
    try {
        (void)roadService->conformToTerrain(conform,
            [](const AlignmentPoint& point) -> std::expected<double, std::string> {
                if (point.easting > 50.0) return std::unexpected("outside test coverage");
                return 2.0;
            });
    } catch (const CommandFailure& failure) {
        threw = true;
        CHECK(failure.code() == CommandFailureCode::InvalidArgument);
        CHECK(std::string{failure.what()}.find("station") != std::string::npos);
    }
    CHECK(threw);
    const auto details = roadService->getRoad(summary.roadId);
    REQUIRE(details.has_value());
    CHECK(details->elevationBreakpoints.empty());
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
    auto summary = roadService->createRoad(input);

    auto projection = roadService->roadSceneProjection();
    REQUIRE(projection.meshes.size() == 1);
    CHECK(projection.meshes[0].roadId == summary.roadId);
    CHECK_FALSE(projection.meshes[0].vertices.empty());
    CHECK_FALSE(projection.meshes[0].indices.empty());
    CHECK_FALSE(projection.meshes[0].roadId.empty());

    // Verify vertices are in render-local coordinates (relative to origin).
    // The origin is (0,0,0) in this test, so vertices should be near the
    // source polyline coordinates.
    const auto& v0 = projection.meshes[0].vertices[0];
    // At least one coordinate is non-zero.
    const bool hasNonZeroCoord = (v0.x != 0.0f) || (v0.y != 0.0f);
    CHECK(hasNonZeroCoord);
}

TEST_CASE_FIXTURE(RoadServiceTestFixture, "road scene normals use superelevation angle") {
    CreateRoadInput input;
    input.name = "Banked Scene Road";
    input.sourcePoints = makeStraightPolyline(0.0, 0.0, 0.0, 100.0, 10);
    input.positionTolerance = 1.0;
    auto summary = roadService->createRoad(input);

    UpdateSuperelevationInput bank;
    bank.roadId = summary.roadId;
    bank.stations = {0.0, 100.0};
    bank.superelevations = {0.1, 0.1};
    (void)roadService->updateSuperelevation(bank);

    const auto projection = roadService->roadSceneProjection();
    REQUIRE(projection.meshes.size() == 1);
    REQUIRE_FALSE(projection.meshes[0].vertices.empty());
    const auto& normal = projection.meshes[0].vertices.front();
    CHECK(normal.nx == doctest::Approx(0.0).epsilon(1e-5));
    CHECK(normal.ny == doctest::Approx(-std::sin(0.1)).epsilon(1e-5));
    CHECK(normal.nz == doctest::Approx(std::cos(0.1)).epsilon(1e-5));
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
    REQUIRE(roadService->undo(summary.roadId));
    roads = roadService->listRoads();
    REQUIRE(roads.size() == 1);
    CHECK(roads[0].name == "Undo Road");

    // Redo the rename.
    REQUIRE(roadService->redo(summary.roadId));
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
    REQUIRE(roadService->undo(summary.roadId));
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

TEST_CASE_FIXTURE(RoadServiceTestFixture, "multiple roads coexist") {
    // Create several roads and verify they all coexist.
    for (int i = 0; i < 5; ++i) {
        CreateRoadInput input;
        input.name = "Road " + std::to_string(i);
        input.sourcePoints = makeStraightPolyline(
            static_cast<double>(i) * 100.0, 0.0, 0.0, 100.0, 10);
        input.positionTolerance = 1.0;
        (void)roadService->createRoad(input);
    }

    auto roads = roadService->listRoads();
    CHECK(roads.size() == 5);

    // Verify each road has a unique ID.
    std::set<std::string> ids;
    for (const auto& r : roads) {
        ids.insert(r.roadId);
    }
    CHECK(ids.size() == 5);

    // Verify the scene projection includes all roads.
    auto projection = roadService->roadSceneProjection();
    CHECK(projection.meshes.size() == 5);
}

TEST_CASE_FIXTURE(RoadServiceTestFixture, "long road spans multiple chunks") {
    // Create a road that is long enough to potentially span multiple chunks.
    CreateRoadInput input;
    input.name = "Long Road";
    input.sourcePoints = makeStraightPolyline(0.0, 0.0, 0.0, 5000.0, 50);
    input.positionTolerance = 1.0;
    auto summary = roadService->createRoad(input);

    // Verify the road was created and has a reasonable length.
    CHECK(summary.length > 4000.0);

    // Verify the scene projection produces valid mesh data for the long road.
    auto projection = roadService->roadSceneProjection();
    REQUIRE(projection.meshes.size() > 1);
    std::set<std::pair<std::int64_t, std::int64_t>> chunkKeys;
    for (const auto& mesh : projection.meshes) {
        CHECK(mesh.roadId == summary.roadId);
        CHECK_FALSE(mesh.vertices.empty());
        CHECK_FALSE(mesh.indices.empty());
        CHECK(chunkKeys.emplace(mesh.chunkX, mesh.chunkY).second);
    }
    for (std::size_t i = 1; i < projection.meshes.size(); ++i) {
        const auto& previous = projection.meshes[i - 1].vertices;
        const auto& next = projection.meshes[i].vertices;
        REQUIRE(previous.size() >= 2); REQUIRE(next.size() >= 2);
        CHECK(previous[previous.size() - 2].x == next[0].x);
        CHECK(previous[previous.size() - 2].y == next[0].y);
        CHECK(previous.back().x == next[1].x);
        CHECK(previous.back().y == next[1].y);
    }

    // Verify tessellation produces a reasonable number of samples.
    auto tess = roadService->getRoadTessellation(summary.roadId);
    REQUIRE(tess.has_value());
    CHECK(tess->crossSections.size() > 10);
}

TEST_CASE_FIXTURE(RoadServiceTestFixture, "road scene projection is empty with no roads") {
    auto projection = roadService->roadSceneProjection();
    CHECK(projection.meshes.empty());
    CHECK(projection.revision > 0);
}

TEST_CASE_FIXTURE(RoadServiceTestFixture, "refit clamps breakpoints when road is shortened and preserves within-range breakpoints when lengthened") {
    CreateRoadInput input;
    input.name = "Refit Clamping Road";
    input.sourcePoints = makeStraightPolyline(0.0, 0.0, 0.0, 100.0, 2);
    input.positionTolerance = 1.0;
    const auto summary = roadService->createRoad(input);

    // Set elevation: 0 -> 10, 50 -> 15, 100 -> 20
    UpdateElevationInput elevInput;
    elevInput.roadId = summary.roadId;
    elevInput.stations = {0.0, 50.0, 100.0};
    elevInput.elevations = {10.0, 15.0, 20.0};
    (void)roadService->updateElevation(elevInput);

    // Set superelevation: 0 -> 0.0, 50 -> 0.04, 100 -> 0.08
    UpdateSuperelevationInput superInput;
    superInput.roadId = summary.roadId;
    superInput.stations = {0.0, 50.0, 100.0};
    superInput.superelevations = {0.0, 0.04, 0.08};
    (void)roadService->updateSuperelevation(superInput);

    // Set width: 0 -> 3.5, 50 -> 4.0, 100 -> 5.0
    UpdateWidthInput widthInput;
    widthInput.roadId = summary.roadId;
    widthInput.stations = {0.0, 50.0, 100.0};
    widthInput.leftWidths = {3.5, 4.0, 5.0};
    widthInput.rightWidths = {3.5, 4.0, 5.0};
    (void)roadService->updateWidth(widthInput);

    auto details = roadService->getRoad(summary.roadId);
    REQUIRE(details.has_value());
    REQUIRE(details->controlPoints.size() == 2);

    // 1. Shorten the road: move last control point to (0, 70)
    MoveControlInput moveInput;
    moveInput.roadId = summary.roadId;
    moveInput.controlIndex = static_cast<std::uint32_t>(details->controlPoints.size() - 1);
    moveInput.position = AlignmentPoint{0.0, 70.0};
    const auto shortenedSummary = roadService->moveControl(moveInput);
    CHECK(shortenedSummary.length == doctest::Approx(70.0));

    details = roadService->getRoad(summary.roadId);
    REQUIRE(details.has_value());

    // Verify all 3 profiles were clamped to new alignment length and no breakpoint > 70 exists
    REQUIRE(details->elevationBreakpoints.size() == 3);
    CHECK(details->elevationBreakpoints[0].station == doctest::Approx(0.0));
    CHECK(details->elevationBreakpoints[1].station == doctest::Approx(50.0));
    CHECK(details->elevationBreakpoints[2].station == doctest::Approx(70.0));
    CHECK(details->elevationBreakpoints[2].value == doctest::Approx(17.0)); // interpolated between 15 and 20

    REQUIRE(details->superelevationBreakpoints.size() == 3);
    CHECK(details->superelevationBreakpoints[0].station == doctest::Approx(0.0));
    CHECK(details->superelevationBreakpoints[1].station == doctest::Approx(50.0));
    CHECK(details->superelevationBreakpoints[2].station == doctest::Approx(70.0));
    CHECK(details->superelevationBreakpoints[2].value == doctest::Approx(0.056));

    REQUIRE(details->widthBreakpoints.size() == 3);
    CHECK(details->widthBreakpoints[0].station == doctest::Approx(0.0));
    CHECK(details->widthBreakpoints[1].station == doctest::Approx(50.0));
    CHECK(details->widthBreakpoints[2].station == doctest::Approx(70.0));
    CHECK(details->widthBreakpoints[2].leftWidth == doctest::Approx(4.4));
    CHECK(details->widthBreakpoints[2].rightWidth == doctest::Approx(4.4));

    // 2. Lengthen the road: move last control point to (0, 120)
    moveInput.position = AlignmentPoint{0.0, 120.0};
    const auto lengthenedSummary = roadService->moveControl(moveInput);
    CHECK(lengthenedSummary.length == doctest::Approx(120.0));

    details = roadService->getRoad(summary.roadId);
    REQUIRE(details.has_value());
    for (const auto& bp : details->elevationBreakpoints) {
        CHECK(bp.station <= lengthenedSummary.length);
    }
    for (const auto& bp : details->superelevationBreakpoints) {
        CHECK(bp.station <= lengthenedSummary.length);
    }
    for (const auto& bp : details->widthBreakpoints) {
        CHECK(bp.station <= lengthenedSummary.length);
    }
}

TEST_CASE_FIXTURE(RoadServiceTestFixture, "terrain conformance is independent of existing profiles") {
    CreateRoadInput input;
    input.name = "Independent Conformance Road";
    input.sourcePoints = makeStraightPolyline(0.0, 0.0, 0.0, 100.0, 10);
    input.positionTolerance = 1.0;
    const auto summary = roadService->createRoad(input);

    // Give it a dense, complex old elevation profile
    UpdateElevationInput elevInput;
    elevInput.roadId = summary.roadId;
    elevInput.stations = {0.0, 15.0, 32.0, 48.0, 67.0, 85.0, 100.0};
    elevInput.elevations = {5.0, 12.0, 8.0, 14.0, 9.0, 11.0, 7.0};
    (void)roadService->updateElevation(elevInput);

    // Give it non-default banking and width
    UpdateSuperelevationInput superInput;
    superInput.roadId = summary.roadId;
    superInput.stations = {0.0, 50.0, 100.0};
    superInput.superelevations = {0.0, 0.05, 0.0};
    (void)roadService->updateSuperelevation(superInput);

    UpdateWidthInput widthInput;
    widthInput.roadId = summary.roadId;
    widthInput.stations = {0.0, 50.0, 100.0};
    widthInput.leftWidths = {3.5, 6.0, 3.5};
    widthInput.rightWidths = {3.5, 6.0, 3.5};
    (void)roadService->updateWidth(widthInput);

    // Conforming to terrain with stationInterval=20 must produce exactly the 6 canonical stations
    // based on alignment alone, regardless of the old 7 elevation stations or width tapers.
    ConformRoadToTerrainInput conform;
    conform.roadId = summary.roadId;
    conform.stationInterval = 20.0;
    conform.verticalOffset = 0.5;
    (void)roadService->conformToTerrain(conform,
        [](const AlignmentPoint& point) -> std::expected<double, std::string> {
            return point.easting * 0.1;
        });

    auto details = roadService->getRoad(summary.roadId);
    REQUIRE(details.has_value());
    REQUIRE(details->elevationBreakpoints.size() == 6);
    CHECK(details->elevationBreakpoints[0].station == doctest::Approx(0.0));
    CHECK(details->elevationBreakpoints[1].station == doctest::Approx(20.0));
    CHECK(details->elevationBreakpoints[2].station == doctest::Approx(40.0));
    CHECK(details->elevationBreakpoints[3].station == doctest::Approx(60.0));
    CHECK(details->elevationBreakpoints[4].station == doctest::Approx(80.0));
    CHECK(details->elevationBreakpoints[5].station == doctest::Approx(100.0));
}

TEST_CASE_FIXTURE(RoadServiceTestFixture, "sparse source elevations map to correct cumulative stations") {
    CreateRoadInput input;
    input.name = "Sparse Elevations Road";
    // Polyline of 3 points: (0,0) -> (0,10) -> (0,20)
    input.sourcePoints = {
        AlignmentPoint{0.0, 0.0},
        AlignmentPoint{0.0, 10.0},
        AlignmentPoint{0.0, 20.0}
    };
    // Sparse elevations: station 0 has 100.0, station 10 is nullopt, station 20 has 120.0
    input.sourceElevations = {100.0, std::nullopt, 120.0};
    input.positionTolerance = 1.0;
    const auto summary = roadService->createRoad(input);

    auto details = roadService->getRoad(summary.roadId);
    REQUIRE(details.has_value());
    REQUIRE(details->elevationBreakpoints.size() == 2);
    CHECK(details->elevationBreakpoints[0].station == doctest::Approx(0.0));
    CHECK(details->elevationBreakpoints[0].value == doctest::Approx(100.0));
    CHECK(details->elevationBreakpoints[1].station == doctest::Approx(20.0)); // Must be 20.0, NOT 10.0!
    CHECK(details->elevationBreakpoints[1].value == doctest::Approx(120.0));
}

TEST_CASE_FIXTURE(RoadServiceTestFixture, "terrain conformance station bounds and atomicity") {
    CreateRoadInput input;
    input.name = "Conformance Bounds Road";
    input.sourcePoints = makeStraightPolyline(0.0, 0.0, 0.0, 100.0, 2);
    input.positionTolerance = 1.0;
    const auto summary = roadService->createRoad(input);

    UpdateElevationInput elevInput;
    elevInput.roadId = summary.roadId;
    elevInput.stations = {0.0, 100.0};
    elevInput.elevations = {5.0, 5.0};
    (void)roadService->updateElevation(elevInput);

    auto detailsBefore = roadService->getRoad(summary.roadId);
    REQUIRE(detailsBefore.has_value());
    REQUIRE(detailsBefore->elevationBreakpoints.size() == 2);

    // 1. Pathological request with tiny interval (100m / 0.005 = 20,000 samples > 10,000)
    // must be rejected quickly without huge allocation or executor lockup
    ConformRoadToTerrainInput conformExcessive;
    conformExcessive.roadId = summary.roadId;
    conformExcessive.stationInterval = 0.005;
    conformExcessive.verticalOffset = 0.1;
    CHECK_THROWS_AS(
        (void)roadService->conformToTerrain(conformExcessive,
            [](const AlignmentPoint&) -> std::expected<double, std::string> {
                return 0.0;
            }),
        infraforge::application::CommandFailure);

    // 2. Ultra-tiny interval (1e-12) must reject immediately without floating-point infinite loop
    ConformRoadToTerrainInput conformTiny;
    conformTiny.roadId = summary.roadId;
    conformTiny.stationInterval = 1e-12;
    conformTiny.verticalOffset = 0.1;
    CHECK_THROWS_AS(
        (void)roadService->conformToTerrain(conformTiny,
            [](const AlignmentPoint&) -> std::expected<double, std::string> {
                return 0.0;
            }),
        infraforge::application::CommandFailure);

    // 3. Atomicity: rejected conformance must NOT mutate the road
    auto detailsAfter = roadService->getRoad(summary.roadId);
    REQUIRE(detailsAfter.has_value());
    REQUIRE(detailsAfter->elevationBreakpoints.size() == 2);
    CHECK(detailsAfter->elevationBreakpoints[0].value == doctest::Approx(5.0));
    CHECK(detailsAfter->elevationBreakpoints[1].value == doctest::Approx(5.0));
}

TEST_CASE_FIXTURE(RoadServiceTestFixture, "terrain conformance includes boundaries and exact end station with strictly increasing stations") {
    CreateRoadInput input;
    input.name = "Multi-segment Conformance Road";
    input.sourcePoints = {
        AlignmentPoint{0.0, 0.0},
        AlignmentPoint{0.0, 33.0},
        AlignmentPoint{0.0, 100.0}
    };
    input.protectedAnchorIndices = {1};
    input.positionTolerance = 1.0;
    const auto summary = roadService->createRoad(input);
    REQUIRE(summary.alignmentSegmentCount >= 2);

    ConformRoadToTerrainInput conform;
    conform.roadId = summary.roadId;
    conform.stationInterval = 20.0;
    conform.verticalOffset = 0.5;
    (void)roadService->conformToTerrain(conform,
        [](const AlignmentPoint& pt) -> std::expected<double, std::string> {
            return pt.northing * 0.1;
        });

    auto details = roadService->getRoad(summary.roadId);
    REQUIRE(details.has_value());
    const auto& bps = details->elevationBreakpoints;
    REQUIRE(bps.size() >= 4);

    CHECK(bps.front().station == doctest::Approx(0.0));
    CHECK(bps.back().station == doctest::Approx(100.0));

    bool foundBoundary = false;
    for (const auto& bp : bps) {
        if (std::abs(bp.station - 33.0) < 1e-4) {
            foundBoundary = true;
            break;
        }
    }
    CHECK(foundBoundary);

    for (std::size_t i = 1; i < bps.size(); ++i) {
        CHECK(bps[i].station > bps[i - 1].station);
    }
}

TEST_CASE_FIXTURE(RoadServiceTestFixture, "undo and redo restore exact unclamped and clamped profile states across road shortening") {
    CreateRoadInput input;
    input.name = "Clamping Undo Redo Road";
    input.sourcePoints = makeStraightPolyline(0.0, 0.0, 0.0, 100.0, 2);
    input.positionTolerance = 1.0;
    const auto summary = roadService->createRoad(input);

    UpdateElevationInput elevInput;
    elevInput.roadId = summary.roadId;
    elevInput.stations = {0.0, 50.0, 100.0};
    elevInput.elevations = {10.0, 15.0, 20.0};
    (void)roadService->updateElevation(elevInput);

    UpdateSuperelevationInput superInput;
    superInput.roadId = summary.roadId;
    superInput.stations = {0.0, 50.0, 100.0};
    superInput.superelevations = {0.0, 0.04, 0.08};
    (void)roadService->updateSuperelevation(superInput);

    UpdateWidthInput widthInput;
    widthInput.roadId = summary.roadId;
    widthInput.stations = {0.0, 50.0, 100.0};
    widthInput.leftWidths = {3.0, 4.0, 5.0};
    widthInput.rightWidths = {3.0, 4.0, 5.0};
    (void)roadService->updateWidth(widthInput);

    // Shorten road: move point 1 from (0,100) to (0,70)
    MoveControlInput moveInput;
    moveInput.roadId = summary.roadId;
    moveInput.controlIndex = 1;
    moveInput.position = AlignmentPoint{0.0, 70.0};
    (void)roadService->moveControl(moveInput);

    auto clampedDetails = roadService->getRoad(summary.roadId);
    REQUIRE(clampedDetails.has_value());
    CHECK(clampedDetails->elevationBreakpoints.back().station == doctest::Approx(70.0));
    CHECK(clampedDetails->superelevationBreakpoints.back().station == doctest::Approx(70.0));
    CHECK(clampedDetails->widthBreakpoints.back().station == doctest::Approx(70.0));

    // Undo: must restore unclamped 100m alignment and exact original breakpoints
    REQUIRE(roadService->undo(summary.roadId));
    auto undoneDetails = roadService->getRoad(summary.roadId);
    REQUIRE(undoneDetails.has_value());
    REQUIRE(undoneDetails->elevationBreakpoints.size() == 3);
    CHECK(undoneDetails->elevationBreakpoints[2].station == doctest::Approx(100.0));
    CHECK(undoneDetails->elevationBreakpoints[2].value == doctest::Approx(20.0));
    REQUIRE(undoneDetails->superelevationBreakpoints.size() == 3);
    CHECK(undoneDetails->superelevationBreakpoints[2].station == doctest::Approx(100.0));
    CHECK(undoneDetails->superelevationBreakpoints[2].value == doctest::Approx(0.08));
    REQUIRE(undoneDetails->widthBreakpoints.size() == 3);
    CHECK(undoneDetails->widthBreakpoints[2].station == doctest::Approx(100.0));
    CHECK(undoneDetails->widthBreakpoints[2].leftWidth == doctest::Approx(5.0));

    // Redo: must restore clamped state
    REQUIRE(roadService->redo(summary.roadId));
    auto redoneDetails = roadService->getRoad(summary.roadId);
    REQUIRE(redoneDetails.has_value());
    CHECK(redoneDetails->elevationBreakpoints.back().station == doctest::Approx(70.0));
    CHECK(redoneDetails->superelevationBreakpoints.back().station == doctest::Approx(70.0));
    CHECK(redoneDetails->widthBreakpoints.back().station == doctest::Approx(70.0));
}



TEST_CASE_FIXTURE(RoadServiceTestFixture, "createStraightRoad creates canonical line segment and persists") {
    CreateStraightRoadInput input;
    input.name = "Direct Straight 1";
    input.start = AlignmentPoint{0.0, 0.0};
    input.end = AlignmentPoint{120.0, 50.0};

    const auto summary = roadService->createStraightRoad(input);
    CHECK(summary.name == "Direct Straight 1");
    CHECK(summary.length == doctest::Approx(130.0));
    CHECK(events.size() == 1);
    CHECK(events[0].kind == RoadServiceEvent::Kind::Created);

    auto details = roadService->getRoad(summary.roadId);
    REQUIRE(details.has_value());
    CHECK(details->name == "Direct Straight 1");
    CHECK(details->length == doctest::Approx(130.0));

    // Reopen store to verify persistence
    roadService.reset();
    store.close();
    (void)store.open(projectDirectory);
    auto project = transforms.resolveProjectGeoreference(store.current().georeference);
    world.resetForProject(project);
    roadService.emplace(store, world, [](const RoadServiceEvent&) {});
    roadService->onProjectOpened();

    auto reopened = roadService->getRoad(summary.roadId);
    REQUIRE(reopened.has_value());
    CHECK(reopened->length == doctest::Approx(130.0));
}

TEST_CASE_FIXTURE(RoadServiceTestFixture, "createArcRoad creates canonical circular arc and supports undo/redo") {
    CreateArcRoadInput input;
    input.name = "Direct Arc 1";
    input.p0 = AlignmentPoint{100.0, 0.0};
    input.p1 = AlignmentPoint{0.0, 100.0};
    input.p2 = AlignmentPoint{-100.0, 0.0};

    const auto summary = roadService->createArcRoad(input);
    CHECK(summary.name == "Direct Arc 1");
    const double expectedLen = 100.0 * 3.14159265358979323846;
    CHECK(summary.length == doctest::Approx(expectedLen));

    // Reject collinear points
    CreateArcRoadInput collinearInput;
    collinearInput.name = "Collinear Arc";
    collinearInput.p0 = AlignmentPoint{0.0, 0.0};
    collinearInput.p1 = AlignmentPoint{50.0, 50.0};
    collinearInput.p2 = AlignmentPoint{100.0, 100.0};
    CHECK_THROWS_AS((void)roadService->createArcRoad(collinearInput), CommandFailure);

    // Verify undo / redo
    REQUIRE(roadService->undo(summary.roadId));
    auto undone = roadService->getRoad(summary.roadId);
    CHECK(!undone.has_value());

    REQUIRE(roadService->redo(summary.roadId));
    auto redone = roadService->getRoad(summary.roadId);
    REQUIRE(redone.has_value());
    CHECK(redone->length == doctest::Approx(expectedLen));
}

TEST_CASE_FIXTURE(RoadServiceTestFixture, "createClothoidRoad creates canonical spiral and persists") {
    CreateClothoidRoadInput input;
    input.name = "Direct Clothoid 1";
    input.start = AlignmentPoint{50.0, 50.0};
    input.startHeading = 0.5;
    input.startCurvature = 0.0;
    input.endCurvature = 0.02;
    input.length = 80.0;

    const auto summary = roadService->createClothoidRoad(input);
    CHECK(summary.name == "Direct Clothoid 1");
    CHECK(summary.length == doctest::Approx(80.0));

    auto details = roadService->getRoad(summary.roadId);
    REQUIRE(details.has_value());
    CHECK(details->length == doctest::Approx(80.0));
}

} // namespace infraforge::application
