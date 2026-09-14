#pragma once

#include "infraforge/domain/road/RoadTypes.hpp"

#include <cstdint>
#include <optional>
#include <variant>

namespace infraforge::domain::road {

// Kind tag for alignment segment primitives. The canonical road alignment is
// composed exclusively of these mathematical primitives
// (docs/05_DOMAINS/LINEAR_INFRASTRUCTURE_GEOMETRY.md): a line, a circular
// arc, or a clothoid/spiral transition. Polylines and tessellated vertices
// are never canonical alignment truth.
enum class AlignmentSegmentKind : std::uint8_t {
    Line,
    CircularArc,
    Clothoid,
};

[[nodiscard]] std::string_view alignmentSegmentKindName(AlignmentSegmentKind kind) noexcept;
[[nodiscard]] std::optional<AlignmentSegmentKind> alignmentSegmentKindFromName(
    std::string_view name) noexcept;

// Mathematically exact straight segment. Canonical parameters: start point,
// constant heading, and length. Curvature is identically zero.
//
// Evaluation (sLocal in [0, length]):
//   position = start + sLocal * (cos(heading), sin(heading))
//   heading  = heading
//   curvature = 0
struct LineSegment {
    AlignmentPoint start{};
    Heading heading{0.0};
    double length{0.0};

    [[nodiscard]] AlignmentSample evaluate(double sLocal) const noexcept;
    [[nodiscard]] AlignmentSample endSample() const noexcept { return evaluate(length); }
};

// Deterministic circular arc segment. Canonical parameters: start point,
// start heading, signed curvature (!= 0), and arc length. Signed curvature
// gives orientation: positive turns left (CCW), negative turns right (CW).
// The arc is never approximated by a polyline; evaluation is the exact
// closed form.
//
// Evaluation (sLocal in [0, length], kappa = curvature):
//   theta(s) = startHeading + kappa * sLocal
//   easting(s)  = start.easting  + (1/kappa) * (sin(theta(s)) - sin(startHeading))
//   northing(s) = start.northing + (1/kappa) * (cos(startHeading) - cos(theta(s)))
//   heading(s)  = theta(s)
//   curvature(s) = kappa
struct CircularArcSegment {
    AlignmentPoint start{};
    Heading startHeading{0.0};
    Curvature curvature{0.0};
    double length{0.0};

    [[nodiscard]] AlignmentSample evaluate(double sLocal) const noexcept;
    [[nodiscard]] AlignmentSample endSample() const noexcept { return evaluate(length); }
};

// Variant over the canonical alignment segment primitives. Value semantics:
// segments are self-contained (each stores its own start point/heading) so
// they persist and validate independently. Clothoid is added in a later
// commit; the variant grows to include it then.
using AlignmentSegment = std::variant<LineSegment, CircularArcSegment>;

// Generic evaluation over the variant.
[[nodiscard]] AlignmentSample evaluateSegment(const AlignmentSegment& segment, double sLocal) noexcept;
[[nodiscard]] double segmentLength(const AlignmentSegment& segment) noexcept;
[[nodiscard]] AlignmentSegmentKind segmentKind(const AlignmentSegment& segment) noexcept;
[[nodiscard]] AlignmentPoint segmentStartPoint(const AlignmentSegment& segment) noexcept;
[[nodiscard]] Heading segmentStartHeading(const AlignmentSegment& segment) noexcept;
[[nodiscard]] Curvature segmentStartCurvature(const AlignmentSegment& segment) noexcept;
[[nodiscard]] AlignmentSample segmentEndSample(const AlignmentSegment& segment) noexcept;

// Structural validation of one segment's canonical parameters. Returns
// nullopt when the segment is well-formed; otherwise a typed diagnostic
// describing the first failure. Does not silently repair invalid geometry.
[[nodiscard]] std::optional<RoadDiagnostic> validateSegment(const AlignmentSegment& segment) noexcept;

} // namespace infraforge::domain::road
