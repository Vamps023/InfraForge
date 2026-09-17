#include "infraforge/domain/road/AlignmentPrimitives.hpp"
#include "infraforge/domain/road/ReferenceAlignment.hpp"
#include "infraforge/domain/road/RoadTessellation.hpp"
#include "infraforge/domain/road/RoadWidthProfile.hpp"
#include "infraforge/domain/road/VerticalProfiles.hpp"

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <ranges>

namespace infraforge::domain::road {
namespace {

using namespace infraforge::domain::road;

ReferenceAlignment makeStraightAlignment(double length) {
    LineSegment line{.start = {0.0, 0.0}, .heading = 0.0, .length = length};
    auto alignment = ReferenceAlignment::build({line});
    REQUIRE(alignment.has_value());
    return *alignment;
}

ReferenceAlignment makeArcAlignment(double radius, double arcLength) {
    const double curvature = 1.0 / radius;
    CircularArcSegment arc{
        .start = {0.0, 0.0},
        .startHeading = 0.0,
        .curvature = curvature,
        .length = arcLength};
    auto alignment = ReferenceAlignment::build({arc});
    REQUIRE(alignment.has_value());
    return *alignment;
}

} // namespace

TEST_CASE("tessellateRoad produces empty tessellation for empty alignment") {
    ReferenceAlignment empty;
    RoadTessellationParams params;
    auto tess = tessellateRoad(empty, {}, {}, params);
    CHECK(tess.isEmpty());
    CHECK(tess.crossSectionCount() == 0);
    CHECK(tess.triangleCount() == 0);
}

TEST_CASE("tessellateRoad samples a straight alignment at regular intervals") {
    auto alignment = makeStraightAlignment(100.0);
    RoadTessellationParams params;
    params.stationInterval = 10.0;
    params.halfWidth = 5.0;

    auto tess = tessellateRoad(alignment, {}, {}, params);
    CHECK(tess.crossSectionCount() == 11);  // 0, 10, 20, ..., 100
    CHECK(tess.triangleCount() == 20);     // 10 pairs * 2 triangles = 20
}

TEST_CASE("tessellateRoad centerline follows the alignment") {
    auto alignment = makeStraightAlignment(100.0);
    RoadTessellationParams params;
    params.stationInterval = 10.0;
    params.halfWidth = 5.0;

    auto tess = tessellateRoad(alignment, {}, {}, params);
    REQUIRE(tess.crossSectionCount() >= 2);

    // First cross-section at station 0.
    CHECK(tess.crossSections[0].station == doctest::Approx(0.0));
    CHECK(tess.crossSections[0].center.easting == doctest::Approx(0.0));
    CHECK(tess.crossSections[0].center.northing == doctest::Approx(0.0));
    CHECK(tess.crossSections[0].heading == doctest::Approx(0.0));

    // Cross-section at station 50.
    CHECK(tess.crossSections[5].station == doctest::Approx(50.0));
    CHECK(tess.crossSections[5].center.easting == doctest::Approx(50.0));
    CHECK(tess.crossSections[5].center.northing == doctest::Approx(0.0));

    // Final cross-section at station 100.
    CHECK(tess.crossSections.back().station == doctest::Approx(100.0));
    CHECK(tess.crossSections.back().center.easting == doctest::Approx(100.0));
}

TEST_CASE("tessellateRoad left/right edges are perpendicular to heading") {
    auto alignment = makeStraightAlignment(100.0);
    RoadTessellationParams params;
    params.stationInterval = 10.0;
    params.halfWidth = 5.0;

    auto tess = tessellateRoad(alignment, {}, {}, params);
    REQUIRE(tess.crossSectionCount() >= 1);

    // For heading = 0 (east), left edge is north (+northing), right edge is south.
    const auto& cs = tess.crossSections[5];  // station 50
    CHECK(cs.leftEdge.easting == doctest::Approx(50.0));
    CHECK(cs.leftEdge.northing == doctest::Approx(5.0));
    CHECK(cs.rightEdge.easting == doctest::Approx(50.0));
    CHECK(cs.rightEdge.northing == doctest::Approx(-5.0));
}

TEST_CASE("tessellateRoad applies elevation profile") {
    auto alignment = makeStraightAlignment(100.0);
    auto elevation = buildElevationProfile({{0.0, 0.0}, {100.0, 10.0}});
    REQUIRE(elevation.has_value());

    RoadTessellationParams params;
    params.stationInterval = 10.0;
    params.halfWidth = 5.0;

    auto tess = tessellateRoad(alignment, *elevation, {}, params);
    REQUIRE(tess.crossSectionCount() >= 2);

    // At station 50, elevation should be 5.0 (linear interpolation).
    CHECK(tess.crossSections[5].height == doctest::Approx(5.0));
}

TEST_CASE("tessellateRoad applies superelevation profile") {
    auto alignment = makeStraightAlignment(100.0);
    auto superelevation = buildSuperelevationProfile({{0.0, 0.0}, {50.0, 0.1}, {100.0, 0.0}});
    REQUIRE(superelevation.has_value());

    RoadTessellationParams params;
    params.stationInterval = 10.0;
    params.halfWidth = 5.0;

    auto tess = tessellateRoad(alignment, {}, *superelevation, params);
    REQUIRE(tess.crossSectionCount() >= 2);

    // Superelevation is an angle, so transverse height uses tan(angle).
    CHECK(tess.crossSections[5].crossSlope == doctest::Approx(0.1));
    CHECK(tess.crossSections[5].leftHeight == doctest::Approx(std::tan(0.1) * 5.0));
    CHECK(tess.crossSections[5].rightHeight == doctest::Approx(-std::tan(0.1) * 5.0));
}

TEST_CASE("tessellateRoad interpolates asymmetric station-aware widths") {
    auto alignment = makeStraightAlignment(100.0);
    auto width = buildRoadWidthProfile({{0.0, 3.0, 4.0}, {100.0, 7.0, 2.0}});
    REQUIRE(width.has_value());

    RoadTessellationParams params;
    params.stationInterval = 50.0;
    auto tess = tessellateRoad(alignment, {}, {}, *width, params);
    REQUIRE(tess.crossSectionCount() == 3);

    CHECK(tess.crossSections[0].leftWidth == doctest::Approx(3.0));
    CHECK(tess.crossSections[0].rightWidth == doctest::Approx(4.0));
    CHECK(tess.crossSections[1].leftWidth == doctest::Approx(5.0));
    CHECK(tess.crossSections[1].rightWidth == doctest::Approx(3.0));
    CHECK(tess.crossSections[1].leftEdge.northing == doctest::Approx(5.0));
    CHECK(tess.crossSections[1].rightEdge.northing == doctest::Approx(-3.0));
    CHECK(tess.crossSections[2].leftWidth == doctest::Approx(7.0));
    CHECK(tess.crossSections[2].rightWidth == doctest::Approx(2.0));
}

TEST_CASE("RoadWidthProfile rejects invalid breakpoints") {
    CHECK_FALSE(buildRoadWidthProfile({{0.0, -1.0, 5.0}}).has_value());
    CHECK_FALSE(buildRoadWidthProfile({{0.0, 5.0, 5.0}, {0.0, 6.0, 6.0}}).has_value());
    CHECK_FALSE(buildRoadWidthProfile({{std::numeric_limits<double>::quiet_NaN(), 5.0, 5.0}}).has_value());
}

TEST_CASE("tessellateRoad indices form valid triangle strip") {
    auto alignment = makeStraightAlignment(50.0);
    RoadTessellationParams params;
    params.stationInterval = 10.0;
    params.halfWidth = 5.0;

    auto tess = tessellateRoad(alignment, {}, {}, params);
    REQUIRE(tess.crossSectionCount() == 6);  // 0, 10, 20, 30, 40, 50
    REQUIRE(tess.indices.size() == 5 * 6);  // 5 pairs * 6 indices = 30

    // Check first triangle: left0, right0, left1
    CHECK(tess.indices[0] == 0);  // left 0
    CHECK(tess.indices[1] == 1);  // right 0
    CHECK(tess.indices[2] == 2);  // left 1
}

TEST_CASE("tessellateRoad handles arc alignment") {
    auto alignment = makeArcAlignment(100.0, 50.0);
    RoadTessellationParams params;
    params.stationInterval = 10.0;
    params.halfWidth = 5.0;

    auto tess = tessellateRoad(alignment, {}, {}, params);
    CHECK(tess.crossSectionCount() == 11);  // 5 m refinement satisfies 5 cm surface error.
    CHECK(tess.triangleCount() == 20);

    // The arc curves to the left (positive curvature), so the centerline
    // should move north as station increases.
    CHECK(tess.crossSections[1].center.northing > doctest::Approx(0.0));
}

TEST_CASE("adaptive tessellation enforces surface chord error on arcs") {
    auto alignment = makeArcAlignment(25.0, 50.0);
    RoadTessellationParams params;
    params.stationInterval = 20.0;
    params.maximumSurfaceError = 0.01;

    auto tess = tessellateRoad(alignment, {}, {}, params);
    REQUIRE(tess.crossSectionCount() > 4);
    for (std::size_t i = 0; i + 1 < tess.crossSections.size(); ++i) {
        const auto& start = tess.crossSections[i];
        const auto& end = tess.crossSections[i + 1];
        const auto actual = alignment.evaluate((start.station + end.station) * 0.5).position;
        const double deviation = std::hypot(
            actual.easting - (start.center.easting + end.center.easting) * 0.5,
            actual.northing - (start.center.northing + end.center.northing) * 0.5);
        CHECK(deviation <= params.maximumSurfaceError + 1e-12);
    }
}

TEST_CASE("adaptive tessellation preserves authored profile and segment boundaries") {
    LineSegment first{.start = {0.0, 0.0}, .heading = 0.0, .length = 40.0};
    LineSegment second{.start = {40.0, 0.0}, .heading = 0.0, .length = 60.0};
    auto alignment = ReferenceAlignment::build({first, second});
    REQUIRE(alignment.has_value());
    auto elevation = buildElevationProfile({{0.0, 0.0}, {37.0, 4.0}, {100.0, 8.0}});
    REQUIRE(elevation.has_value());
    auto width = buildRoadWidthProfile({{0.0, 3.0, 3.0}, {63.0, 8.0, 2.0}});
    REQUIRE(width.has_value());

    RoadTessellationParams params;
    params.stationInterval = 50.0;
    auto tess = tessellateRoad(*alignment, *elevation, {}, *width, params);
    const auto hasStation = [&](const double station) {
        return std::ranges::any_of(tess.crossSections,
            [station](const auto& section) { return section.station == doctest::Approx(station); });
    };
    CHECK(hasStation(37.0));
    CHECK(hasStation(40.0));
    CHECK(hasStation(63.0));
}

TEST_CASE("adaptive tessellation fails safely when its cross-section budget is exceeded") {
    auto alignment = makeStraightAlignment(100.0);
    RoadTessellationParams params;
    params.stationInterval = 1.0;
    params.maximumCrossSections = 10;
    CHECK(tessellateRoad(alignment, {}, {}, params).isEmpty());
}

TEST_CASE("tessellateRoad with small interval produces more samples") {
    auto alignment = makeStraightAlignment(100.0);
    RoadTessellationParams params;
    params.stationInterval = 1.0;
    params.halfWidth = 5.0;

    auto tess = tessellateRoad(alignment, {}, {}, params);
    CHECK(tess.crossSectionCount() == 101);  // 0, 1, 2, ..., 100
    CHECK(tess.triangleCount() == 200);      // 100 pairs * 2 triangles
}

// Blocker 13: invalid tessellation inputs are rejected, not silently
// clamped. A zero or negative interval would produce undefined geometry.
TEST_CASE("tessellateRoad rejects zero interval") {
    auto alignment = makeStraightAlignment(100.0);
    RoadTessellationParams params;
    params.stationInterval = 0.0;  // invalid
    params.halfWidth = 5.0;

    auto tess = tessellateRoad(alignment, {}, {}, params);
    CHECK(tess.isEmpty());
}

TEST_CASE("tessellateRoad rejects negative interval") {
    auto alignment = makeStraightAlignment(100.0);
    RoadTessellationParams params;
    params.stationInterval = -5.0;
    params.halfWidth = 5.0;

    auto tess = tessellateRoad(alignment, {}, {}, params);
    CHECK(tess.isEmpty());
}

TEST_CASE("tessellateRoad rejects non-finite interval") {
    auto alignment = makeStraightAlignment(100.0);
    RoadTessellationParams params;
    params.stationInterval = std::numeric_limits<double>::quiet_NaN();
    params.halfWidth = 5.0;

    auto tess = tessellateRoad(alignment, {}, {}, params);
    CHECK(tess.isEmpty());
}

TEST_CASE("tessellateRoad rejects negative halfWidth") {
    auto alignment = makeStraightAlignment(100.0);
    RoadTessellationParams params;
    params.stationInterval = 10.0;
    params.halfWidth = -5.0;

    auto tess = tessellateRoad(alignment, {}, {}, params);
    CHECK(tess.isEmpty());
}

} // namespace infraforge::domain::road
