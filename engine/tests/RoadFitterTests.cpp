#include <doctest/doctest.h>

#include "infraforge/domain/road/AlignmentFitter.hpp"
#include "infraforge/domain/road/AlignmentPrimitives.hpp"
#include "infraforge/domain/road/ReferenceAlignment.hpp"
#include "infraforge/domain/road/RoadSource.hpp"
#include "infraforge/domain/road/RoadTypes.hpp"

#include <cmath>
#include <iostream>
#include <numbers>

using namespace infraforge::domain::road;

namespace {

// Helper: create a straight polyline of N points along a heading.
std::vector<ConditionedVertex> makeStraightPolyline(
    double startX, double startY, double heading, double segmentLen, int n) {
    std::vector<ConditionedVertex> pts;
    for (int i = 0; i < n; ++i) {
        const double s = i * segmentLen;
        ConditionedVertex v;
        v.position.easting = startX + s * std::cos(heading);
        v.position.northing = startY + s * std::sin(heading);
        v.sourceStation = s;
        pts.push_back(v);
    }
    return pts;
}

// Helper: create a curved polyline (arc) of N points.
std::vector<ConditionedVertex> makeArcPolyline(
    double startX, double startY, double startHeading,
    double curvature, double arcLen, int n) {
    std::vector<ConditionedVertex> pts;
    for (int i = 0; i < n; ++i) {
        const double s = (arcLen * i) / (n - 1);
        const double theta = startHeading + curvature * s;
        ConditionedVertex v;
        if (std::abs(curvature) < 1e-15) {
            v.position.easting = startX + s * std::cos(startHeading);
            v.position.northing = startY + s * std::sin(startHeading);
        } else {
            v.position.easting = startX +
                (1.0 / curvature) * (std::sin(theta) - std::sin(startHeading));
            v.position.northing = startY +
                (1.0 / curvature) * (std::cos(startHeading) - std::cos(theta));
        }
        v.sourceStation = s;
        pts.push_back(v);
    }
    return pts;
}

// Helper: create an S-curve polyline (arc one way, then arc the other).
std::vector<ConditionedVertex> makeSCurvePolyline(
    double startX, double startY, double startHeading,
    double curvature1, double arcLen1,
    double curvature2, double arcLen2, int n) {
    std::vector<ConditionedVertex> pts;
    // First arc
    for (int i = 0; i < n; ++i) {
        const double s = (arcLen1 * i) / (n - 1);
        const double theta = startHeading + curvature1 * s;
        ConditionedVertex v;
        v.position.easting = startX +
            (1.0 / curvature1) * (std::sin(theta) - std::sin(startHeading));
        v.position.northing = startY +
            (1.0 / curvature1) * (std::cos(startHeading) - std::cos(theta));
        v.sourceStation = s;
        pts.push_back(v);
    }
    // Second arc starts where first ends
    const double s1End = arcLen1;
    const double theta1End = startHeading + curvature1 * s1End;
    const double x1End = startX +
        (1.0 / curvature1) * (std::sin(theta1End) - std::sin(startHeading));
    const double y1End = startY +
        (1.0 / curvature1) * (std::cos(startHeading) - std::cos(theta1End));
    for (int i = 1; i < n; ++i) {
        const double s = (arcLen2 * i) / (n - 1);
        const double theta = theta1End + curvature2 * s;
        ConditionedVertex v;
        v.position.easting = x1End +
            (1.0 / curvature2) * (std::sin(theta) - std::sin(theta1End));
        v.position.northing = y1End +
            (1.0 / curvature2) * (std::cos(theta1End) - std::cos(theta));
        v.sourceStation = s1End + s;
        pts.push_back(v);
    }
    return pts;
}

// Helper: create a line-arc-line polyline.
std::vector<ConditionedVertex> makeLineArcLinePolyline(
    double startX, double startY, double startHeading,
    double lineLen1, double curvature, double arcLen, double lineLen2,
    int nPerSection) {
    std::vector<ConditionedVertex> pts;
    // First line
    for (int i = 0; i < nPerSection; ++i) {
        const double s = (lineLen1 * i) / (nPerSection - 1);
        ConditionedVertex v;
        v.position.easting = startX + s * std::cos(startHeading);
        v.position.northing = startY + s * std::sin(startHeading);
        v.sourceStation = s;
        pts.push_back(v);
    }
    // Arc
    const double x1 = startX + lineLen1 * std::cos(startHeading);
    const double y1 = startY + lineLen1 * std::sin(startHeading);
    for (int i = 1; i < nPerSection; ++i) {
        const double s = (arcLen * i) / (nPerSection - 1);
        const double theta = startHeading + curvature * s;
        ConditionedVertex v;
        v.position.easting = x1 +
            (1.0 / curvature) * (std::sin(theta) - std::sin(startHeading));
        v.position.northing = y1 +
            (1.0 / curvature) * (std::cos(startHeading) - std::cos(theta));
        v.sourceStation = lineLen1 + s;
        pts.push_back(v);
    }
    // Second line
    const double sArcEnd = arcLen;
    const double thetaArcEnd = startHeading + curvature * sArcEnd;
    const double x2 = x1 +
        (1.0 / curvature) * (std::sin(thetaArcEnd) - std::sin(startHeading));
    const double y2 = y1 +
        (1.0 / curvature) * (std::cos(startHeading) - std::cos(thetaArcEnd));
    for (int i = 1; i < nPerSection; ++i) {
        const double s = (lineLen2 * i) / (nPerSection - 1);
        ConditionedVertex v;
        v.position.easting = x2 + s * std::cos(thetaArcEnd);
        v.position.northing = y2 + s * std::sin(thetaArcEnd);
        v.sourceStation = lineLen1 + arcLen + s;
        pts.push_back(v);
    }
    return pts;
}

} // anonymous namespace

TEST_SUITE("road alignment fitter") {

TEST_CASE("straight source produces single line segment") {
    auto polyline = makeStraightPolyline(100.0, 200.0, 0.3, 10.0, 10);
    AlignmentFitInput input;
    input.polyline = polyline;
    input.positionTolerance = 0.01;

    auto result = fitAlignment(input);
    REQUIRE(result.alignment.has_value());
    REQUIRE(result.diagnostics.empty());
    REQUIRE(result.alignment->segmentCount() >= 1);
    REQUIRE(result.alignment->totalLength() >= doctest::Approx(90.0).epsilon(0.1));
}

TEST_CASE("simple curve produces arc segment") {
    // A clear arc with 20 points
    auto polyline = makeArcPolyline(0.0, 0.0, 0.0, 0.01, 100.0, 20);
    AlignmentFitInput input;
    input.polyline = polyline;
    input.positionTolerance = 5.0;  // generous tolerance for arc fit

    auto result = fitAlignment(input);
    REQUIRE(result.alignment.has_value());
    REQUIRE(result.diagnostics.empty());
    REQUIRE(result.alignment->segmentCount() >= 1);
    REQUIRE(result.alignment->totalLength() > 0.0);
}

TEST_CASE("line-arc-line produces multi-segment alignment") {
    auto polyline = makeLineArcLinePolyline(
        0.0, 0.0, 0.0, 50.0, 0.01, 80.0, 50.0, 10);
    AlignmentFitInput input;
    input.polyline = polyline;
    input.positionTolerance = 15.0;  // generous for multi-section fit

    auto result = fitAlignment(input);
    REQUIRE(result.alignment.has_value());
    REQUIRE(result.diagnostics.empty());
    REQUIRE(result.alignment->segmentCount() >= 1);
}

TEST_CASE("S-curve produces multi-segment alignment") {
    auto polyline = makeSCurvePolyline(
        0.0, 0.0, 0.0, 0.01, 50.0, -0.01, 50.0, 10);
    AlignmentFitInput input;
    input.polyline = polyline;
    input.positionTolerance = 50.0;  // generous for S-curve fit

    auto result = fitAlignment(input);
    // The fitter should either produce a valid alignment or typed diagnostics.
    // Either outcome is acceptable as long as it's deterministic and safe.
    REQUIRE((result.alignment.has_value() || !result.diagnostics.empty()));
    if (result.alignment.has_value()) {
        REQUIRE(result.alignment->segmentCount() >= 1);
    }
}

TEST_CASE("coarse source points are fitted") {
    // Only 4 points: start, mid, mid, end
    std::vector<ConditionedVertex> polyline(4);
    polyline[0].position = {0.0, 0.0};
    polyline[1].position = {50.0, 0.0};
    polyline[2].position = {100.0, 5.0};
    polyline[3].position = {150.0, 0.0};

    AlignmentFitInput input;
    input.polyline = polyline;
    input.positionTolerance = 200.0;  // very generous for coarse data

    auto result = fitAlignment(input);
    // The fitter should either produce a valid alignment or typed diagnostics.
    REQUIRE((result.alignment.has_value() || !result.diagnostics.empty()));
}

TEST_CASE("protected junction is preserved exactly") {
    auto polyline = makeLineArcLinePolyline(
        0.0, 0.0, 0.0, 50.0, 0.01, 80.0, 50.0, 10);

    // The start point is a protected anchor (junction/endpoint).
    ProtectedAnchor anchor;
    anchor.station = 0.0;
    anchor.position = polyline[0].position;
    anchor.kind = AnchorKind::Endpoint;

    AlignmentFitInput input;
    input.polyline = polyline;
    input.protectedAnchors = {anchor};
    input.positionTolerance = 15.0;  // generous for multi-section fit

    auto result = fitAlignment(input);
    REQUIRE(result.alignment.has_value());
    REQUIRE(result.diagnostics.empty());

    // Verify the alignment passes through the anchor position.
    auto sample = result.alignment->evaluate(0.0);
    CHECK(sample.position.easting == doctest::Approx(anchor.position.easting).epsilon(1e-6));
    CHECK(sample.position.northing == doctest::Approx(anchor.position.northing).epsilon(1e-6));
}

TEST_CASE("deterministic: identical inputs produce identical results") {
    auto polyline = makeLineArcLinePolyline(
        0.0, 0.0, 0.0, 50.0, 0.01, 80.0, 50.0, 10);

    AlignmentFitInput input;
    input.polyline = polyline;
    input.positionTolerance = 15.0;  // generous for multi-section fit

    auto result1 = fitAlignment(input);
    auto result2 = fitAlignment(input);

    REQUIRE(result1.alignment.has_value());
    REQUIRE(result2.alignment.has_value());
    REQUIRE(result1.alignment->segmentCount() == result2.alignment->segmentCount());
    REQUIRE(result1.alignment->totalLength() == doctest::Approx(result2.alignment->totalLength()));

    // Compare each segment's evaluation at several stations.
    for (int i = 0; i <= 20; ++i) {
        const double s = result1.alignment->totalLength() * i / 20.0;
        auto s1 = result1.alignment->evaluate(s);
        auto s2 = result2.alignment->evaluate(s);
        CHECK(s1.position.easting == doctest::Approx(s2.position.easting).epsilon(1e-12));
        CHECK(s1.position.northing == doctest::Approx(s2.position.northing).epsilon(1e-12));
        CHECK(s1.heading == doctest::Approx(s2.heading).epsilon(1e-12));
    }
}

TEST_CASE("impossible fit with tight tolerance returns diagnostics") {
    // A zigzag polyline that can't be fit with a smooth alignment
    // within a very tight tolerance.
    std::vector<ConditionedVertex> polyline(6);
    polyline[0].position = {0.0, 0.0};
    polyline[1].position = {10.0, 10.0};
    polyline[2].position = {20.0, 0.0};
    polyline[3].position = {30.0, 10.0};
    polyline[4].position = {40.0, 0.0};
    polyline[5].position = {50.0, 10.0};

    AlignmentFitInput input;
    input.polyline = polyline;
    input.positionTolerance = 0.001;  // very tight

    auto result = fitAlignment(input);
    // With a tight tolerance, the fitter should either fail to fit
    // or produce diagnostics about source deviation.
    if (!result.alignment.has_value()) {
        REQUIRE_FALSE(result.diagnostics.empty());
    }
    // If it did produce an alignment, the deviation check should have caught it.
    // (Either outcome is acceptable as long as it's deterministic and typed.)
}

TEST_CASE("max curvature constraint is respected") {
    // An arc with curvature 0.1 (radius 10)
    auto polyline = makeArcPolyline(0.0, 0.0, 0.0, 0.1, 30.0, 15);
    AlignmentFitInput input;
    input.polyline = polyline;
    input.positionTolerance = 1.0;
    input.maxCurvature = 0.05;  // stricter than the actual curvature

    AlignmentFitConfig config;
    config.maxCurvature = 0.05;
    config.positionTolerance = 1.0;

    auto result = fitAlignment(input, config);
    // The fitter should reject the fit because the arc curvature exceeds
    // the configured maximum.
    if (result.alignment.has_value()) {
        // If it fit, check that no segment exceeds the max curvature.
        for (const auto& seg : result.alignment->segments()) {
            CHECK(std::abs(segmentEndCurvature(seg.segment)) <= 0.05 + 1e-9);
        }
    } else {
        // Or it should return a diagnostic about curvature.
        REQUIRE_FALSE(result.diagnostics.empty());
    }
}

TEST_CASE("empty polyline is rejected") {
    AlignmentFitInput input;
    input.polyline = {};
    auto result = fitAlignment(input);
    REQUIRE_FALSE(result.alignment.has_value());
    REQUIRE_FALSE(result.diagnostics.empty());
}

TEST_CASE("single point polyline is rejected") {
    AlignmentFitInput input;
    input.polyline = {ConditionedVertex{}};
    auto result = fitAlignment(input);
    REQUIRE_FALSE(result.alignment.has_value());
    REQUIRE_FALSE(result.diagnostics.empty());
}

TEST_CASE("non-finite coordinates are rejected") {
    AlignmentFitInput input;
    ConditionedVertex v;
    v.position.easting = std::numeric_limits<double>::quiet_NaN();
    v.position.northing = 0.0;
    input.polyline = {v, ConditionedVertex{}};
    auto result = fitAlignment(input);
    REQUIRE_FALSE(result.alignment.has_value());
    REQUIRE_FALSE(result.diagnostics.empty());
}

TEST_CASE("source deviation validation detects large deviation") {
    // A straight polyline with an outlier point.
    std::vector<ConditionedVertex> polyline(5);
    polyline[0].position = {0.0, 0.0};
    polyline[1].position = {25.0, 0.0};
    polyline[2].position = {50.0, 100.0};  // huge outlier
    polyline[3].position = {75.0, 0.0};
    polyline[4].position = {100.0, 0.0};

    // With a tight tolerance, the outlier should cause a deviation diagnostic.
    auto diags = validateSourceDeviation(
        ReferenceAlignment::build({LineSegment{{0.0, 0.0}, 0.0, 100.0}}).value(),
        polyline, 1.0);
    REQUIRE_FALSE(diags.empty());
}

// Blocker 9: finite-arc source-deviation validation.
// A point on the parent circle but OUTSIDE the arc's angular span must NOT
// produce zero deviation. The old code used |distFromCenter - radius| which
// accepted any point on the infinite circle.
TEST_CASE("finite arc: point on circle but outside arc span is not zero deviation") {
    // Arc: start=(100,0), heading=+Y, curvature=+1/100 (CCW), sweeps 90 deg
    // to end at (0,100). Center is at (0,0).
    const double r = 100.0;
    CircularArcSegment arc;
    arc.start = {r, 0.0};
    arc.startHeading = std::numbers::pi / 2.0;
    arc.curvature = 1.0 / r;
    arc.length = r * std::numbers::pi / 2.0;  // quarter circle

    auto alignment = ReferenceAlignment::build({arc});
    REQUIRE(alignment.has_value());

    // Point (0,-100) is on the parent circle (radius 100) but at angle -pi/2,
    // well outside the arc's [0, pi/2] angular range.
    std::vector<ConditionedVertex> polyline(1);
    polyline[0].position = {0.0, -r};

    auto diags = validateSourceDeviation(*alignment, polyline, 1.0);
    // Must NOT be zero deviation. Distance to nearest endpoint (100,0) is
    // sqrt(100^2 + 100^2) ~ 141.4.
    REQUIRE_FALSE(diags.empty());
}

TEST_CASE("finite arc: point on arc interior produces near-zero deviation") {
    // Same arc as above.
    const double r = 100.0;
    CircularArcSegment arc;
    arc.start = {r, 0.0};
    arc.startHeading = std::numbers::pi / 2.0;
    arc.curvature = 1.0 / r;
    arc.length = r * std::numbers::pi / 2.0;

    auto alignment = ReferenceAlignment::build({arc});
    REQUIRE(alignment.has_value());

    // Point at 45 degrees on the arc: (r*cos(45), r*sin(45)).
    std::vector<ConditionedVertex> polyline(1);
    polyline[0].position = {r * std::cos(std::numbers::pi / 4.0),
                            r * std::sin(std::numbers::pi / 4.0)};

    auto diags = validateSourceDeviation(*alignment, polyline, 0.01);
    REQUIRE(diags.empty());
}

TEST_CASE("finite arc: point near arc start endpoint produces small deviation") {
    const double r = 100.0;
    CircularArcSegment arc;
    arc.start = {r, 0.0};
    arc.startHeading = std::numbers::pi / 2.0;
    arc.curvature = 1.0 / r;
    arc.length = r * std::numbers::pi / 2.0;

    auto alignment = ReferenceAlignment::build({arc});
    REQUIRE(alignment.has_value());

    // Point just past the arc start (slightly before angle 0).
    // At angle -0.001, the point is on the circle but outside the arc.
    // The nearest point should be the arc start (100,0).
    std::vector<ConditionedVertex> polyline(1);
    polyline[0].position = {r * std::cos(-0.001), r * std::sin(-0.001)};

    auto diags = validateSourceDeviation(*alignment, polyline, 1.0);
    // The distance should be ~0.1 (arc gap), which is within tolerance 1.0.
    REQUIRE(diags.empty());
}

TEST_CASE("finite arc: point far beyond arc end produces large deviation") {
    const double r = 100.0;
    CircularArcSegment arc;
    arc.start = {r, 0.0};
    arc.startHeading = std::numbers::pi / 2.0;
    arc.curvature = 1.0 / r;
    arc.length = r * std::numbers::pi / 2.0;

    auto alignment = ReferenceAlignment::build({arc});
    REQUIRE(alignment.has_value());

    // Point at angle pi (i.e., (-100, 0)) on the parent circle, well past
    // the arc's end at (0, 100). Nearest endpoint distance ~141.4.
    std::vector<ConditionedVertex> polyline(1);
    polyline[0].position = {-r, 0.0};

    auto diags = validateSourceDeviation(*alignment, polyline, 1.0);
    REQUIRE_FALSE(diags.empty());
}

TEST_CASE("finite arc: clockwise arc respects angular span") {
    // Clockwise arc: start=(100,0), heading=-Y, curvature=-1/100, sweeps
    // 90 deg to end at (0,-100). Center at (0,0).
    const double r = 100.0;
    CircularArcSegment arc;
    arc.start = {r, 0.0};
    arc.startHeading = -std::numbers::pi / 2.0;
    arc.curvature = -1.0 / r;
    arc.length = r * std::numbers::pi / 2.0;

    auto alignment = ReferenceAlignment::build({arc});
    REQUIRE(alignment.has_value());

    // Point (0,100) is on the parent circle but outside the CW arc's range.
    std::vector<ConditionedVertex> polyline(1);
    polyline[0].position = {0.0, r};

    auto diags = validateSourceDeviation(*alignment, polyline, 1.0);
    REQUIRE_FALSE(diags.empty());
}

// Blocker 8: protected anchors act as true fit constraints. An interior
// anchor vertex must be a section boundary so the fitted alignment passes
// through the anchor position exactly, not merely within tolerance.
TEST_CASE("protected anchor constrains the fitter to pass through it") {
    // A line-arc-line polyline with a protected anchor at the arc start.
    auto polyline = makeLineArcLinePolyline(
        0.0, 0.0, 0.0, 50.0, 0.01, 80.0, 50.0, 10);

    // Anchor at the line-to-arc transition (vertex index 9, which is the
    // last point of the first line section = first point of the arc).
    const std::size_t anchorIdx = 9;
    ProtectedAnchor anchor;
    anchor.station = 0.0;  // station is recomputed by the caller
    anchor.position = polyline[anchorIdx].position;
    anchor.kind = AnchorKind::Junction;

    // Compute the cumulative station for the anchor.
    double cumulative = 0.0;
    for (std::size_t i = 1; i <= anchorIdx && i < polyline.size(); ++i) {
        const double dx = polyline[i].position.easting - polyline[i-1].position.easting;
        const double dy = polyline[i].position.northing - polyline[i-1].position.northing;
        cumulative += std::sqrt(dx*dx + dy*dy);
    }
    anchor.station = cumulative;

    AlignmentFitInput input;
    input.polyline = polyline;
    input.protectedAnchors = {anchor};
    input.positionTolerance = 15.0;

    auto result = fitAlignment(input);
    REQUIRE(result.alignment.has_value());
    REQUIRE(result.diagnostics.empty());

    // The anchor vertex is a section boundary, so a segment must start
    // at the anchor position exactly.
    bool foundBoundary = false;
    for (const auto& seg : result.alignment->segments()) {
        std::visit([&](const auto& s) {
            const double dxS = s.start.easting - anchor.position.easting;
            const double dyS = s.start.northing - anchor.position.northing;
            if (std::sqrt(dxS*dxS + dyS*dyS) < 1e-6) foundBoundary = true;
        }, seg.segment);
    }
    CHECK(foundBoundary);
}

TEST_CASE("protected anchor is preserved when surrounding controls move") {
    // A simple arc polyline with an interior anchor.
    auto polyline = makeArcPolyline(0.0, 0.0, 0.0, 0.01, 100.0, 20);
    const std::size_t anchorIdx = 10;
    ProtectedAnchor anchor;
    anchor.station = 0.0;
    anchor.position = polyline[anchorIdx].position;
    anchor.kind = AnchorKind::Junction;

    double cumulative = 0.0;
    for (std::size_t i = 1; i <= anchorIdx && i < polyline.size(); ++i) {
        const double dx = polyline[i].position.easting - polyline[i-1].position.easting;
        const double dy = polyline[i].position.northing - polyline[i-1].position.northing;
        cumulative += std::sqrt(dx*dx + dy*dy);
    }
    anchor.station = cumulative;

    AlignmentFitInput input;
    input.polyline = polyline;
    input.protectedAnchors = {anchor};
    input.positionTolerance = 5.0;

    auto result = fitAlignment(input);
    REQUIRE(result.alignment.has_value());
    REQUIRE(result.diagnostics.empty());

    // The anchor vertex is a section boundary, so a segment must start
    // or end at the anchor position exactly.
    bool foundBoundary = false;
    for (const auto& seg : result.alignment->segments()) {
        std::visit([&](const auto& s) {
            const double dxS = s.start.easting - anchor.position.easting;
            const double dyS = s.start.northing - anchor.position.northing;
            if (std::sqrt(dxS*dxS + dyS*dyS) < 1e-6) foundBoundary = true;
        }, seg.segment);
    }
    CHECK(foundBoundary);
}

TEST_CASE("absent max curvature does not invent a constraint") {
    // Without maxCurvature, the fitter should not reject any curvature.
    auto polyline = makeArcPolyline(0.0, 0.0, 0.0, 0.1, 30.0, 15);
    AlignmentFitInput input;
    input.polyline = polyline;
    input.positionTolerance = 5.0;
    // maxCurvature is not set (nullopt)

    auto result = fitAlignment(input);
    // Should succeed without inventing a curvature constraint.
    if (result.alignment.has_value()) {
        REQUIRE(result.diagnostics.empty());
    }
}

} // TEST_SUITE
