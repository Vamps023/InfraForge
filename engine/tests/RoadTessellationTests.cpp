#include "infraforge/domain/road/AlignmentPrimitives.hpp"
#include "infraforge/domain/road/ReferenceAlignment.hpp"
#include "infraforge/domain/road/RoadTessellation.hpp"
#include "infraforge/domain/road/VerticalProfiles.hpp"

#include <doctest/doctest.h>
#include <cmath>

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

    // At station 50, superelevation = 0.1, halfWidth = 5.0.
    // leftHeight = height + 0.1 * 5 = 0 + 0.5 = 0.5
    // rightHeight = height - 0.1 * 5 = 0 - 0.5 = -0.5
    CHECK(tess.crossSections[5].crossSlope == doctest::Approx(0.1));
    CHECK(tess.crossSections[5].leftHeight == doctest::Approx(0.5));
    CHECK(tess.crossSections[5].rightHeight == doctest::Approx(-0.5));
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
    CHECK(tess.crossSectionCount() == 6);  // 0, 10, 20, 30, 40, 50
    CHECK(tess.triangleCount() == 10);

    // The arc curves to the left (positive curvature), so the centerline
    // should move north as station increases.
    CHECK(tess.crossSections[1].center.northing > doctest::Approx(0.0));
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

TEST_CASE("tessellateRoad clamps interval to safe minimum") {
    auto alignment = makeStraightAlignment(100.0);
    RoadTessellationParams params;
    params.stationInterval = 0.0;  // invalid
    params.halfWidth = 5.0;

    auto tess = tessellateRoad(alignment, {}, {}, params);
    // Should still produce a valid tessellation with the clamped interval.
    CHECK(tess.crossSectionCount() >= 2);
    CHECK(tess.triangleCount() >= 2);
}

} // namespace infraforge::domain::road
