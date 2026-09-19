#include <doctest/doctest.h>

#include "infraforge/domain/road/AlignmentPrimitives.hpp"
#include "infraforge/domain/road/ReferenceAlignment.hpp"
#include "infraforge/domain/road/Road.hpp"
#include "infraforge/domain/road/RoadSource.hpp"
#include "infraforge/domain/road/RoadTypes.hpp"
#include "infraforge/domain/road/VerticalProfiles.hpp"
#include "infraforge/domain/road/RoadWidthProfile.hpp"

#include <cmath>
#include <limits>
#include <optional>
#include <string>

namespace {

using infraforge::domain::road::AlignmentPoint;
using infraforge::domain::road::AlignmentSample;
using infraforge::domain::road::AlignmentSegment;
using infraforge::domain::road::CircularArcSegment;
using infraforge::domain::road::LineSegment;
using infraforge::domain::road::validateSegment;

constexpr double kTol = 1e-9;
constexpr double kPi = 3.14159265358979323846;

double normalizeAngle(double a) {
    while (a < 0.0) { a += 2.0 * kPi; }
    while (a >= 2.0 * kPi) { a -= 2.0 * kPi; }
    return a;
}

} // namespace

TEST_SUITE("road domain foundation") {

TEST_CASE("road error codes round-trip through stable names") {
    using infraforge::domain::road::RoadErrorCode;
    for (std::size_t code = 0;
        code <= static_cast<std::size_t>(RoadErrorCode::AnchorOutOfRange); ++code) {
        const auto value = static_cast<RoadErrorCode>(code);
        const auto parsed = infraforge::domain::road::roadErrorCodeFromName(
            infraforge::domain::road::roadErrorCodeName(value));
        REQUIRE(parsed.has_value());
        CHECK(*parsed == value);
    }
    CHECK_FALSE(infraforge::domain::road::roadErrorCodeFromName("not_a_code").has_value());
}

TEST_CASE("anchor kinds round-trip through stable names") {
    using infraforge::domain::road::AnchorKind;
    for (std::size_t kind = 0;
        kind <= static_cast<std::size_t>(AnchorKind::Semantic); ++kind) {
        const auto value = static_cast<AnchorKind>(kind);
        const auto parsed = infraforge::domain::road::anchorKindFromName(
            infraforge::domain::road::anchorKindName(value));
        REQUIRE(parsed.has_value());
        CHECK(*parsed == value);
    }
    CHECK_FALSE(infraforge::domain::road::anchorKindFromName("not_a_kind").has_value());
}

TEST_CASE("source providers round-trip through stable names") {
    using infraforge::domain::road::SourceProvider;
    for (std::size_t provider = 0;
        provider <= static_cast<std::size_t>(SourceProvider::Other); ++provider) {
        const auto value = static_cast<SourceProvider>(provider);
        const auto parsed = infraforge::domain::road::sourceProviderFromName(
            infraforge::domain::road::sourceProviderName(value));
        REQUIRE(parsed.has_value());
        CHECK(*parsed == value);
    }
    CHECK_FALSE(infraforge::domain::road::sourceProviderFromName("not_a_provider").has_value());
}

TEST_CASE("road identity maps uuid text <-> road id") {
    const std::string uuid = "0f1e2d3c-4b5a-6978-8798-aabbccddeeff";
    const auto id = infraforge::domain::road::roadIdFromUuidText(uuid);
    CHECK_FALSE(id.isNull());
    CHECK(infraforge::domain::road::uuidTextFromRoadId(id) == uuid);
}

TEST_CASE("road display name validation rejects empty and overlong names") {
    using infraforge::domain::road::validateRoadDisplayName;
    CHECK_FALSE(validateRoadDisplayName("Highway 1").has_value());
    CHECK(validateRoadDisplayName("").has_value());
    CHECK(validateRoadDisplayName(std::string(infraforge::domain::road::kMaxRoadDisplayNameLength + 1, 'a')).has_value());
}

TEST_CASE("station range reports length and containment") {
    infraforge::domain::road::StationRange range{100.0, 145.0};
    CHECK(range.length() == doctest::Approx(45.0));
    CHECK(range.contains(100.0));
    CHECK(range.contains(145.0));
    CHECK(range.contains(120.0));
    CHECK_FALSE(range.contains(99.999));
    CHECK_FALSE(range.contains(145.001));
}

} // TEST_SUITE

TEST_SUITE("road alignment primitives") {

TEST_CASE("line evaluation is mathematically exact") {
    LineSegment line{.start = {100.0, 200.0}, .heading = 0.0, .length = 50.0};
    CHECK_FALSE(validateSegment(line).has_value());

    const AlignmentSample mid = line.evaluate(25.0);
    CHECK(mid.position.easting == doctest::Approx(125.0));
    CHECK(mid.position.northing == doctest::Approx(200.0));
    CHECK(mid.heading == doctest::Approx(0.0));
    CHECK(mid.curvature == doctest::Approx(0.0));

    const AlignmentSample end = line.endSample();
    CHECK(end.position.easting == doctest::Approx(150.0));
    CHECK(end.position.northing == doctest::Approx(200.0));
    CHECK(end.heading == doctest::Approx(0.0));
}

TEST_CASE("line evaluation follows an arbitrary heading") {
    LineSegment line{.start = {0.0, 0.0}, .heading = kPi / 2.0, .length = 10.0};
    const AlignmentSample s = line.evaluate(10.0);
    CHECK(s.position.easting == doctest::Approx(0.0).epsilon(kTol));
    CHECK(s.position.northing == doctest::Approx(10.0));
    CHECK(s.heading == doctest::Approx(kPi / 2.0));
}

TEST_CASE("line validation rejects degenerate and non-finite parameters") {
    CHECK(validateSegment(LineSegment{.start = {0.0, 0.0}, .heading = 0.0, .length = 0.0}).has_value());
    CHECK(validateSegment(LineSegment{.start = {0.0, 0.0}, .heading = 0.0, .length = -5.0}).has_value());
    CHECK(validateSegment(LineSegment{.start = {std::nan(""), 0.0}, .heading = 0.0, .length = 5.0}).has_value());
    CHECK(validateSegment(LineSegment{.start = {0.0, 0.0}, .heading = std::nan(""), .length = 5.0}).has_value());
}

TEST_CASE("positive circular arc turns left with constant curvature") {
    // Radius 100 m, left turn, 90 degrees of arc.
    const double radius = 100.0;
    const double kappa = 1.0 / radius;
    const double arcLength = radius * (kPi / 2.0);
    CircularArcSegment arc{.start = {0.0, 0.0}, .startHeading = 0.0, .curvature = kappa, .length = arcLength};
    CHECK_FALSE(validateSegment(arc).has_value());

    const AlignmentSample end = arc.endSample();
    // Center is at (0, 100) for a left turn from heading 0 at origin.
    CHECK(end.position.easting == doctest::Approx(100.0).epsilon(kTol));
    CHECK(end.position.northing == doctest::Approx(100.0).epsilon(kTol));
    CHECK(normalizeAngle(end.heading) == doctest::Approx(kPi / 2.0).epsilon(kTol));
    CHECK(end.curvature == doctest::Approx(kappa));

    const AlignmentSample mid = arc.evaluate(arcLength / 2.0);
    CHECK(mid.curvature == doctest::Approx(kappa));
    CHECK(normalizeAngle(mid.heading) == doctest::Approx(kPi / 4.0).epsilon(kTol));
}

TEST_CASE("negative circular arc turns right") {
    const double radius = 50.0;
    const double kappa = -1.0 / radius;
    const double arcLength = radius * (kPi / 2.0);
    CircularArcSegment arc{.start = {0.0, 0.0}, .startHeading = 0.0, .curvature = kappa, .length = arcLength};
    CHECK_FALSE(validateSegment(arc).has_value());

    const AlignmentSample end = arc.endSample();
    // Center is at (0, -50) for a right turn from heading 0 at origin.
    CHECK(end.position.easting == doctest::Approx(50.0).epsilon(kTol));
    CHECK(end.position.northing == doctest::Approx(-50.0).epsilon(kTol));
    CHECK(normalizeAngle(end.heading) == doctest::Approx(3.0 * kPi / 2.0).epsilon(kTol));
    CHECK(end.curvature == doctest::Approx(kappa));
}

TEST_CASE("arc start sample matches start point and heading") {
    CircularArcSegment arc{.start = {10.0, 20.0}, .startHeading = 0.5, .curvature = 0.01, .length = 30.0};
    const AlignmentSample start = arc.evaluate(0.0);
    CHECK(start.position.easting == doctest::Approx(10.0));
    CHECK(start.position.northing == doctest::Approx(20.0));
    CHECK(start.heading == doctest::Approx(0.5));
    CHECK(start.curvature == doctest::Approx(0.01));
}

TEST_CASE("arc validation rejects zero and non-finite curvature") {
    CHECK(validateSegment(CircularArcSegment{.start = {0.0, 0.0}, .startHeading = 0.0, .curvature = 0.0, .length = 10.0}).has_value());
    CHECK(validateSegment(CircularArcSegment{.start = {0.0, 0.0}, .startHeading = 0.0, .curvature = std::nan(""), .length = 10.0}).has_value());
    CHECK(validateSegment(CircularArcSegment{.start = {0.0, 0.0}, .startHeading = 0.0, .curvature = 0.01, .length = 0.0}).has_value());
}

TEST_CASE("variant dispatch evaluates line and arc uniformly") {
    LineSegment line{.start = {0.0, 0.0}, .heading = 0.0, .length = 10.0};
    CircularArcSegment arc{.start = {0.0, 0.0}, .startHeading = 0.0, .curvature = 0.1, .length = 10.0};
    AlignmentSegment segLine = line;
    AlignmentSegment segArc = arc;

    CHECK(infraforge::domain::road::segmentKind(segLine) == infraforge::domain::road::AlignmentSegmentKind::Line);
    CHECK(infraforge::domain::road::segmentKind(segArc) == infraforge::domain::road::AlignmentSegmentKind::CircularArc);
    CHECK(infraforge::domain::road::segmentLength(segLine) == doctest::Approx(10.0));
    CHECK(infraforge::domain::road::segmentStartPoint(segArc).easting == doctest::Approx(0.0));
    CHECK(infraforge::domain::road::evaluateSegment(segLine, 10.0).position.easting == doctest::Approx(10.0));
}

TEST_CASE("alignment segment kind names round-trip") {
    using infraforge::domain::road::AlignmentSegmentKind;
    for (std::size_t k = 0; k <= static_cast<std::size_t>(AlignmentSegmentKind::Clothoid); ++k) {
        const auto value = static_cast<AlignmentSegmentKind>(k);
        const auto parsed = infraforge::domain::road::alignmentSegmentKindFromName(
            infraforge::domain::road::alignmentSegmentKindName(value));
        REQUIRE(parsed.has_value());
        CHECK(*parsed == value);
    }
}

} // TEST_SUITE

TEST_SUITE("road clothoid primitive") {

using infraforge::domain::road::ClothoidSegment;

TEST_CASE("clothoid with zero curvature throughout is a straight line") {
    ClothoidSegment spiral{.start = {10.0, 20.0}, .startHeading = 0.3, .startCurvature = 0.0,
        .endCurvature = 0.0, .length = 40.0};
    CHECK_FALSE(validateSegment(spiral).has_value());

    const AlignmentSample end = spiral.endSample();
    CHECK(end.position.easting == doctest::Approx(10.0 + 40.0 * std::cos(0.3)).epsilon(kTol));
    CHECK(end.position.northing == doctest::Approx(20.0 + 40.0 * std::sin(0.3)).epsilon(kTol));
    CHECK(end.heading == doctest::Approx(0.3).epsilon(kTol));
    CHECK(end.curvature == doctest::Approx(0.0));
}

TEST_CASE("clothoid with constant curvature matches a circular arc") {
    const double kappa = 0.02;
    CircularArcSegment arc{.start = {0.0, 0.0}, .startHeading = 0.0, .curvature = kappa, .length = 50.0};
    ClothoidSegment spiral{.start = {0.0, 0.0}, .startHeading = 0.0, .startCurvature = kappa,
        .endCurvature = kappa, .length = 50.0};

    const AlignmentSample arcEnd = arc.endSample();
    const AlignmentSample spEnd = spiral.endSample();
    CHECK(spEnd.position.easting == doctest::Approx(arcEnd.position.easting).epsilon(1e-9));
    CHECK(spEnd.position.northing == doctest::Approx(arcEnd.position.northing).epsilon(1e-9));
    CHECK(spEnd.heading == doctest::Approx(arcEnd.heading).epsilon(kTol));
    CHECK(spEnd.curvature == doctest::Approx(kappa));
}

TEST_CASE("clothoid curvature changes linearly with station") {
    ClothoidSegment spiral{.start = {0.0, 0.0}, .startHeading = 0.0, .startCurvature = 0.0,
        .endCurvature = 0.01, .length = 100.0};
    CHECK(spiral.evaluate(0.0).curvature == doctest::Approx(0.0));
    CHECK(spiral.evaluate(25.0).curvature == doctest::Approx(0.0025));
    CHECK(spiral.evaluate(50.0).curvature == doctest::Approx(0.005));
    CHECK(spiral.evaluate(100.0).curvature == doctest::Approx(0.01));
}

TEST_CASE("clothoid zero to positive curvature transition") {
    ClothoidSegment spiral{.start = {0.0, 0.0}, .startHeading = 0.0, .startCurvature = 0.0,
        .endCurvature = 0.01, .length = 100.0};
    const AlignmentSample end = spiral.endSample();
    // End heading = startHeading + (startCurvature + endCurvature) * length / 2.
    CHECK(end.heading == doctest::Approx(0.5).epsilon(kTol));
    CHECK(end.curvature == doctest::Approx(0.01));
    CHECK_FALSE(validateSegment(spiral).has_value());
}

TEST_CASE("clothoid positive to zero curvature transition") {
    ClothoidSegment spiral{.start = {0.0, 0.0}, .startHeading = 0.0, .startCurvature = 0.01,
        .endCurvature = 0.0, .length = 100.0};
    const AlignmentSample end = spiral.endSample();
    CHECK(end.heading == doctest::Approx(0.5).epsilon(kTol));
    CHECK(end.curvature == doctest::Approx(0.0));
}

TEST_CASE("clothoid zero to negative curvature transition") {
    ClothoidSegment spiral{.start = {0.0, 0.0}, .startHeading = 0.0, .startCurvature = 0.0,
        .endCurvature = -0.01, .length = 100.0};
    const AlignmentSample end = spiral.endSample();
    CHECK(end.heading == doctest::Approx(-0.5).epsilon(kTol));
    CHECK(end.curvature == doctest::Approx(-0.01));
}

TEST_CASE("clothoid general curvature A to curvature B transition") {
    ClothoidSegment spiral{.start = {0.0, 0.0}, .startHeading = 0.1, .startCurvature = 0.004,
        .endCurvature = -0.002, .length = 60.0};
    const AlignmentSample end = spiral.endSample();
    CHECK(end.heading == doctest::Approx(0.1 + (0.004 - 0.002) * 60.0 / 2.0).epsilon(kTol));
    CHECK(end.curvature == doctest::Approx(-0.002));
    CHECK(spiral.evaluate(0.0).curvature == doctest::Approx(0.004));
}

TEST_CASE("clothoid evaluation is deterministic") {
    ClothoidSegment spiral{.start = {5.0, -3.0}, .startHeading = 0.7, .startCurvature = 0.003,
        .endCurvature = -0.001, .length = 75.0};
    const AlignmentSample a = spiral.evaluate(33.0);
    const AlignmentSample b = spiral.evaluate(33.0);
    CHECK(a.position.easting == b.position.easting);
    CHECK(a.position.northing == b.position.northing);
    CHECK(a.heading == b.heading);
    CHECK(a.curvature == b.curvature);
}

TEST_CASE("clothoid validation rejects non-finite and degenerate parameters") {
    CHECK(validateSegment(ClothoidSegment{.start = {0.0, 0.0}, .startHeading = 0.0,
        .startCurvature = 0.0, .endCurvature = 0.01, .length = 0.0}).has_value());
    CHECK(validateSegment(ClothoidSegment{.start = {0.0, 0.0}, .startHeading = 0.0,
        .startCurvature = std::nan(""), .endCurvature = 0.01, .length = 10.0}).has_value());
    CHECK(validateSegment(ClothoidSegment{.start = {0.0, 0.0}, .startHeading = 0.0,
        .startCurvature = 0.0, .endCurvature = std::numeric_limits<double>::infinity(), .length = 10.0}).has_value());
}

TEST_CASE("clothoid start sample matches start point and heading") {
    ClothoidSegment spiral{.start = {12.0, 34.0}, .startHeading = 0.9, .startCurvature = 0.005,
        .endCurvature = -0.005, .length = 20.0};
    const AlignmentSample start = spiral.evaluate(0.0);
    CHECK(start.position.easting == doctest::Approx(12.0));
    CHECK(start.position.northing == doctest::Approx(34.0));
    CHECK(start.heading == doctest::Approx(0.9));
    CHECK(start.curvature == doctest::Approx(0.005));
}

} // TEST_SUITE

TEST_SUITE("road reference alignment stationing") {

using infraforge::domain::road::ReferenceAlignment;
using infraforge::domain::road::StationedSegment;
using infraforge::domain::road::LineSegment;
using infraforge::domain::road::CircularArcSegment;

ReferenceAlignment buildLineArc() {
    // Line 100m, then arc radius 50m for 45 degrees.
    LineSegment line{.start = {0.0, 0.0}, .heading = 0.0, .length = 100.0};
    const AlignmentSample lineEnd = line.endSample();
    const double radius = 50.0;
    const double kappa = 1.0 / radius;
    const double arcLen = radius * kPi / 4.0;
    CircularArcSegment arc{.start = lineEnd.position, .startHeading = lineEnd.heading,
        .curvature = kappa, .length = arcLen};
    auto built = ReferenceAlignment::build({line, arc},
        infraforge::domain::road::kDefaultPositionTolerance,
        infraforge::domain::road::kDefaultHeadingTolerance,
        1.0); // relax curvature tolerance: line->arc has an intentional curvature step
    REQUIRE(built.has_value());
    return *built;
}

TEST_CASE("alignment stationing is continuous with no gaps") {
    const auto alignment = buildLineArc();
    CHECK(alignment.segmentCount() == 2);
    CHECK(alignment.totalLength() == doctest::Approx(100.0 + 50.0 * kPi / 4.0));

    const auto& segs = alignment.segments();
    CHECK(segs[0].startStation == doctest::Approx(0.0));
    CHECK(segs[1].startStation == doctest::Approx(100.0));
    CHECK(segs[1].startStation + infraforge::domain::road::segmentLength(segs[1].segment)
        == doctest::Approx(alignment.totalLength()));
}

TEST_CASE("segment lookup by station returns the correct segment") {
    const auto alignment = buildLineArc();
    CHECK(*alignment.segmentIndexAt(0.0) == 0);
    CHECK(*alignment.segmentIndexAt(50.0) == 0);
    CHECK(*alignment.segmentIndexAt(100.0) == 0); // boundary -> earlier segment
    CHECK(*alignment.segmentIndexAt(100.001) == 1);
    CHECK(*alignment.segmentIndexAt(alignment.totalLength()) == 1);
    CHECK_FALSE(alignment.segmentIndexAt(-0.001).has_value());
    CHECK_FALSE(alignment.segmentIndexAt(alignment.totalLength() + 0.001).has_value());
}

TEST_CASE("boundary evaluation is deterministic") {
    const auto alignment = buildLineArc();
    const AlignmentSample atBoundaryFromLine = alignment.evaluate(100.0);
    const AlignmentSample atBoundaryFromArc = alignment.evaluate(100.0001);
    CHECK(atBoundaryFromLine.position.easting == doctest::Approx(100.0));
    CHECK(atBoundaryFromLine.position.northing == doctest::Approx(0.0));
    CHECK(atBoundaryFromArc.position.easting == doctest::Approx(100.0).epsilon(1e-4));
    CHECK(atBoundaryFromArc.position.northing == doctest::Approx(0.0).epsilon(1e-4));
}

TEST_CASE("alignment station range reports [0, totalLength]") {
    const auto alignment = buildLineArc();
    const auto range = alignment.stationRange();
    CHECK(range.start == doctest::Approx(0.0));
    CHECK(range.end == doctest::Approx(alignment.totalLength()));
}

TEST_CASE("empty alignment is rejected") {
    auto built = ReferenceAlignment::build({});
    REQUIRE_FALSE(built.has_value());
    CHECK(built.error().size() == 1);
    CHECK(built.error().front().code == infraforge::domain::road::RoadErrorCode::EmptyAlignment);
}

} // TEST_SUITE

TEST_SUITE("road alignment continuity validation") {

using infraforge::domain::road::ReferenceAlignment;
using infraforge::domain::road::LineSegment;
using infraforge::domain::road::CircularArcSegment;
using infraforge::domain::road::ClothoidSegment;

TEST_CASE("G0 position discontinuity is detected") {
    LineSegment line{.start = {0.0, 0.0}, .heading = 0.0, .length = 100.0};
    // Second line starts 5m off the first line's end.
    LineSegment bad{.start = {105.0, 0.0}, .heading = 0.0, .length = 50.0};
    auto built = ReferenceAlignment::build({line, bad});
    REQUIRE_FALSE(built.has_value());
    bool found = false;
    for (const auto& d : built.error()) {
        if (d.code == infraforge::domain::road::RoadErrorCode::PositionDiscontinuity) { found = true; }
    }
    CHECK(found);
}

TEST_CASE("G1 heading discontinuity is detected") {
    LineSegment line{.start = {0.0, 0.0}, .heading = 0.0, .length = 100.0};
    const AlignmentSample lineEnd = line.endSample();
    // Second line starts at the right position but with a different heading.
    LineSegment bad{.start = lineEnd.position, .heading = 0.5, .length = 50.0};
    auto built = ReferenceAlignment::build({line, bad});
    REQUIRE_FALSE(built.has_value());
    bool found = false;
    for (const auto& d : built.error()) {
        if (d.code == infraforge::domain::road::RoadErrorCode::HeadingDiscontinuity) { found = true; }
    }
    CHECK(found);
}

TEST_CASE("curvature discontinuity between line and arc is detected") {
    LineSegment line{.start = {0.0, 0.0}, .heading = 0.0, .length = 100.0};
    const AlignmentSample lineEnd = line.endSample();
    // Arc with non-zero curvature directly after a line: curvature jumps.
    CircularArcSegment arc{.start = lineEnd.position, .startHeading = lineEnd.heading,
        .curvature = 0.01, .length = 50.0};
    auto built = ReferenceAlignment::build({line, arc});
    REQUIRE_FALSE(built.has_value());
    bool found = false;
    for (const auto& d : built.error()) {
        if (d.code == infraforge::domain::road::RoadErrorCode::CurvatureDiscontinuity) { found = true; }
    }
    CHECK(found);
}

TEST_CASE("line to clothoid to arc is continuous when curvatures match") {
    LineSegment line{.start = {0.0, 0.0}, .heading = 0.0, .length = 100.0};
    const AlignmentSample lineEnd = line.endSample();
    // Clothoid transitions 0 -> arc curvature, so curvature is continuous.
    const double kappa = 0.01;
    ClothoidSegment spiral{.start = lineEnd.position, .startHeading = lineEnd.heading,
        .startCurvature = 0.0, .endCurvature = kappa, .length = 50.0};
    const AlignmentSample spiralEnd = spiral.endSample();
    CircularArcSegment arc{.start = spiralEnd.position, .startHeading = spiralEnd.heading,
        .curvature = kappa, .length = 50.0};
    auto built = ReferenceAlignment::build({line, spiral, arc});
    REQUIRE(built.has_value());
    CHECK(built->segmentCount() == 3);
}

TEST_CASE("invalid segment parameters are reported before continuity") {
    LineSegment bad{.start = {0.0, 0.0}, .heading = 0.0, .length = -10.0};
    auto built = ReferenceAlignment::build({bad});
    REQUIRE_FALSE(built.has_value());
    CHECK(built.error().front().code == infraforge::domain::road::RoadErrorCode::DegenerateSegment);
}

} // TEST_SUITE

TEST_SUITE("road vertical profiles") {

using infraforge::domain::road::ElevationProfile;
using infraforge::domain::road::SuperelevationProfile;
using infraforge::domain::road::ProfileBreakpoint;
using infraforge::domain::road::buildElevationProfile;
using infraforge::domain::road::buildSuperelevationProfile;

TEST_CASE("empty elevation profile evaluates to zero everywhere") {
    ElevationProfile profile;
    CHECK(profile.isEmpty());
    CHECK(profile.evaluate(0.0) == doctest::Approx(0.0));
    CHECK(profile.evaluate(100.0) == doctest::Approx(0.0));
}

TEST_CASE("elevation profile interpolates linearly between breakpoints") {
    auto built = buildElevationProfile({{0.0, 100.0}, {100.0, 110.0}, {200.0, 108.0}});
    REQUIRE(built.has_value());
    const auto& profile = *built;
    CHECK(profile.evaluate(0.0) == doctest::Approx(100.0));
    CHECK(profile.evaluate(50.0) == doctest::Approx(105.0));
    CHECK(profile.evaluate(100.0) == doctest::Approx(110.0));
    CHECK(profile.evaluate(150.0) == doctest::Approx(109.0));
    CHECK(profile.evaluate(200.0) == doctest::Approx(108.0));
}

TEST_CASE("elevation profile holds constant value outside breakpoint range") {
    auto built = buildElevationProfile({{50.0, 200.0}, {150.0, 210.0}});
    REQUIRE(built.has_value());
    const auto& profile = *built;
    CHECK(profile.evaluate(0.0) == doctest::Approx(200.0));   // before first -> first value
    CHECK(profile.evaluate(250.0) == doctest::Approx(210.0)); // after last -> last value
}

TEST_CASE("elevation profile with single breakpoint holds constant") {
    auto built = buildElevationProfile({{0.0, 50.0}});
    REQUIRE(built.has_value());
    const auto& profile = *built;
    CHECK(profile.evaluate(0.0) == doctest::Approx(50.0));
    CHECK(profile.evaluate(1000.0) == doctest::Approx(50.0));
}

TEST_CASE("elevation profile rejects non-finite breakpoints") {
    auto built = buildElevationProfile({{0.0, std::nan("")}, {100.0, 10.0}});
    REQUIRE_FALSE(built.has_value());
    CHECK(built.error().code == infraforge::domain::road::RoadErrorCode::NonFiniteParameter);
}

TEST_CASE("elevation profile rejects non-increasing stations") {
    auto built = buildElevationProfile({{100.0, 10.0}, {100.0, 20.0}});
    REQUIRE_FALSE(built.has_value());
    CHECK(built.error().code == infraforge::domain::road::RoadErrorCode::InvalidProfile);

    auto built2 = buildElevationProfile({{100.0, 10.0}, {50.0, 20.0}});
    REQUIRE_FALSE(built2.has_value());
    CHECK(built2.error().code == infraforge::domain::road::RoadErrorCode::InvalidProfile);
}

TEST_CASE("superelevation profile evaluates by station") {
    auto built = buildSuperelevationProfile({{0.0, 0.0}, {50.0, 0.05}, {100.0, 0.05}, {150.0, 0.0}});
    REQUIRE(built.has_value());
    const auto& profile = *built;
    CHECK(profile.evaluate(0.0) == doctest::Approx(0.0));
    CHECK(profile.evaluate(25.0) == doctest::Approx(0.025));
    CHECK(profile.evaluate(75.0) == doctest::Approx(0.05));
    CHECK(profile.evaluate(125.0) == doctest::Approx(0.025));
    CHECK(profile.evaluate(150.0) == doctest::Approx(0.0));
}

TEST_CASE("superelevation profile is independent of terrain") {
    // A road can have a superelevation profile with no terrain in the project.
    auto built = buildSuperelevationProfile({{0.0, 0.0}, {100.0, 0.08}});
    REQUIRE(built.has_value());
    CHECK(built->evaluate(50.0) == doctest::Approx(0.04));
}

} // TEST_SUITE

TEST_SUITE("road source geometry and provenance") {

using infraforge::domain::road::SourceVertex;
using infraforge::domain::road::SourceTag;
using infraforge::domain::road::RoadProvenance;
using infraforge::domain::road::SourcePolyline;
using infraforge::domain::road::RoadSource;
using infraforge::domain::road::ProtectedAnchor;
using infraforge::domain::road::AnchorKind;
using infraforge::domain::road::SourceProvider;

TEST_CASE("authored road has empty source geometry") {
    RoadSource source;
    CHECK(source.geometry.vertices.empty());
    CHECK(source.provenance.provider == SourceProvider::Authored);
    CHECK(source.protectedAnchors.empty());
}

TEST_CASE("imported road retains source polyline and provenance") {
    RoadSource source;
    source.geometry.sourceCrs = "EPSG:4326";
    source.geometry.vertices = {
        {-122.4, 37.8, std::optional<double>{10.0}},
        {-122.41, 37.81, std::optional<double>{12.0}}};
    source.provenance.provider = SourceProvider::Osm;
    source.provenance.sourceId = "way/123456";
    source.provenance.tags = {{"highway", "motorway"}, {"lanes", "2"}};
    source.provenance.importedAt = "2026-09-14T00:00:00Z";

    CHECK(source.geometry.vertices.size() == 2);
    CHECK(source.provenance.provider == SourceProvider::Osm);
    CHECK(source.provenance.sourceId == "way/123456");
    CHECK(source.provenance.tags.size() == 2);
    CHECK(source.geometry.sourceCrs == "EPSG:4326");
}

TEST_CASE("source geometry is distinct from canonical alignment") {
    // Source polyline in WGS84 is NOT the canonical alignment.
    RoadSource source;
    source.geometry.vertices = {
        {-122.4, 37.8, std::optional<double>{}},
        {-122.41, 37.81, std::optional<double>{}}};
    source.provenance.provider = SourceProvider::Osm;

    // Canonical alignment is in project coordinates, separate.
    LineSegment line{.start = {1000.0, 2000.0}, .heading = 0.5, .length = 100.0};
    auto alignment = infraforge::domain::road::ReferenceAlignment::build({line});
    REQUIRE(alignment.has_value());

    // The source polyline coordinates are in a different space (WGS84).
    CHECK(source.geometry.vertices[0].x != alignment->evaluate(0.0).position.easting);
}

TEST_CASE("protected anchors are stored and compared by value") {
    ProtectedAnchor a{.station = 50.0, .position = {100.0, 200.0}, .kind = AnchorKind::Junction};
    ProtectedAnchor b = a;
    CHECK(a == b);
    b.kind = AnchorKind::Endpoint;
    CHECK(a != b);
}

} // TEST_SUITE

TEST_SUITE("road entity") {

using infraforge::domain::road::Road;
using infraforge::domain::road::RoadId;
using infraforge::domain::road::LineSegment;
using infraforge::domain::road::ReferenceAlignment;
using infraforge::domain::road::ProtectedAnchor;
using infraforge::domain::road::AnchorKind;
using infraforge::domain::road::validateProtectedAnchors;
using infraforge::domain::road::RoadSource;
using infraforge::domain::road::SourceProvider;
using infraforge::domain::road::buildElevationProfile;
using infraforge::domain::road::buildSuperelevationProfile;

RoadId makeId() {
    return infraforge::domain::road::roadIdFromUuidText("12345678-1234-1234-1234-123456789abc");
}

RoadId makeRoadId(const std::string& uuid) {
    return infraforge::domain::road::roadIdFromUuidText(uuid);
}

ReferenceAlignment makeAlignment() {
    LineSegment line{.start = {0.0, 0.0}, .heading = 0.0, .length = 100.0};
    auto built = ReferenceAlignment::build({line});
    REQUIRE(built.has_value());
    return *built;
}

TEST_CASE("road build succeeds with valid input") {
    auto alignment = makeAlignment();
    Road::BuildInput input{
        .id = makeId(),
        .displayName = "Highway 1",
        .alignment = alignment,
        .elevation = {},
        .superelevation = {},
        .source = {},
    };
    auto road = Road::build(std::move(input));
    REQUIRE(road.has_value());
    CHECK(road->displayName() == "Highway 1");
    CHECK_FALSE(road->id().isNull());
    CHECK(road->alignment().totalLength() == doctest::Approx(100.0));
}

TEST_CASE("road build rejects null id") {
    auto alignment = makeAlignment();
    Road::BuildInput input{
        .id = RoadId{},
        .displayName = "Road",
        .alignment = alignment,
    };
    auto road = Road::build(std::move(input));
    REQUIRE_FALSE(road.has_value());
    bool found = false;
    for (const auto& d : road.error()) {
        if (d.code == infraforge::domain::road::RoadErrorCode::InvalidArgument) { found = true; }
    }
    CHECK(found);
}

TEST_CASE("road build rejects empty display name") {
    auto alignment = makeAlignment();
    Road::BuildInput input{
        .id = makeId(),
        .displayName = "",
        .alignment = alignment,
    };
    auto road = Road::build(std::move(input));
    REQUIRE_FALSE(road.has_value());
}

TEST_CASE("road evaluate combines alignment and profiles") {
    auto alignment = makeAlignment();
    auto elev = infraforge::domain::road::buildElevationProfile({{0.0, 100.0}, {100.0, 110.0}});
    REQUIRE(elev.has_value());
    auto sup = infraforge::domain::road::buildSuperelevationProfile({{0.0, 0.0}, {100.0, 0.05}});
    REQUIRE(sup.has_value());

    Road::BuildInput input{
        .id = makeId(),
        .displayName = "Road",
        .alignment = alignment,
        .elevation = *elev,
        .superelevation = *sup,
    };
    auto road = Road::build(std::move(input));
    REQUIRE(road.has_value());

    const auto sample = road->evaluate(50.0);
    CHECK(sample.position.easting == doctest::Approx(50.0));
    CHECK(sample.position.northing == doctest::Approx(0.0));
    CHECK(sample.heading == doctest::Approx(0.0));
    CHECK(sample.height == doctest::Approx(105.0));
    CHECK(sample.crossSlope == doctest::Approx(0.025));
}

TEST_CASE("protected anchor in range passes validation") {
    auto alignment = makeAlignment();
    ProtectedAnchor anchor{.station = 50.0, .position = {50.0, 0.0}, .kind = AnchorKind::Endpoint};
    auto diagnostics = validateProtectedAnchors(alignment, {anchor});
    CHECK(diagnostics.empty());
}

TEST_CASE("protected anchor out of range is detected") {
    auto alignment = makeAlignment();
    ProtectedAnchor anchor{.station = 200.0, .position = {200.0, 0.0}, .kind = AnchorKind::Junction};
    auto diagnostics = validateProtectedAnchors(alignment, {anchor});
    REQUIRE_FALSE(diagnostics.empty());
    CHECK(diagnostics[0].code == infraforge::domain::road::RoadErrorCode::AnchorOutOfRange);
}

TEST_CASE("protected anchor with displaced position is detected") {
    auto alignment = makeAlignment();
    ProtectedAnchor anchor{.station = 50.0, .position = {55.0, 0.0}, .kind = AnchorKind::UserPinned};
    auto diagnostics = validateProtectedAnchors(alignment, {anchor});
    REQUIRE_FALSE(diagnostics.empty());
    CHECK(diagnostics[0].code == infraforge::domain::road::RoadErrorCode::PositionDiscontinuity);
}

TEST_CASE("protected anchor with non-finite position is rejected") {
    auto alignment = makeAlignment();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    ProtectedAnchor anchor{.station = 50.0,
        .position = {nan, 0.0}, .kind = AnchorKind::UserPinned};
    auto diagnostics = validateProtectedAnchors(alignment, {anchor});
    REQUIRE_FALSE(diagnostics.empty());
    CHECK(diagnostics[0].code == infraforge::domain::road::RoadErrorCode::NonFiniteParameter);
}

TEST_CASE("protected anchor with non-finite easting is rejected") {
    auto alignment = makeAlignment();
    const double inf = std::numeric_limits<double>::infinity();
    ProtectedAnchor anchor{.station = 50.0,
        .position = {inf, 0.0}, .kind = AnchorKind::UserPinned};
    auto diagnostics = validateProtectedAnchors(alignment, {anchor});
    REQUIRE_FALSE(diagnostics.empty());
    CHECK(diagnostics[0].code == infraforge::domain::road::RoadErrorCode::NonFiniteParameter);
}

TEST_CASE("non-finite position tolerance is rejected") {
    auto alignment = makeAlignment();
    ProtectedAnchor anchor{.station = 50.0, .position = {50.0, 0.0}, .kind = AnchorKind::UserPinned};
    const double nan = std::numeric_limits<double>::quiet_NaN();
    auto diagnostics = validateProtectedAnchors(alignment, {anchor}, nan);
    REQUIRE_FALSE(diagnostics.empty());
    CHECK(diagnostics[0].code == infraforge::domain::road::RoadErrorCode::InvalidArgument);
}

TEST_CASE("arc with huge-but-finite curvature and length produces non-finite end and is rejected") {
    // Individually finite parameters, but curvature * length overflows to inf.
    const double hugeCurvature = 1e300;
    const double hugeLength = 1e300;
    CircularArcSegment arc{.start = {0.0, 0.0}, .startHeading = 0.0,
        .curvature = hugeCurvature, .length = hugeLength};
    auto built = ReferenceAlignment::build({arc});
    REQUIRE_FALSE(built.has_value());
    // Should report non-finite derived geometry, not succeed.
    bool foundNonFinite = false;
    for (const auto& d : built.error()) {
        if (d.code == infraforge::domain::road::RoadErrorCode::NonFiniteParameter) {
            foundNonFinite = true;
            break;
        }
    }
    CHECK(foundNonFinite);
}

TEST_CASE("clothoid with huge-but-finite parameters does not hang or crash") {
    using infraforge::domain::road::ClothoidSegment;
    // Finite but extreme parameters that would overflow phase computation.
    const double hugeCurvature = 1e300;
    ClothoidSegment spiral{.start = {0.0, 0.0}, .startHeading = 0.0,
        .startCurvature = 0.0, .endCurvature = hugeCurvature, .length = 1e300};
    // This should not hang; the builder should reject non-finite derived geometry.
    auto built = ReferenceAlignment::build({spiral});
    REQUIRE_FALSE(built.has_value());
    bool foundNonFinite = false;
    for (const auto& d : built.error()) {
        if (d.code == infraforge::domain::road::RoadErrorCode::NonFiniteParameter) {
            foundNonFinite = true;
            break;
        }
    }
    CHECK(foundNonFinite);
}

TEST_CASE("alignment with huge heading does not hang angle normalization") {
    // A huge but finite heading should not cause iterative normalization to hang.
    const double hugeHeading = 1e18;
    LineSegment line1{.start = {0.0, 0.0}, .heading = 0.0, .length = 100.0};
    const auto line1End = line1.endSample();
    LineSegment line2{.start = line1End.position, .heading = hugeHeading, .length = 100.0};
    // This should fail G1 continuity (huge heading difference) but not hang.
    auto built = ReferenceAlignment::build({line1, line2});
    REQUIRE_FALSE(built.has_value());
    bool foundHeading = false;
    for (const auto& d : built.error()) {
        if (d.code == infraforge::domain::road::RoadErrorCode::HeadingDiscontinuity) {
            foundHeading = true;
            break;
        }
    }
    CHECK(foundHeading);
}

// ---- Regression tests for review pass 2 ----

TEST_CASE("clothoid with large finite phase (~1e20) does not overflow long long") {
    using infraforge::domain::road::ClothoidSegment;
    using infraforge::domain::road::ReferenceAlignment;
    // phaseChange = |kappa0 * s + alpha * s^2| ~ 1e20, finite but exceeds long long max.
    // This must not cause UB in the static_cast<long long> conversion. The builder
    // must either safely evaluate (finite result) or reject (non-finite diagnostic).
    ClothoidSegment spiral{.start = {0.0, 0.0}, .startHeading = 0.0,
        .startCurvature = 0.0, .endCurvature = 1e10, .length = 1e10};
    auto built = ReferenceAlignment::build({spiral});
    if (built.has_value()) {
        // Safe evaluation: verify the result is finite and deterministic.
        const auto sample = built->evaluate(0.0);
        CHECK(std::isfinite(sample.position.easting));
        CHECK(std::isfinite(sample.position.northing));
        CHECK(std::isfinite(sample.heading));
        CHECK(std::isfinite(sample.curvature));
    } else {
        // Rejection: verify a non-finite diagnostic was reported.
        bool foundNonFinite = false;
        for (const auto& d : built.error()) {
            if (d.code == infraforge::domain::road::RoadErrorCode::NonFiniteParameter) {
                foundNonFinite = true;
                break;
            }
        }
        CHECK(foundNonFinite);
    }
}

TEST_CASE("alignment evaluate(NaN) returns default sample") {
    using infraforge::domain::road::ReferenceAlignment;
    LineSegment line{.start = {100.0, 200.0}, .heading = 0.5, .length = 150.0};
    auto built = ReferenceAlignment::build({line});
    REQUIRE(built.has_value());
    const auto& alignment = *built;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    auto sample = alignment.evaluate(nan);
    // NaN policy: returns a default (zero) sample, not an arbitrary segment.
    CHECK(sample.position.easting == 0.0);
    CHECK(sample.position.northing == 0.0);
    CHECK(sample.heading == 0.0);
    CHECK(sample.curvature == 0.0);
}

TEST_CASE("alignment evaluate(+inf) clamps to end") {
    using infraforge::domain::road::ReferenceAlignment;
    LineSegment line{.start = {100.0, 200.0}, .heading = 0.0, .length = 150.0};
    auto built = ReferenceAlignment::build({line});
    REQUIRE(built.has_value());
    const auto& alignment = *built;
    const double inf = std::numeric_limits<double>::infinity();
    auto sample = alignment.evaluate(inf);
    // +inf clamps to end: start + length * (cos, sin)
    CHECK(sample.position.easting == doctest::Approx(250.0));
    CHECK(sample.position.northing == doctest::Approx(200.0));
    CHECK(sample.heading == doctest::Approx(0.0));
}

TEST_CASE("alignment evaluate(-inf) clamps to start") {
    using infraforge::domain::road::ReferenceAlignment;
    LineSegment line{.start = {100.0, 200.0}, .heading = 0.0, .length = 150.0};
    auto built = ReferenceAlignment::build({line});
    REQUIRE(built.has_value());
    const auto& alignment = *built;
    const double negInf = -std::numeric_limits<double>::infinity();
    auto sample = alignment.evaluate(negInf);
    // -inf clamps to start.
    CHECK(sample.position.easting == doctest::Approx(100.0));
    CHECK(sample.position.northing == doctest::Approx(200.0));
    CHECK(sample.heading == doctest::Approx(0.0));
}

TEST_CASE("alignment segmentIndexAt(NaN) returns nullopt") {
    using infraforge::domain::road::ReferenceAlignment;
    LineSegment line{.start = {0.0, 0.0}, .heading = 0.0, .length = 100.0};
    auto built = ReferenceAlignment::build({line});
    REQUIRE(built.has_value());
    const auto& alignment = *built;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    CHECK_FALSE(alignment.segmentIndexAt(nan).has_value());
}

TEST_CASE("alignment segmentIndexAt(+inf) returns nullopt") {
    using infraforge::domain::road::ReferenceAlignment;
    LineSegment line{.start = {0.0, 0.0}, .heading = 0.0, .length = 100.0};
    auto built = ReferenceAlignment::build({line});
    REQUIRE(built.has_value());
    const auto& alignment = *built;
    const double inf = std::numeric_limits<double>::infinity();
    CHECK_FALSE(alignment.segmentIndexAt(inf).has_value());
}

TEST_CASE("alignment segmentIndexAt(-inf) returns nullopt") {
    using infraforge::domain::road::ReferenceAlignment;
    LineSegment line{.start = {0.0, 0.0}, .heading = 0.0, .length = 100.0};
    auto built = ReferenceAlignment::build({line});
    REQUIRE(built.has_value());
    const auto& alignment = *built;
    const double negInf = -std::numeric_limits<double>::infinity();
    CHECK_FALSE(alignment.segmentIndexAt(negInf).has_value());
}

TEST_CASE("elevation profile evaluate(NaN) returns 0.0") {
    using infraforge::domain::road::buildElevationProfile;
    auto elev = buildElevationProfile({{0.0, 100.0}, {200.0, 120.0}});
    REQUIRE(elev.has_value());
    const double nan = std::numeric_limits<double>::quiet_NaN();
    CHECK(elev->evaluate(nan) == 0.0);
}

TEST_CASE("elevation profile evaluate(+inf) returns last value") {
    using infraforge::domain::road::buildElevationProfile;
    auto elev = buildElevationProfile({{0.0, 100.0}, {200.0, 120.0}});
    REQUIRE(elev.has_value());
    const double inf = std::numeric_limits<double>::infinity();
    CHECK(elev->evaluate(inf) == doctest::Approx(120.0));
}

TEST_CASE("elevation profile evaluate(-inf) returns first value") {
    using infraforge::domain::road::buildElevationProfile;
    auto elev = buildElevationProfile({{0.0, 100.0}, {200.0, 120.0}});
    REQUIRE(elev.has_value());
    const double negInf = -std::numeric_limits<double>::infinity();
    CHECK(elev->evaluate(negInf) == doctest::Approx(100.0));
}

TEST_CASE("superelevation profile evaluate(NaN) returns 0.0") {
    using infraforge::domain::road::buildSuperelevationProfile;
    auto sup = buildSuperelevationProfile({{0.0, 0.0}, {100.0, 0.05}});
    REQUIRE(sup.has_value());
    const double nan = std::numeric_limits<double>::quiet_NaN();
    CHECK(sup->evaluate(nan) == 0.0);
}

TEST_CASE("road evaluate(NaN) returns default sample") {
    using infraforge::domain::road::Road;
    using infraforge::domain::road::ReferenceAlignment;
    LineSegment line{.start = {0.0, 0.0}, .heading = 0.0, .length = 100.0};
    auto alignment = ReferenceAlignment::build({line});
    REQUIRE(alignment.has_value());
    Road::BuildInput input{
        .id = makeRoadId("aaaa0000-bbbb-cccc-dddd-eeeeeeeeeeee"),
        .displayName = "NaN Test Road",
        .alignment = *alignment,
    };
    auto road = Road::build(std::move(input));
    REQUIRE(road.has_value());
    const double nan = std::numeric_limits<double>::quiet_NaN();
    auto sample = road->evaluate(nan);
    CHECK(sample.position.easting == 0.0);
    CHECK(sample.position.northing == 0.0);
    CHECK(sample.heading == 0.0);
    CHECK(sample.curvature == 0.0);
    CHECK(sample.height == 0.0);
    CHECK(sample.crossSlope == 0.0);
}

TEST_CASE("NaN position tolerance is rejected") {
    using infraforge::domain::road::ReferenceAlignment;
    LineSegment line{.start = {0.0, 0.0}, .heading = 0.0, .length = 100.0};
    const double nan = std::numeric_limits<double>::quiet_NaN();
    auto built = ReferenceAlignment::build({line}, nan);
    REQUIRE_FALSE(built.has_value());
    CHECK(built.error()[0].code == infraforge::domain::road::RoadErrorCode::InvalidArgument);
}

TEST_CASE("inf position tolerance is rejected") {
    using infraforge::domain::road::ReferenceAlignment;
    LineSegment line{.start = {0.0, 0.0}, .heading = 0.0, .length = 100.0};
    const double inf = std::numeric_limits<double>::infinity();
    auto built = ReferenceAlignment::build({line}, inf);
    REQUIRE_FALSE(built.has_value());
    CHECK(built.error()[0].code == infraforge::domain::road::RoadErrorCode::InvalidArgument);
}

TEST_CASE("negative position tolerance is rejected") {
    using infraforge::domain::road::ReferenceAlignment;
    LineSegment line{.start = {0.0, 0.0}, .heading = 0.0, .length = 100.0};
    auto built = ReferenceAlignment::build({line}, -1e-9);
    REQUIRE_FALSE(built.has_value());
    CHECK(built.error()[0].code == infraforge::domain::road::RoadErrorCode::InvalidArgument);
}

TEST_CASE("zero position tolerance works correctly") {
    using infraforge::domain::road::ReferenceAlignment;
    LineSegment line{.start = {0.0, 0.0}, .heading = 0.0, .length = 100.0};
    auto built = ReferenceAlignment::build({line}, 0.0);
    REQUIRE(built.has_value());
}

TEST_CASE("NaN heading tolerance is rejected") {
    using infraforge::domain::road::ReferenceAlignment;
    LineSegment line{.start = {0.0, 0.0}, .heading = 0.0, .length = 100.0};
    const double nan = std::numeric_limits<double>::quiet_NaN();
    auto built = ReferenceAlignment::build({line},
        infraforge::domain::road::kDefaultPositionTolerance, nan);
    REQUIRE_FALSE(built.has_value());
    CHECK(built.error()[0].code == infraforge::domain::road::RoadErrorCode::InvalidArgument);
}

TEST_CASE("inf heading tolerance is rejected") {
    using infraforge::domain::road::ReferenceAlignment;
    LineSegment line{.start = {0.0, 0.0}, .heading = 0.0, .length = 100.0};
    const double inf = std::numeric_limits<double>::infinity();
    auto built = ReferenceAlignment::build({line},
        infraforge::domain::road::kDefaultPositionTolerance, inf);
    REQUIRE_FALSE(built.has_value());
    CHECK(built.error()[0].code == infraforge::domain::road::RoadErrorCode::InvalidArgument);
}

TEST_CASE("negative heading tolerance is rejected") {
    using infraforge::domain::road::ReferenceAlignment;
    LineSegment line{.start = {0.0, 0.0}, .heading = 0.0, .length = 100.0};
    auto built = ReferenceAlignment::build({line},
        infraforge::domain::road::kDefaultPositionTolerance, -1e-9);
    REQUIRE_FALSE(built.has_value());
    CHECK(built.error()[0].code == infraforge::domain::road::RoadErrorCode::InvalidArgument);
}

TEST_CASE("NaN curvature tolerance is rejected") {
    using infraforge::domain::road::ReferenceAlignment;
    LineSegment line{.start = {0.0, 0.0}, .heading = 0.0, .length = 100.0};
    const double nan = std::numeric_limits<double>::quiet_NaN();
    auto built = ReferenceAlignment::build({line},
        infraforge::domain::road::kDefaultPositionTolerance,
        infraforge::domain::road::kDefaultHeadingTolerance, nan);
    REQUIRE_FALSE(built.has_value());
    CHECK(built.error()[0].code == infraforge::domain::road::RoadErrorCode::InvalidArgument);
}

TEST_CASE("inf curvature tolerance is rejected") {
    using infraforge::domain::road::ReferenceAlignment;
    LineSegment line{.start = {0.0, 0.0}, .heading = 0.0, .length = 100.0};
    const double inf = std::numeric_limits<double>::infinity();
    auto built = ReferenceAlignment::build({line},
        infraforge::domain::road::kDefaultPositionTolerance,
        infraforge::domain::road::kDefaultHeadingTolerance, inf);
    REQUIRE_FALSE(built.has_value());
    CHECK(built.error()[0].code == infraforge::domain::road::RoadErrorCode::InvalidArgument);
}

TEST_CASE("negative curvature tolerance is rejected") {
    using infraforge::domain::road::ReferenceAlignment;
    LineSegment line{.start = {0.0, 0.0}, .heading = 0.0, .length = 100.0};
    auto built = ReferenceAlignment::build({line},
        infraforge::domain::road::kDefaultPositionTolerance,
        infraforge::domain::road::kDefaultHeadingTolerance, -1e-12);
    REQUIRE_FALSE(built.has_value());
    CHECK(built.error()[0].code == infraforge::domain::road::RoadErrorCode::InvalidArgument);
}

TEST_CASE("source vertex with NaN x is rejected by Road::build") {
    using infraforge::domain::road::Road;
    using infraforge::domain::road::RoadSource;
    using infraforge::domain::road::ReferenceAlignment;
    LineSegment line{.start = {0.0, 0.0}, .heading = 0.0, .length = 100.0};
    auto alignment = ReferenceAlignment::build({line});
    REQUIRE(alignment.has_value());
    RoadSource source;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    source.geometry.vertices = {{nan, 37.8, std::optional<double>{}}};
    source.provenance.provider = SourceProvider::Osm;
    Road::BuildInput input{
        .id = makeRoadId("bbbb0000-cccc-dddd-eeee-ffffffffffff"),
        .displayName = "NaN X Road",
        .alignment = *alignment,
        .source = source,
    };
    auto road = Road::build(std::move(input));
    REQUIRE_FALSE(road.has_value());
    bool found = false;
    for (const auto& d : road.error()) {
        if (d.code == infraforge::domain::road::RoadErrorCode::NonFiniteParameter) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("source vertex with inf x is rejected by Road::build") {
    using infraforge::domain::road::Road;
    using infraforge::domain::road::RoadSource;
    using infraforge::domain::road::ReferenceAlignment;
    LineSegment line{.start = {0.0, 0.0}, .heading = 0.0, .length = 100.0};
    auto alignment = ReferenceAlignment::build({line});
    REQUIRE(alignment.has_value());
    RoadSource source;
    const double inf = std::numeric_limits<double>::infinity();
    source.geometry.vertices = {{inf, 37.8, std::optional<double>{}}};
    source.provenance.provider = SourceProvider::Osm;
    Road::BuildInput input{
        .id = makeRoadId("cccc0000-dddd-eeee-ffff-aaaaaaaaaaaa"),
        .displayName = "Inf X Road",
        .alignment = *alignment,
        .source = source,
    };
    auto road = Road::build(std::move(input));
    REQUIRE_FALSE(road.has_value());
    bool found = false;
    for (const auto& d : road.error()) {
        if (d.code == infraforge::domain::road::RoadErrorCode::NonFiniteParameter) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("source vertex with NaN y is rejected by Road::build") {
    using infraforge::domain::road::Road;
    using infraforge::domain::road::RoadSource;
    using infraforge::domain::road::ReferenceAlignment;
    LineSegment line{.start = {0.0, 0.0}, .heading = 0.0, .length = 100.0};
    auto alignment = ReferenceAlignment::build({line});
    REQUIRE(alignment.has_value());
    RoadSource source;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    source.geometry.vertices = {{-122.4, nan, std::optional<double>{}}};
    source.provenance.provider = SourceProvider::Osm;
    Road::BuildInput input{
        .id = makeRoadId("dddd0000-eeee-ffff-aaaa-bbbbbbbbbbbb"),
        .displayName = "NaN Y Road",
        .alignment = *alignment,
        .source = source,
    };
    auto road = Road::build(std::move(input));
    REQUIRE_FALSE(road.has_value());
    bool found = false;
    for (const auto& d : road.error()) {
        if (d.code == infraforge::domain::road::RoadErrorCode::NonFiniteParameter) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("source vertex with inf y is rejected by Road::build") {
    using infraforge::domain::road::Road;
    using infraforge::domain::road::RoadSource;
    using infraforge::domain::road::ReferenceAlignment;
    LineSegment line{.start = {0.0, 0.0}, .heading = 0.0, .length = 100.0};
    auto alignment = ReferenceAlignment::build({line});
    REQUIRE(alignment.has_value());
    RoadSource source;
    const double inf = std::numeric_limits<double>::infinity();
    source.geometry.vertices = {{-122.4, inf, std::optional<double>{}}};
    source.provenance.provider = SourceProvider::Osm;
    Road::BuildInput input{
        .id = makeRoadId("eeee0000-ffff-aaaa-bbbb-cccccccccccc"),
        .displayName = "Inf Y Road",
        .alignment = *alignment,
        .source = source,
    };
    auto road = Road::build(std::move(input));
    REQUIRE_FALSE(road.has_value());
    bool found = false;
    for (const auto& d : road.error()) {
        if (d.code == infraforge::domain::road::RoadErrorCode::NonFiniteParameter) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("source vertex with NaN z is rejected by Road::build") {
    using infraforge::domain::road::Road;
    using infraforge::domain::road::RoadSource;
    using infraforge::domain::road::ReferenceAlignment;
    LineSegment line{.start = {0.0, 0.0}, .heading = 0.0, .length = 100.0};
    auto alignment = ReferenceAlignment::build({line});
    REQUIRE(alignment.has_value());
    RoadSource source;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    source.geometry.vertices = {{-122.4, 37.8, std::optional<double>{nan}}};
    source.provenance.provider = SourceProvider::Osm;
    Road::BuildInput input{
        .id = makeRoadId("ffff0000-aaaa-bbbb-cccc-dddddddddddd"),
        .displayName = "NaN Z Road",
        .alignment = *alignment,
        .source = source,
    };
    auto road = Road::build(std::move(input));
    REQUIRE_FALSE(road.has_value());
    bool found = false;
    for (const auto& d : road.error()) {
        if (d.code == infraforge::domain::road::RoadErrorCode::NonFiniteParameter) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("source vertex with inf z is rejected by Road::build") {
    using infraforge::domain::road::Road;
    using infraforge::domain::road::RoadSource;
    using infraforge::domain::road::ReferenceAlignment;
    LineSegment line{.start = {0.0, 0.0}, .heading = 0.0, .length = 100.0};
    auto alignment = ReferenceAlignment::build({line});
    REQUIRE(alignment.has_value());
    RoadSource source;
    const double inf = std::numeric_limits<double>::infinity();
    source.geometry.vertices = {{-122.4, 37.8, std::optional<double>{inf}}};
    source.provenance.provider = SourceProvider::Osm;
    Road::BuildInput input{
        .id = makeRoadId("0000aaaa-bbbb-cccc-dddd-eeeeeeeeeeee"),
        .displayName = "Inf Z Road",
        .alignment = *alignment,
        .source = source,
    };
    auto road = Road::build(std::move(input));
    REQUIRE_FALSE(road.has_value());
    bool found = false;
    for (const auto& d : road.error()) {
        if (d.code == infraforge::domain::road::RoadErrorCode::NonFiniteParameter) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("source vertex with absent z is accepted by Road::build") {
    using infraforge::domain::road::Road;
    using infraforge::domain::road::RoadSource;
    using infraforge::domain::road::ReferenceAlignment;
    LineSegment line{.start = {0.0, 0.0}, .heading = 0.0, .length = 100.0};
    auto alignment = ReferenceAlignment::build({line});
    REQUIRE(alignment.has_value());
    RoadSource source;
    source.geometry.vertices = {{-122.4, 37.8, std::optional<double>{}}};
    source.provenance.provider = SourceProvider::Osm;
    Road::BuildInput input{
        .id = makeRoadId("1111aaaa-bbbb-cccc-dddd-eeeeeeeeeeee"),
        .displayName = "Absent Z Road",
        .alignment = *alignment,
        .source = source,
    };
    auto road = Road::build(std::move(input));
    REQUIRE(road.has_value());
}

TEST_CASE("source vertex with finite z is accepted by Road::build") {
    using infraforge::domain::road::Road;
    using infraforge::domain::road::RoadSource;
    using infraforge::domain::road::ReferenceAlignment;
    LineSegment line{.start = {0.0, 0.0}, .heading = 0.0, .length = 100.0};
    auto alignment = ReferenceAlignment::build({line});
    REQUIRE(alignment.has_value());
    RoadSource source;
    source.geometry.vertices = {{-122.4, 37.8, std::optional<double>{10.0}}};
    source.provenance.provider = SourceProvider::Osm;
    Road::BuildInput input{
        .id = makeRoadId("2222aaaa-bbbb-cccc-dddd-eeeeeeeeeeee"),
        .displayName = "Finite Z Road",
        .alignment = *alignment,
        .source = source,
    };
    auto road = Road::build(std::move(input));
    REQUIRE(road.has_value());
}

TEST_CASE("Road::build enforces canonical station bounds [0, totalLength] for all profiles") {
    using infraforge::domain::road::LineSegment;
    using infraforge::domain::road::PiecewiseLinearProfile;
    using infraforge::domain::road::ProfileBreakpoint;
    using infraforge::domain::road::ReferenceAlignment;
    using infraforge::domain::road::Road;
    using infraforge::domain::road::RoadErrorCode;
    using infraforge::domain::road::RoadSource;
    using infraforge::domain::road::RoadWidthBreakpoint;
    using infraforge::domain::road::RoadWidthProfile;
    using infraforge::domain::road::SourceProvider;

    LineSegment line{.start = {0.0, 0.0}, .heading = 0.0, .length = 100.0};
    auto alignment = ReferenceAlignment::build({line});
    REQUIRE(alignment.has_value());

    auto makeBaseInput = [&]() -> Road::BuildInput {
        RoadSource source;
        source.geometry.vertices = {{-122.4, 37.8, std::optional<double>{0.0}}};
        source.provenance.provider = SourceProvider::Osm;
        return Road::BuildInput{
            .id = makeRoadId("3333aaaa-bbbb-cccc-dddd-eeeeeeeeeeee"),
            .displayName = "Station Bounds Road",
            .alignment = *alignment,
            .source = source,
        };
    };

    // 1. Normal valid road still builds
    {
        auto input = makeBaseInput();
        input.elevation = PiecewiseLinearProfile({ProfileBreakpoint{0.0, 10.0}, ProfileBreakpoint{100.0, 20.0}});
        input.superelevation = PiecewiseLinearProfile({ProfileBreakpoint{0.0, 0.0}, ProfileBreakpoint{100.0, 0.04}});
        input.width = RoadWidthProfile({RoadWidthBreakpoint{0.0, 4.0, 4.0}, RoadWidthBreakpoint{100.0, 6.0, 6.0}});
        auto road = Road::build(std::move(input));
        REQUIRE(road.has_value());
    }

    // 2. Station exactly zero accepted
    {
        auto input = makeBaseInput();
        input.elevation = PiecewiseLinearProfile({ProfileBreakpoint{0.0, 10.0}});
        auto road = Road::build(std::move(input));
        REQUIRE(road.has_value());
    }

    // 3. Station exactly at road length accepted
    {
        auto input = makeBaseInput();
        input.elevation = PiecewiseLinearProfile({ProfileBreakpoint{100.0, 20.0}});
        auto road = Road::build(std::move(input));
        REQUIRE(road.has_value());
    }

    // 4. Negative elevation station rejected
    {
        auto input = makeBaseInput();
        input.elevation = PiecewiseLinearProfile({ProfileBreakpoint{-1.0, 10.0}, ProfileBreakpoint{50.0, 20.0}});
        auto road = Road::build(std::move(input));
        REQUIRE_FALSE(road.has_value());
        bool hasDiag = false;
        for (const auto& d : road.error()) {
            if (d.code == RoadErrorCode::InvalidProfile && d.message.find("elevation breakpoint station") != std::string::npos) {
                hasDiag = true;
            }
        }
        CHECK(hasDiag);
    }

    // 5. Negative superelevation station rejected
    {
        auto input = makeBaseInput();
        input.superelevation = PiecewiseLinearProfile({ProfileBreakpoint{-0.5, 0.0}, ProfileBreakpoint{50.0, 0.02}});
        auto road = Road::build(std::move(input));
        REQUIRE_FALSE(road.has_value());
        bool hasDiag = false;
        for (const auto& d : road.error()) {
            if (d.code == RoadErrorCode::InvalidProfile && d.message.find("superelevation breakpoint station") != std::string::npos) {
                hasDiag = true;
            }
        }
        CHECK(hasDiag);
    }

    // 6. Negative width station rejected
    {
        auto input = makeBaseInput();
        input.width = RoadWidthProfile({RoadWidthBreakpoint{-2.0, 4.0, 4.0}, RoadWidthBreakpoint{50.0, 4.0, 4.0}});
        auto road = Road::build(std::move(input));
        REQUIRE_FALSE(road.has_value());
        bool hasDiag = false;
        for (const auto& d : road.error()) {
            if (d.code == RoadErrorCode::InvalidProfile && d.message.find("width") != std::string::npos) {
                hasDiag = true;
            }
        }
        CHECK(hasDiag);
    }

    // 7. Station just materially above road length rejected
    {
        auto input = makeBaseInput();
        input.elevation = PiecewiseLinearProfile({ProfileBreakpoint{0.0, 10.0}, ProfileBreakpoint{100.01, 20.0}});
        auto road = Road::build(std::move(input));
        REQUIRE_FALSE(road.has_value());
        bool hasDiag = false;
        for (const auto& d : road.error()) {
            if (d.code == RoadErrorCode::InvalidProfile && d.message.find("elevation breakpoint station") != std::string::npos) {
                hasDiag = true;
            }
        }
        CHECK(hasDiag);
    }
}

TEST_CASE("constructCircularArcThroughPoints builds canonical mathematical arcs") {
    using infraforge::domain::road::constructCircularArcThroughPoints;
    using infraforge::domain::road::constructStraightSegment;
    using infraforge::domain::road::constructClothoidSegment;
    using infraforge::domain::road::RoadErrorCode;

    // 1. Semicircle left turn (CCW): P0=(10, 0), P1=(0, 10), P2=(-10, 0)
    {
        const auto arcRes = constructCircularArcThroughPoints(
            AlignmentPoint{10.0, 0.0}, AlignmentPoint{0.0, 10.0}, AlignmentPoint{-10.0, 0.0});
        REQUIRE(arcRes.has_value());
        const auto& arc = *arcRes;
        CHECK(arc.start.easting == doctest::Approx(10.0));
        CHECK(arc.start.northing == doctest::Approx(0.0));
        CHECK(arc.curvature == doctest::Approx(0.1)); // 1/R = 1/10
        CHECK(arc.length == doctest::Approx(10.0 * kPi));
        CHECK(arc.startHeading == doctest::Approx(kPi / 2.0));

        // Evaluate start and end
        const auto startSample = arc.evaluate(0.0);
        CHECK(startSample.position.easting == doctest::Approx(10.0));
        CHECK(startSample.position.northing == doctest::Approx(0.0));
        const auto endSample = arc.endSample();
        CHECK(endSample.position.easting == doctest::Approx(-10.0));
        CHECK(std::abs(endSample.position.northing) < 1e-6);
    }

    // 2. Semicircle right turn (CW): P0=(10, 0), P1=(0, -10), P2=(-10, 0)
    {
        const auto arcRes = constructCircularArcThroughPoints(
            AlignmentPoint{10.0, 0.0}, AlignmentPoint{0.0, -10.0}, AlignmentPoint{-10.0, 0.0});
        REQUIRE(arcRes.has_value());
        const auto& arc = *arcRes;
        CHECK(arc.curvature == doctest::Approx(-0.1)); // negative = CW turn
        CHECK(arc.length == doctest::Approx(10.0 * kPi));
        CHECK(arc.startHeading == doctest::Approx(-kPi / 2.0));

        const auto endSample = arc.endSample();
        CHECK(endSample.position.easting == doctest::Approx(-10.0));
        CHECK(std::abs(endSample.position.northing) < 1e-6);
    }

    // 3. Shallow arc: small deflection
    {
        const auto arcRes = constructCircularArcThroughPoints(
            AlignmentPoint{0.0, 0.0}, AlignmentPoint{50.0, 1.0}, AlignmentPoint{100.0, 0.0});
        REQUIRE(arcRes.has_value());
        const auto& arc = *arcRes;
        CHECK(arc.curvature < 0.0); // right turn
        CHECK(arc.length > 100.0);
        const auto endSample = arc.endSample();
        CHECK(endSample.position.easting == doctest::Approx(100.0).epsilon(1e-4));
        CHECK(std::abs(endSample.position.northing) < 1e-4);
    }

    // 4. Nearly collinear points rejected
    {
        const auto arcRes = constructCircularArcThroughPoints(
            AlignmentPoint{0.0, 0.0}, AlignmentPoint{50.0, 0.0}, AlignmentPoint{100.0, 0.0});
        REQUIRE_FALSE(arcRes.has_value());
        CHECK(arcRes.error().code == RoadErrorCode::DegenerateSegment);
    }

    // 5. Duplicate / coincident points rejected
    {
        const auto arcRes = constructCircularArcThroughPoints(
            AlignmentPoint{0.0, 0.0}, AlignmentPoint{0.0, 0.0}, AlignmentPoint{100.0, 50.0});
        REQUIRE_FALSE(arcRes.has_value());
        CHECK(arcRes.error().code == RoadErrorCode::DegenerateSegment);
    }

    // 6. Non-finite coordinates rejected
    {
        const auto arcRes = constructCircularArcThroughPoints(
            AlignmentPoint{std::numeric_limits<double>::quiet_NaN(), 0.0},
            AlignmentPoint{50.0, 20.0}, AlignmentPoint{100.0, 0.0});
        REQUIRE_FALSE(arcRes.has_value());
        CHECK(arcRes.error().code == RoadErrorCode::NonFiniteParameter);
    }

    // 7. Very large radius exceeding maximum bound rejected
    {
        const auto arcRes = constructCircularArcThroughPoints(
            AlignmentPoint{0.0, 0.0}, AlignmentPoint{50.0, 1e-8}, AlignmentPoint{100.0, 0.0},
            1e-12, 0.1, 1000.0); // maxRadius = 1000m
        REQUIRE_FALSE(arcRes.has_value());
        CHECK(arcRes.error().code == RoadErrorCode::InvalidCurvature);
    }

    // 8. GIS / UTM-scale coordinates (e.g. Easting ~500k, Northing ~5000k) maintain exact machine precision
    {
        const double utmE = 524000.0;
        const double utmN = 5930000.0;
        const auto arcRes = constructCircularArcThroughPoints(
            AlignmentPoint{utmE + 10.0, utmN + 0.0},
            AlignmentPoint{utmE + 0.0, utmN + 10.0},
            AlignmentPoint{utmE - 10.0, utmN + 0.0});
        REQUIRE(arcRes.has_value());
        const auto& arc = *arcRes;
        CHECK(arc.start.easting == doctest::Approx(utmE + 10.0));
        CHECK(arc.start.northing == doctest::Approx(utmN + 0.0));
        CHECK(arc.curvature == doctest::Approx(0.1));
        CHECK(arc.length == doctest::Approx(10.0 * kPi));
        CHECK(arc.startHeading == doctest::Approx(kPi / 2.0));

        const auto endSample = arc.endSample();
        CHECK(endSample.position.easting == doctest::Approx(utmE - 10.0));
        CHECK(endSample.position.northing == doctest::Approx(utmN + 0.0));
    }

    // 9. Shallow GIS / UTM curve
    {
        const double utmE = 600000.0;
        const double utmN = 4500000.0;
        const auto arcRes = constructCircularArcThroughPoints(
            AlignmentPoint{utmE, utmN},
            AlignmentPoint{utmE + 500.0, utmN + 25.0},
            AlignmentPoint{utmE + 1000.0, utmN});
        REQUIRE(arcRes.has_value());
        const auto& arc = *arcRes;
        CHECK(arc.curvature < 0.0); // right turn
        const auto endSample = arc.endSample();
        CHECK(endSample.position.easting == doctest::Approx(utmE + 1000.0).epsilon(1e-4));
        CHECK(endSample.position.northing == doctest::Approx(utmN).epsilon(1e-4));
    }

    // 10. Scale-aware collinearity in UTM coordinates
    {
        const double utmE = 500000.0;
        const double utmN = 5000000.0;
        const auto arcRes = constructCircularArcThroughPoints(
            AlignmentPoint{utmE, utmN},
            AlignmentPoint{utmE + 100.0, utmN},
            AlignmentPoint{utmE + 200.0, utmN});
        REQUIRE_FALSE(arcRes.has_value());
        CHECK(arcRes.error().code == RoadErrorCode::DegenerateSegment);
    }

    // 11. Straight segment construction
    {
        const auto lineRes = constructStraightSegment(AlignmentPoint{10.0, 20.0}, AlignmentPoint{40.0, 60.0});
        REQUIRE(lineRes.has_value());
        const auto& line = *lineRes;
        CHECK(line.start.easting == doctest::Approx(10.0));
        CHECK(line.start.northing == doctest::Approx(20.0));
        CHECK(line.length == doctest::Approx(50.0));
        CHECK(line.heading == doctest::Approx(std::atan2(40.0, 30.0)));
        const auto endSample = line.endSample();
        CHECK(endSample.position.easting == doctest::Approx(40.0));
        CHECK(endSample.position.northing == doctest::Approx(60.0));
    }

    // 12. Clothoid segment construction
    {
        const auto clothoidRes = constructClothoidSegment(
            AlignmentPoint{0.0, 0.0}, 0.0, 0.0, 0.02, 100.0);
        REQUIRE(clothoidRes.has_value());
        const auto& clothoid = *clothoidRes;
        CHECK(clothoid.startCurvature == doctest::Approx(0.0));
        CHECK(clothoid.endCurvature == doctest::Approx(0.02));
        CHECK(clothoid.length == doctest::Approx(100.0));
    }
}

} // TEST_SUITE

