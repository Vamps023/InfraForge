#include <doctest/doctest.h>

#include "infraforge/domain/road/AlignmentPrimitives.hpp"
#include "infraforge/domain/road/ReferenceAlignment.hpp"
#include "infraforge/domain/road/RoadTypes.hpp"

#include <cmath>
#include <string>

namespace {

using infraforge::domain::road::AlignmentSample;
using infraforge::domain::road::AlignmentSegmentKind;
using infraforge::domain::road::CircularArcSegment;
using infraforge::domain::road::ClothoidSegment;
using infraforge::domain::road::LineSegment;
using infraforge::domain::road::ReferenceAlignment;
using infraforge::domain::road::Station;
using infraforge::domain::road::segmentKind;

constexpr double kPi = 3.14159265358979323846;
constexpr double kTol = 1e-9;

// Validates a fixture: alignment builds, stationing is continuous, and
// evaluation is deterministic (same input -> same output).
void validateFixture(const ReferenceAlignment& alignment, const std::string& name) {
    INFO("fixture: ", name);
    CHECK_FALSE(alignment.isEmpty());
    CHECK(alignment.totalLength() > 0.0);

    // Stationing continuity: each segment starts where the previous ends.
    const auto& segs = alignment.segments();
    for (std::size_t i = 1; i < segs.size(); ++i) {
        const double prevEnd = segs[i - 1].startStation
            + infraforge::domain::road::segmentLength(segs[i - 1].segment);
        CHECK(segs[i].startStation == doctest::Approx(prevEnd).epsilon(kTol));
    }

    // Determinism: evaluate twice, get identical results.
    for (Station s = 0.0; s <= alignment.totalLength(); s += 10.0) {
        const auto a = alignment.evaluate(s);
        const auto b = alignment.evaluate(s);
        CHECK(a.position.easting == b.position.easting);
        CHECK(a.position.northing == b.position.northing);
        CHECK(a.heading == b.heading);
        CHECK(a.curvature == b.curvature);
    }
}

} // namespace

TEST_SUITE("road alignment fixtures") {

TEST_CASE("fixture: single line") {
    LineSegment line{.start = {1000.0, 2000.0}, .heading = 0.3, .length = 200.0};
    auto built = ReferenceAlignment::build({line});
    REQUIRE(built.has_value());
    const auto& alignment = *built;
    validateFixture(alignment, "single line");

    CHECK(alignment.segmentCount() == 1);
    CHECK(alignment.totalLength() == doctest::Approx(200.0));

    // End point: start + length * (cos, sin).
    const auto end = alignment.evaluate(200.0);
    CHECK(end.position.easting == doctest::Approx(1000.0 + 200.0 * std::cos(0.3)).epsilon(kTol));
    CHECK(end.position.northing == doctest::Approx(2000.0 + 200.0 * std::sin(0.3)).epsilon(kTol));
    CHECK(end.heading == doctest::Approx(0.3));
    CHECK(end.curvature == doctest::Approx(0.0));
}

TEST_CASE("fixture: line to arc") {
    LineSegment line{.start = {0.0, 0.0}, .heading = 0.0, .length = 100.0};
    const auto lineEnd = line.endSample();
    // Clothoid transition needed for curvature continuity; but for a
    // pure line->arc fixture we accept the curvature discontinuity by
    // relaxing the curvature tolerance.
    const double radius = 80.0;
    const double kappa = 1.0 / radius;
    const double arcLen = radius * kPi / 6.0; // 30 degrees
    CircularArcSegment arc{.start = lineEnd.position, .startHeading = lineEnd.heading,
        .curvature = kappa, .length = arcLen};

    auto built = ReferenceAlignment::build({line, arc},
        1e-6, 1e-6, 1.0); // relax curvature tolerance for this fixture
    REQUIRE(built.has_value());
    const auto& alignment = *built;
    validateFixture(alignment, "line to arc");

    CHECK(alignment.segmentCount() == 2);
    CHECK(segmentKind(alignment.segments()[0].segment) == AlignmentSegmentKind::Line);
    CHECK(segmentKind(alignment.segments()[1].segment) == AlignmentSegmentKind::CircularArc);

    // At the boundary, position and heading are continuous.
    const auto atBoundary = alignment.evaluate(100.0);
    CHECK(atBoundary.position.easting == doctest::Approx(100.0));
    CHECK(atBoundary.position.northing == doctest::Approx(0.0));
    CHECK(atBoundary.heading == doctest::Approx(0.0));

    // End heading = 30 degrees.
    const auto end = alignment.evaluate(alignment.totalLength());
    CHECK(end.heading == doctest::Approx(kPi / 6.0).epsilon(kTol));
    CHECK(end.curvature == doctest::Approx(kappa));
}

TEST_CASE("fixture: line to spiral to arc") {
    LineSegment line{.start = {500.0, 1000.0}, .heading = 0.0, .length = 120.0};
    const auto lineEnd = line.endSample();
    const double kappa = 0.015; // radius ~66.67m
    ClothoidSegment spiral{.start = lineEnd.position, .startHeading = lineEnd.heading,
        .startCurvature = 0.0, .endCurvature = kappa, .length = 60.0};
    const auto spiralEnd = spiral.endSample();
    CircularArcSegment arc{.start = spiralEnd.position, .startHeading = spiralEnd.heading,
        .curvature = kappa, .length = 80.0};

    auto built = ReferenceAlignment::build({line, spiral, arc});
    REQUIRE(built.has_value());
    const auto& alignment = *built;
    validateFixture(alignment, "line to spiral to arc");

    CHECK(alignment.segmentCount() == 3);
    CHECK(segmentKind(alignment.segments()[0].segment) == AlignmentSegmentKind::Line);
    CHECK(segmentKind(alignment.segments()[1].segment) == AlignmentSegmentKind::Clothoid);
    CHECK(segmentKind(alignment.segments()[2].segment) == AlignmentSegmentKind::CircularArc);

    // Curvature is continuous: 0 at start, ramps to kappa through spiral,
    // stays kappa through arc.
    CHECK(alignment.evaluate(0.0).curvature == doctest::Approx(0.0));
    CHECK(alignment.evaluate(120.0).curvature == doctest::Approx(0.0)); // end of line
    CHECK(alignment.evaluate(150.0).curvature == doctest::Approx(kappa / 2.0)); // mid-spiral
    CHECK(alignment.evaluate(180.0).curvature == doctest::Approx(kappa)); // end of spiral
    CHECK(alignment.evaluate(alignment.totalLength()).curvature == doctest::Approx(kappa));
}

TEST_CASE("fixture: arc to spiral to line") {
    const double kappa = 0.02;
    CircularArcSegment arc{.start = {0.0, 0.0}, .startHeading = 0.0,
        .curvature = kappa, .length = 50.0};
    const auto arcEnd = arc.endSample();
    ClothoidSegment spiral{.start = arcEnd.position, .startHeading = arcEnd.heading,
        .startCurvature = kappa, .endCurvature = 0.0, .length = 60.0};
    const auto spiralEnd = spiral.endSample();
    LineSegment line{.start = spiralEnd.position, .heading = spiralEnd.heading, .length = 80.0};

    auto built = ReferenceAlignment::build({arc, spiral, line});
    REQUIRE(built.has_value());
    const auto& alignment = *built;
    validateFixture(alignment, "arc to spiral to line");

    CHECK(alignment.segmentCount() == 3);
    CHECK(segmentKind(alignment.segments()[0].segment) == AlignmentSegmentKind::CircularArc);
    CHECK(segmentKind(alignment.segments()[1].segment) == AlignmentSegmentKind::Clothoid);
    CHECK(segmentKind(alignment.segments()[2].segment) == AlignmentSegmentKind::Line);

    // Curvature: kappa through arc, ramps to 0 through spiral, 0 on line.
    CHECK(alignment.evaluate(0.0).curvature == doctest::Approx(kappa));
    CHECK(alignment.evaluate(50.0).curvature == doctest::Approx(kappa));
    CHECK(alignment.evaluate(80.0).curvature == doctest::Approx(kappa / 2.0));
    CHECK(alignment.evaluate(110.0).curvature == doctest::Approx(0.0));
    CHECK(alignment.evaluate(alignment.totalLength()).curvature == doctest::Approx(0.0));
}

TEST_CASE("fixture: multi-segment road") {
    // Line -> Spiral -> Arc -> Spiral -> Line (S-curve).
    LineSegment line1{.start = {0.0, 0.0}, .heading = 0.0, .length = 100.0};
    const auto l1End = line1.endSample();
    const double kappa = 0.01;
    ClothoidSegment sp1{.start = l1End.position, .startHeading = l1End.heading,
        .startCurvature = 0.0, .endCurvature = kappa, .length = 50.0};
    const auto sp1End = sp1.endSample();
    CircularArcSegment arc{.start = sp1End.position, .startHeading = sp1End.heading,
        .curvature = kappa, .length = 60.0};
    const auto arcEnd = arc.endSample();
    ClothoidSegment sp2{.start = arcEnd.position, .startHeading = arcEnd.heading,
        .startCurvature = kappa, .endCurvature = 0.0, .length = 50.0};
    const auto sp2End = sp2.endSample();
    LineSegment line2{.start = sp2End.position, .heading = sp2End.heading, .length = 100.0};

    auto built = ReferenceAlignment::build({line1, sp1, arc, sp2, line2});
    REQUIRE(built.has_value());
    const auto& alignment = *built;
    validateFixture(alignment, "multi-segment road");

    CHECK(alignment.segmentCount() == 5);
    CHECK(alignment.totalLength() == doctest::Approx(100.0 + 50.0 + 60.0 + 50.0 + 100.0));

    // Verify segment kinds in order.
    const auto& segs = alignment.segments();
    CHECK(segmentKind(segs[0].segment) == AlignmentSegmentKind::Line);
    CHECK(segmentKind(segs[1].segment) == AlignmentSegmentKind::Clothoid);
    CHECK(segmentKind(segs[2].segment) == AlignmentSegmentKind::CircularArc);
    CHECK(segmentKind(segs[3].segment) == AlignmentSegmentKind::Clothoid);
    CHECK(segmentKind(segs[4].segment) == AlignmentSegmentKind::Line);

    // Curvature profile: 0 -> kappa -> kappa -> kappa -> 0 -> 0.
    CHECK(alignment.evaluate(0.0).curvature == doctest::Approx(0.0));
    CHECK(alignment.evaluate(100.0).curvature == doctest::Approx(0.0));
    CHECK(alignment.evaluate(125.0).curvature == doctest::Approx(kappa / 2.0));
    CHECK(alignment.evaluate(150.0).curvature == doctest::Approx(kappa));
    CHECK(alignment.evaluate(180.0).curvature == doctest::Approx(kappa));
    CHECK(alignment.evaluate(210.0).curvature == doctest::Approx(kappa)); // end of arc
    CHECK(alignment.evaluate(235.0).curvature == doctest::Approx(kappa / 2.0)); // mid sp2
    CHECK(alignment.evaluate(260.0).curvature == doctest::Approx(0.0));
    CHECK(alignment.evaluate(alignment.totalLength()).curvature == doctest::Approx(0.0));

    // Segment lookup at boundaries is deterministic.
    CHECK(*alignment.segmentIndexAt(0.0) == 0);
    CHECK(*alignment.segmentIndexAt(100.0) == 0); // boundary -> earlier
    CHECK(*alignment.segmentIndexAt(100.001) == 1);
    CHECK(*alignment.segmentIndexAt(150.0) == 1); // boundary -> earlier
    CHECK(*alignment.segmentIndexAt(150.001) == 2);
    CHECK(*alignment.segmentIndexAt(alignment.totalLength()) == 4);
}

TEST_CASE("fixture: negative curvature S-curve") {
    // Line -> Spiral(0 -> -kappa) -> Arc(-kappa) -> Spiral(-kappa -> 0) -> Line.
    LineSegment line1{.start = {0.0, 0.0}, .heading = 0.0, .length = 80.0};
    const auto l1End = line1.endSample();
    const double kappa = -0.012;
    ClothoidSegment sp1{.start = l1End.position, .startHeading = l1End.heading,
        .startCurvature = 0.0, .endCurvature = kappa, .length = 40.0};
    const auto sp1End = sp1.endSample();
    CircularArcSegment arc{.start = sp1End.position, .startHeading = sp1End.heading,
        .curvature = kappa, .length = 50.0};
    const auto arcEnd = arc.endSample();
    ClothoidSegment sp2{.start = arcEnd.position, .startHeading = arcEnd.heading,
        .startCurvature = kappa, .endCurvature = 0.0, .length = 40.0};
    const auto sp2End = sp2.endSample();
    LineSegment line2{.start = sp2End.position, .heading = sp2End.heading, .length = 80.0};

    auto built = ReferenceAlignment::build({line1, sp1, arc, sp2, line2});
    REQUIRE(built.has_value());
    const auto& alignment = *built;
    validateFixture(alignment, "negative curvature S-curve");

    // Curvature is negative through the curve.
    CHECK(alignment.evaluate(80.0).curvature == doctest::Approx(0.0));
    CHECK(alignment.evaluate(100.0).curvature == doctest::Approx(kappa / 2.0));
    CHECK(alignment.evaluate(120.0).curvature == doctest::Approx(kappa));
    CHECK(alignment.evaluate(170.0).curvature == doctest::Approx(kappa));
    CHECK(alignment.evaluate(190.0).curvature == doctest::Approx(kappa / 2.0));
    CHECK(alignment.evaluate(210.0).curvature == doctest::Approx(0.0));

    // End heading: the arc contributes constant curvature heading change.
    // sp1: (0 + kappa) * 40 / 2 = kappa * 20
    // arc: kappa * 50
    // sp2: (kappa + 0) * 40 / 2 = kappa * 20
    // Total = kappa * (20 + 50 + 20) = kappa * 90
    const auto end = alignment.evaluate(alignment.totalLength());
    CHECK(end.heading == doctest::Approx(kappa * 90.0).epsilon(1e-6));
}

} // TEST_SUITE
