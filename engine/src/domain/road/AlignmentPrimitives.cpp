#include "infraforge/domain/road/AlignmentPrimitives.hpp"

#include <cmath>

namespace infraforge::domain::road {
namespace {

std::string_view kindName(AlignmentSegmentKind kind) noexcept {
    switch (kind) {
    case AlignmentSegmentKind::Line:
        return "line";
    case AlignmentSegmentKind::CircularArc:
        return "circular_arc";
    case AlignmentSegmentKind::Clothoid:
        return "clothoid";
    }
    return "unknown";
}

bool isFinitePoint(const AlignmentPoint& p) noexcept {
    return std::isfinite(p.easting) && std::isfinite(p.northing);
}

std::optional<RoadDiagnostic> validateCommon(
    const AlignmentPoint& start, const Heading startHeading, const double length) noexcept {
    if (!isFinitePoint(start)) {
        return RoadDiagnostic{RoadErrorCode::NonFiniteParameter, "segment start point is not finite"};
    }
    if (!std::isfinite(startHeading)) {
        return RoadDiagnostic{RoadErrorCode::NonFiniteParameter, "segment start heading is not finite"};
    }
    if (!std::isfinite(length) || length <= 0.0) {
        return RoadDiagnostic{RoadErrorCode::DegenerateSegment, "segment length must be finite and positive"};
    }
    return std::nullopt;
}

// Per-type accessors used by the variant dispatch free functions.
AlignmentSegmentKind kindOf(const LineSegment&) noexcept { return AlignmentSegmentKind::Line; }
AlignmentSegmentKind kindOf(const CircularArcSegment&) noexcept { return AlignmentSegmentKind::CircularArc; }

double lengthOf(const LineSegment& s) noexcept { return s.length; }
double lengthOf(const CircularArcSegment& s) noexcept { return s.length; }

AlignmentPoint startOf(const LineSegment& s) noexcept { return s.start; }
AlignmentPoint startOf(const CircularArcSegment& s) noexcept { return s.start; }

Heading startHeadingOf(const LineSegment& s) noexcept { return s.heading; }
Heading startHeadingOf(const CircularArcSegment& s) noexcept { return s.startHeading; }

Curvature startCurvatureOf(const LineSegment&) noexcept { return 0.0; }
Curvature startCurvatureOf(const CircularArcSegment& s) noexcept { return s.curvature; }

std::optional<RoadDiagnostic> validateOne(const LineSegment& s) noexcept {
    return validateCommon(s.start, s.heading, s.length);
}

std::optional<RoadDiagnostic> validateOne(const CircularArcSegment& s) noexcept {
    if (auto common = validateCommon(s.start, s.startHeading, s.length)) {
        return common;
    }
    if (!std::isfinite(s.curvature) || s.curvature == 0.0) {
        return RoadDiagnostic{RoadErrorCode::InvalidCurvature,
            "circular arc curvature must be finite and non-zero"};
    }
    return std::nullopt;
}

} // namespace

std::string_view alignmentSegmentKindName(const AlignmentSegmentKind kind) noexcept {
    return kindName(kind);
}

std::optional<AlignmentSegmentKind> alignmentSegmentKindFromName(const std::string_view name) noexcept {
    for (std::size_t index = 0;
         index <= static_cast<std::size_t>(AlignmentSegmentKind::Clothoid); ++index) {
        const auto kind = static_cast<AlignmentSegmentKind>(index);
        if (kindName(kind) == name) {
            return kind;
        }
    }
    return std::nullopt;
}

// ---- Line ----

AlignmentSample LineSegment::evaluate(const double sLocal) const noexcept {
    AlignmentSample sample;
    sample.position.easting = start.easting + sLocal * std::cos(heading);
    sample.position.northing = start.northing + sLocal * std::sin(heading);
    sample.heading = heading;
    sample.curvature = 0.0;
    return sample;
}

// ---- Circular arc ----

AlignmentSample CircularArcSegment::evaluate(const double sLocal) const noexcept {
    const double theta = startHeading + curvature * sLocal;
    const double invK = 1.0 / curvature;
    AlignmentSample sample;
    sample.position.easting = start.easting + invK * (std::sin(theta) - std::sin(startHeading));
    sample.position.northing = start.northing + invK * (std::cos(startHeading) - std::cos(theta));
    sample.heading = theta;
    sample.curvature = curvature;
    return sample;
}

// ---- Variant dispatch ----

AlignmentSample evaluateSegment(const AlignmentSegment& segment, const double sLocal) noexcept {
    return std::visit([&](const auto& s) { return s.evaluate(sLocal); }, segment);
}

double segmentLength(const AlignmentSegment& segment) noexcept {
    return std::visit([](const auto& s) { return lengthOf(s); }, segment);
}

AlignmentSegmentKind segmentKind(const AlignmentSegment& segment) noexcept {
    return std::visit([](const auto& s) { return kindOf(s); }, segment);
}

AlignmentPoint segmentStartPoint(const AlignmentSegment& segment) noexcept {
    return std::visit([](const auto& s) { return startOf(s); }, segment);
}

Heading segmentStartHeading(const AlignmentSegment& segment) noexcept {
    return std::visit([](const auto& s) { return startHeadingOf(s); }, segment);
}

Curvature segmentStartCurvature(const AlignmentSegment& segment) noexcept {
    return std::visit([](const auto& s) { return startCurvatureOf(s); }, segment);
}

AlignmentSample segmentEndSample(const AlignmentSegment& segment) noexcept {
    return std::visit([](const auto& s) { return s.endSample(); }, segment);
}

std::optional<RoadDiagnostic> validateSegment(const AlignmentSegment& segment) noexcept {
    return std::visit([](const auto& s) { return validateOne(s); }, segment);
}

} // namespace infraforge::domain::road
