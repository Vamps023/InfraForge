#include <doctest/doctest.h>

#include "TestHelpers.hpp"
#include "infraforge/domain/world/ChunkGrid.hpp"
#include "infraforge/domain/world/WorldPartitionError.hpp"

#include <limits>
#include <string>
#include <vector>

namespace world = infraforge::domain::world;

using infraforge::testhelpers::captureException;

TEST_SUITE("logical chunk grid") {
    TEST_CASE("default grid is the 1 km canonical partition") {
        const world::ChunkGrid grid;

        CHECK(grid.chunkSize() == world::defaultChunkSize);
        CHECK(grid.chunkSize() == 1000.0);
        CHECK(grid.config() == world::ChunkGridConfig{});
    }

    TEST_CASE("chunk identity uses floor division, not truncation toward zero") {
        const world::ChunkGrid grid;

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
        const world::ChunkGrid grid;

        const auto inside = world::SpatialBounds::ofEdges(100.0, 200.0, 300.0, 400.0);
        CHECK(grid.chunksIntersecting(inside) == std::vector<world::ChunkCoord>{{0, 0}});

        const auto negativeInside = world::SpatialBounds::ofEdges(-2500.0, -2600.0, -2100.0, -2200.0);
        CHECK(grid.chunksIntersecting(negativeInside) == std::vector<world::ChunkCoord>{{-3, -3}});
    }

    TEST_CASE("bounds crossing a boundary select both adjacent chunks") {
        const world::ChunkGrid grid;

        const auto crossing = world::SpatialBounds::ofEdges(800.0, 100.0, 1200.0, 300.0);
        CHECK(grid.chunksIntersecting(crossing)
            == std::vector<world::ChunkCoord>{{0, 0}, {1, 0}});

        const auto negativeCrossing = world::SpatialBounds::ofEdges(-1200.0, -300.0, -800.0, -100.0);
        CHECK(grid.chunksIntersecting(negativeCrossing)
            == std::vector<world::ChunkCoord>{{-2, -1}, {-1, -1}});
    }

    TEST_CASE("bounds spanning many chunks enumerate deterministically") {
        const world::ChunkGrid grid;

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
        const world::ChunkGrid grid;

        // Closed bounds ending exactly on the 1 km edge stay conservative:
        // the shared-edge cell is touched too, while the position on that
        // edge belongs only to the higher cell.
        const auto edgeAligned = world::SpatialBounds::ofEdges(0.0, 0.0, 1000.0, 1000.0);
        CHECK(grid.chunksIntersecting(edgeAligned)
            == std::vector<world::ChunkCoord>{{0, 0}, {0, 1}, {1, 0}, {1, 1}});
        CHECK(grid.chunkAt(1000.0, 1000.0) == world::ChunkCoord{1, 1});
    }

    TEST_CASE("empty bounds select no chunks") {
        const world::ChunkGrid grid;

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
        const world::ChunkGrid grid;

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
        const world::ChunkGrid grid;

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
