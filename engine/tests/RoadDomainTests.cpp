#include <doctest/doctest.h>

#include "infraforge/domain/road/AlignmentPrimitives.hpp"
#include "infraforge/domain/road/RoadTypes.hpp"

#include <cmath>
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
