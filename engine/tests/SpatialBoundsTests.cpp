#include <doctest/doctest.h>

#include "infraforge/domain/geo/GeoTypes.hpp"
#include "infraforge/domain/world/SpatialBounds.hpp"

#include <limits>

namespace world = infraforge::domain::world;

TEST_SUITE("canonical spatial bounds") {
    TEST_CASE("default constructed and explicit bounds are empty") {
        CHECK(world::SpatialBounds{}.isEmpty());
        CHECK(world::SpatialBounds::empty().isEmpty());
        CHECK_FALSE(world::SpatialBounds::ofEdges(0.0, 0.0, 10.0, 10.0).isEmpty());
    }

    TEST_CASE("contains uses closed edges") {
        const auto bounds = world::SpatialBounds::ofEdges(-10.0, -20.0, 10.0, 20.0);

        CHECK(bounds.contains(0.0, 0.0));
        CHECK(bounds.contains(-10.0, -20.0)); // min edges inclusive
        CHECK(bounds.contains(10.0, 20.0));   // max edges inclusive
        CHECK_FALSE(bounds.contains(10.0001, 0.0));
        CHECK_FALSE(bounds.contains(0.0, -20.0001));
        CHECK_FALSE(bounds.contains(std::numeric_limits<double>::infinity(), 0.0));
    }

    TEST_CASE("empty bounds contain nothing") {
        const auto empty = world::SpatialBounds::empty();

        CHECK_FALSE(empty.contains(0.0, 0.0));
        CHECK_FALSE(empty.contains(world::SpatialBounds::ofPoint(0.0, 0.0)));
        CHECK(empty.contains(world::SpatialBounds::empty())); // vacuously
    }

    TEST_CASE("intersection of touching edges is deterministic") {
        const auto left = world::SpatialBounds::ofEdges(0.0, 0.0, 10.0, 10.0);
        const auto touching = world::SpatialBounds::ofEdges(10.0, 0.0, 20.0, 10.0);
        const auto overlapping = world::SpatialBounds::ofEdges(5.0, 5.0, 15.0, 15.0);
        const auto disjoint = world::SpatialBounds::ofEdges(100.0, 100.0, 110.0, 110.0);

        // Closed edges: exact edge contact is a real intersection, and the
        // degenerate shared edge is a valid non-empty result.
        CHECK(left.intersects(touching));
        CHECK(touching.intersects(left));
        CHECK(left.intersectedWith(touching) == world::SpatialBounds::ofEdges(10.0, 0.0, 10.0, 10.0));
        CHECK(left.intersectedWith(overlapping)
            == world::SpatialBounds::ofEdges(5.0, 5.0, 10.0, 10.0));
        CHECK_FALSE(left.intersects(disjoint));
        CHECK(left.intersectedWith(disjoint).isEmpty());
        CHECK(left.intersectedWith(world::SpatialBounds::empty()).isEmpty());
    }

    TEST_CASE("union and expansion absorb empty bounds and grow monotonically") {
        auto bounds = world::SpatialBounds::empty();
        CHECK(bounds.unitedWith(world::SpatialBounds::empty()).isEmpty());

        bounds.expandTo(5.0, -5.0);
        CHECK(bounds == world::SpatialBounds::ofPoint(5.0, -5.0));

        bounds.expandTo(-15.0, 25.0);
        CHECK(bounds == world::SpatialBounds::ofEdges(-15.0, -5.0, 5.0, 25.0));

        bounds.uniteWith(world::SpatialBounds::empty());
        CHECK(bounds == world::SpatialBounds::ofEdges(-15.0, -5.0, 5.0, 25.0));

        bounds.uniteWith(world::SpatialBounds::ofEdges(100.0, 100.0, 200.0, 200.0));
        CHECK(bounds == world::SpatialBounds::ofEdges(-15.0, -5.0, 200.0, 200.0));

        // Empty union operand disappears; non-empty union keeps both sides.
        CHECK(world::SpatialBounds::empty()
                  .unitedWith(world::SpatialBounds::ofPoint(-1.0, 1.0))
              == world::SpatialBounds::ofPoint(-1.0, 1.0));
    }

    TEST_CASE("positions expand via the project-global overload") {
        using infraforge::domain::geo::ProjectGlobalPosition;

        auto bounds = world::SpatialBounds::empty();
        bounds.expandTo(ProjectGlobalPosition{-5000.0, 2500.0, 100.0});
        bounds.expandTo(ProjectGlobalPosition{5000.0, -2500.0, -30.0});

        // Height never participates: only easting/northing are tracked.
        CHECK(bounds == world::SpatialBounds::ofEdges(-5000.0, -2500.0, 5000.0, 2500.0));
        CHECK(bounds.contains(ProjectGlobalPosition{0.0, 0.0, 1.0e9}));
    }

    TEST_CASE("negative coordinates behave identically") {
        const auto bounds = world::SpatialBounds::ofEdges(-50000.0, -50000.0, -49000.0, -49000.0);

        CHECK(bounds.contains(-49500.0, -49500.0));
        CHECK(bounds.intersects(world::SpatialBounds::ofPoint(-49000.0, -49000.0)));
        CHECK_FALSE(bounds.intersects(world::SpatialBounds::ofPoint(-48000.0, -49000.0)));
    }

    TEST_CASE("finite check rejects non-finite non-empty bounds") {
        const auto nanBounds = world::SpatialBounds::ofEdges(
            0.0, 0.0, std::numeric_limits<double>::quiet_NaN(), 10.0);
        CHECK_FALSE(nanBounds.isFinite());

        const auto infiniteBounds = world::SpatialBounds::ofEdges(
            0.0, 0.0, std::numeric_limits<double>::infinity(), 10.0);
        CHECK_FALSE(infiniteBounds.isFinite());

        CHECK(world::SpatialBounds::ofEdges(-1.0e300, -1.0e300, 1.0e300, 1.0e300).isFinite());
        // Empty bounds are well-formed regardless of their sentinel edges.
        CHECK(world::SpatialBounds::empty().isFinite());
    }
}
