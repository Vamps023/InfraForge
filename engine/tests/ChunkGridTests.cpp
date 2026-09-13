#include <doctest/doctest.h>

#include "TestHelpers.hpp"
#include "infraforge/domain/geo/GeoTypes.hpp"
#include "infraforge/domain/world/ChunkGrid.hpp"
#include "infraforge/domain/world/WorldPartitionError.hpp"

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace world = infraforge::domain::world;
namespace geo = infraforge::domain::geo;

using infraforge::testhelpers::captureException;

namespace {

geo::ResolvedUnit metreUnit() {
    return geo::ResolvedUnit{"metre", 1.0};
}

geo::ResolvedUnit usSurveyFootUnit() {
    // The exact US survey foot factor carried by the Geo domain's unit
    // database (1200/3937 metres per foot).
    return geo::ResolvedUnit{"foot_us", 1200.0 / 3937.0};
}

} // namespace

TEST_SUITE("logical chunk grid") {
    TEST_CASE("the production path converts the physical 1 km default through the project unit") {
        const auto metreGrid = world::ChunkGrid::fromMetreEdge(metreUnit());
        CHECK(metreGrid.chunkSize() == world::defaultChunkEdgeMetres);
        CHECK(metreGrid.chunkSize() == 1000.0);
        CHECK(metreGrid.config() == world::ChunkGridConfig{world::defaultChunkEdgeMetres});

        // A US-survey-foot project must get physically identical 1 km
        // cells, not 1000-foot (~305 m) cells.
        const auto footUnit = usSurveyFootUnit();
        const auto footGrid = world::ChunkGrid::fromMetreEdge(footUnit);
        CHECK(footGrid.chunkSize() == doctest::Approx(1000.0 * 3937.0 / 1200.0));
        CHECK(footGrid.chunkSize() * footUnit.toMetre == doctest::Approx(1000.0));

        // The same physical location lands in the same chunk in both
        // projects: one full cell edge east is exactly 1 km in each.
        CHECK(metreGrid.chunkAt(world::defaultChunkEdgeMetres, 0.0) == world::ChunkCoord{1, 0});
        CHECK(footGrid.chunkAt(footGrid.chunkSize(), 0.0) == world::ChunkCoord{1, 0});
        const auto footPositionOneKmNorth = 1000.0 / footUnit.toMetre;
        CHECK(footGrid.chunkAt(0.0, footPositionOneKmNorth) == world::ChunkCoord{0, 1});
        CHECK(footGrid.chunkAt(-1.0, -1.0) == world::ChunkCoord{-1, -1});
    }

    TEST_CASE("broken linear units are rejected") {
        const auto zeroFactor = captureException<world::WorldPartitionError>(
            [] { static_cast<void>(world::ChunkGrid::fromMetreEdge(geo::ResolvedUnit{"bad", 0.0})); });
        REQUIRE(zeroFactor.has_value());
        CHECK(zeroFactor->code() == world::WorldPartitionErrorCode::InvalidLinearUnit);

        const auto negativeFactor = captureException<world::WorldPartitionError>(
            [] { static_cast<void>(world::ChunkGrid::fromMetreEdge(geo::ResolvedUnit{"bad", -1.0})); });
        REQUIRE(negativeFactor.has_value());
        CHECK(negativeFactor->code() == world::WorldPartitionErrorCode::InvalidLinearUnit);

        const auto nanFactor = captureException<world::WorldPartitionError>([] {
            static_cast<void>(world::ChunkGrid::fromMetreEdge(
                geo::ResolvedUnit{"bad", std::numeric_limits<double>::quiet_NaN()}));
        });
        REQUIRE(nanFactor.has_value());
        CHECK(nanFactor->code() == world::WorldPartitionErrorCode::InvalidLinearUnit);
    }

    TEST_CASE("chunk identity uses floor division, not truncation toward zero") {
        const world::ChunkGrid grid{world::ChunkGrid::fromMetreEdge(metreUnit())};

        // Positive coordinates.
        CHECK(grid.chunkAt(0.0, 0.0) == world::ChunkCoord{0, 0});
        CHECK(grid.chunkAt(999.999, 999.999) == world::ChunkCoord{0, 0});

        // Exactly on a positive boundary: belongs to the higher cell.
        CHECK(grid.chunkAt(1000.0, 1000.0) == world::ChunkCoord{1, 1});

        // Negative coordinates floor to the mathematically lower cell.
        CHECK(grid.chunkAt(-1.0, -1.0) == world::ChunkCoord{-1, -1});
        CHECK(grid.chunkAt(-999.999, -0.001) == world::ChunkCoord{-1, -1});
        CHECK(grid.chunkAt(-0.001, -0.001) == world::ChunkCoord{-1, -1});

        // Exactly on a negative boundary.
        CHECK(grid.chunkAt(-1000.0, -2000.0) == world::ChunkCoord{-1, -2});
        CHECK(grid.chunkAt(-1000.001, -1000.0) == world::ChunkCoord{-2, -1});

        // Mixed signs.
        CHECK(grid.chunkAt(2500.0, -2500.0) == world::ChunkCoord{2, -3});
    }

    TEST_CASE("bounds fully inside one chunk select exactly that chunk") {
        const world::ChunkGrid grid{world::ChunkGrid::fromMetreEdge(metreUnit())};

        const auto inside = world::SpatialBounds::ofEdges(100.0, 200.0, 300.0, 400.0);
        CHECK(grid.chunksIntersecting(inside) == std::vector<world::ChunkCoord>{{0, 0}});

        const auto negativeInside = world::SpatialBounds::ofEdges(-2500.0, -2600.0, -2100.0, -2200.0);
        CHECK(grid.chunksIntersecting(negativeInside) == std::vector<world::ChunkCoord>{{-3, -3}});
    }

    TEST_CASE("bounds crossing a boundary select both adjacent chunks") {
        const world::ChunkGrid grid{world::ChunkGrid::fromMetreEdge(metreUnit())};

        const auto crossing = world::SpatialBounds::ofEdges(800.0, 100.0, 1200.0, 300.0);
        CHECK(grid.chunksIntersecting(crossing)
            == std::vector<world::ChunkCoord>{{0, 0}, {1, 0}});

        const auto negativeCrossing = world::SpatialBounds::ofEdges(-1200.0, -300.0, -800.0, -100.0);
        CHECK(grid.chunksIntersecting(negativeCrossing)
            == std::vector<world::ChunkCoord>{{-2, -1}, {-1, -1}});
    }

    TEST_CASE("bounds spanning many chunks enumerate deterministically") {
        const world::ChunkGrid grid{world::ChunkGrid::fromMetreEdge(metreUnit())};

        // A 3 km x 2 km area (inset from the cell edges) covering cells
        // {-1,0,1} x {0,1} in lexicographic order.
        const auto block = world::SpatialBounds::ofEdges(-999.0, 1.0, 1999.0, 1999.0);
        CHECK(grid.chunksIntersecting(block)
            == std::vector<world::ChunkCoord>{
                {-1, 0}, {-1, 1}, {0, 0}, {0, 1}, {1, 0}, {1, 1}});

        // Far-apart remote coordinates work identically.
        const auto remote = world::SpatialBounds::ofEdges(-100500.0, 9900.0, -99000.0, 10999.0);
        const auto remoteChunks = grid.chunksIntersecting(remote);
        REQUIRE(remoteChunks.size() == 6);
        CHECK(remoteChunks[0] == world::ChunkCoord{-101, 9});
        CHECK(remoteChunks[1] == world::ChunkCoord{-101, 10});
        CHECK(remoteChunks[5] == world::ChunkCoord{-99, 10});
    }

    TEST_CASE("bounds exactly on chunk boundaries touch both adjacent cells") {
        const world::ChunkGrid grid{world::ChunkGrid::fromMetreEdge(metreUnit())};

        // Closed bounds ending exactly on the 1 km edge stay conservative:
        // the shared-edge cell is touched too, while the position on that
        // edge belongs only to the higher cell.
        const auto edgeAligned = world::SpatialBounds::ofEdges(0.0, 0.0, 1000.0, 1000.0);
        CHECK(grid.chunksIntersecting(edgeAligned)
            == std::vector<world::ChunkCoord>{{0, 0}, {0, 1}, {1, 0}, {1, 1}});
        CHECK(grid.chunkAt(1000.0, 1000.0) == world::ChunkCoord{1, 1});
    }

    TEST_CASE("empty bounds select no chunks") {
        const world::ChunkGrid grid{world::ChunkGrid::fromMetreEdge(metreUnit())};

        CHECK(grid.chunksIntersecting(world::SpatialBounds::empty()).empty());
    }

    TEST_CASE("chunk size is configurable through the grid config") {
        const world::ChunkGrid fine{world::ChunkGridConfig{250.0}};
        const world::ChunkGrid coarse{world::ChunkGridConfig{4000.0}};

        CHECK(fine.chunkAt(1000.0, -1000.0) == world::ChunkCoord{4, -4});
        CHECK(coarse.chunkAt(1000.0, -1000.0) == world::ChunkCoord{0, -1});

        // The same bounds cover more cells on a finer grid.
        const auto area = world::SpatialBounds::ofEdges(0.0, 0.0, 1000.0, 1000.0);
        CHECK(fine.chunksIntersecting(area).size() == 25);
        CHECK(coarse.chunksIntersecting(area).size() == 1);
    }

    TEST_CASE("chunk bounds are the cell footprint") {
        const world::ChunkGrid grid{world::ChunkGrid::fromMetreEdge(metreUnit())};

        CHECK(grid.chunkBounds({2, -1})
            == world::SpatialBounds::ofEdges(2000.0, -1000.0, 3000.0, 0.0));
        CHECK(grid.chunkBounds({-1, -1})
            == world::SpatialBounds::ofEdges(-1000.0, -1000.0, 0.0, 0.0));

        // Footprint membership matches chunkAt boundary semantics: the
        // shared edge position lives in the higher cell.
        const auto footprint = grid.chunkBounds({0, 0});
        CHECK(footprint.contains(0.0, 0.0));
        CHECK(grid.chunkAt(1000.0, 500.0) == world::ChunkCoord{1, 0});
    }

    TEST_CASE("invalid chunk sizes are rejected") {
        const auto zero = captureException<world::WorldPartitionError>(
            [] { world::ChunkGrid bad{world::ChunkGridConfig{0.0}}; });
        REQUIRE(zero.has_value());
        CHECK(zero->code() == world::WorldPartitionErrorCode::InvalidChunkSize);

        const auto negative = captureException<world::WorldPartitionError>(
            [] { world::ChunkGrid bad{world::ChunkGridConfig{-1.0}}; });
        REQUIRE(negative.has_value());
        CHECK(negative->code() == world::WorldPartitionErrorCode::InvalidChunkSize);

        const auto nan = captureException<world::WorldPartitionError>(
            [] { world::ChunkGrid bad{world::ChunkGridConfig{std::numeric_limits<double>::quiet_NaN()}}; });
        REQUIRE(nan.has_value());
        CHECK(nan->code() == world::WorldPartitionErrorCode::InvalidChunkSize);
    }

    TEST_CASE("unrepresentable coordinates are rejected instead of wrapping") {
        const world::ChunkGrid grid{world::ChunkGrid::fromMetreEdge(metreUnit())};

        const auto huge = captureException<world::WorldPartitionError>(
            [&] { static_cast<void>(grid.chunkAt(1.0e300, 0.0)); });
        REQUIRE(huge.has_value());
        CHECK(huge->code() == world::WorldPartitionErrorCode::CoordinateOutOfRange);

        const auto nan = captureException<world::WorldPartitionError>(
            [&] { static_cast<void>(grid.chunkAt(0.0, std::numeric_limits<double>::quiet_NaN())); });
        REQUIRE(nan.has_value());
        CHECK(nan->code() == world::WorldPartitionErrorCode::CoordinateOutOfRange);

        const auto infinite = captureException<world::WorldPartitionError>(
            [&] { static_cast<void>(grid.chunkAt(std::numeric_limits<double>::infinity(), 0.0)); });
        REQUIRE(infinite.has_value());
        CHECK(infinite->code() == world::WorldPartitionErrorCode::CoordinateOutOfRange);
    }

    TEST_CASE("the representable index range stops at 2^53 - 1 on both sides") {
        // A unit grid makes the boundary arithmetic exact.
        const world::ChunkGrid unit{world::ChunkGridConfig{1.0}};
        const auto highest = 9007199254740991.0; // 2^53 - 1

        CHECK(unit.chunkAt(highest, 0.0) == world::ChunkCoord{9007199254740991, 0});
        CHECK(unit.chunkAt(0.0, -highest) == world::ChunkCoord{0, -9007199254740991});

        // The highest cell's footprint must keep its full width: at
        // exactly 2^53 the (k + 1) upper edge would round back onto k.
        const auto topFootprint = unit.chunkBounds(world::ChunkCoord{9007199254740991, 0});
        CHECK(topFootprint.maxEasting == 9007199254740992.0);
        CHECK(topFootprint.maxEasting - topFootprint.minEasting == 1.0);
        const auto bottomFootprint = unit.chunkBounds(world::ChunkCoord{0, -9007199254740991});
        CHECK(bottomFootprint.minNorthing == -9007199254740991.0);
        CHECK(bottomFootprint.maxNorthing - bottomFootprint.minNorthing == 1.0);

        // One step beyond the cap is rejected instead of silently
        // collapsing cells.
        const auto beyond = captureException<world::WorldPartitionError>(
            [&] { static_cast<void>(unit.chunkAt(9007199254740992.0, 0.0)); });
        REQUIRE(beyond.has_value());
        CHECK(beyond->code() == world::WorldPartitionErrorCode::CoordinateOutOfRange);
    }

    TEST_CASE("chunk bounds reject coordinates outside the supported index range") {
        const world::ChunkGrid unit{world::ChunkGridConfig{1.0}};

        // ChunkCoord is public and spans all of int64; chunkBounds must
        // enforce the documented range itself rather than trusting its
        // input to have come from chunkAt.
        for (const auto extreme : {std::numeric_limits<std::int64_t>::max(),
                 std::numeric_limits<std::int64_t>::min(),
                 static_cast<std::int64_t>(9007199254740992), // 2^53
                 static_cast<std::int64_t>(-9007199254740992)}) {
            CAPTURE(extreme);
            const auto xError = captureException<world::WorldPartitionError>(
                [&] { static_cast<void>(unit.chunkBounds(world::ChunkCoord{extreme, 0})); });
            REQUIRE(xError.has_value());
            CHECK(xError->code() == world::WorldPartitionErrorCode::CoordinateOutOfRange);

            const auto yError = captureException<world::WorldPartitionError>(
                [&] { static_cast<void>(unit.chunkBounds(world::ChunkCoord{0, extreme})); });
            REQUIRE(yError.has_value());
            CHECK(yError->code() == world::WorldPartitionErrorCode::CoordinateOutOfRange);
        }
    }

    TEST_CASE("bounds spanning more cells than the enumeration cap are rejected") {
        // Cell size chosen so the test bounds are geometrically reasonable
        // in metres but cross the enumeration safety cap.
        const world::ChunkGrid millimetre{world::ChunkGridConfig{0.001}};
        const auto vast = world::SpatialBounds::ofEdges(0.0, 0.0, 2000.0, 2000.0);

        const auto error = captureException<world::WorldPartitionError>(
            [&] { static_cast<void>(millimetre.chunksIntersecting(vast)); });
        REQUIRE(error.has_value());
        CHECK(error->code() == world::WorldPartitionErrorCode::CoordinateOutOfRange);
    }
}
