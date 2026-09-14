#include "infraforge/domain/road/AlignmentPrimitives.hpp"

#include <array>
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
AlignmentSegmentKind kindOf(const ClothoidSegment&) noexcept { return AlignmentSegmentKind::Clothoid; }

double lengthOf(const LineSegment& s) noexcept { return s.length; }
double lengthOf(const CircularArcSegment& s) noexcept { return s.length; }
double lengthOf(const ClothoidSegment& s) noexcept { return s.length; }

AlignmentPoint startOf(const LineSegment& s) noexcept { return s.start; }
AlignmentPoint startOf(const CircularArcSegment& s) noexcept { return s.start; }
AlignmentPoint startOf(const ClothoidSegment& s) noexcept { return s.start; }

Heading startHeadingOf(const LineSegment& s) noexcept { return s.heading; }
Heading startHeadingOf(const CircularArcSegment& s) noexcept { return s.startHeading; }
Heading startHeadingOf(const ClothoidSegment& s) noexcept { return s.startHeading; }

Curvature startCurvatureOf(const LineSegment&) noexcept { return 0.0; }
Curvature startCurvatureOf(const CircularArcSegment& s) noexcept { return s.curvature; }
Curvature startCurvatureOf(const ClothoidSegment& s) noexcept { return s.startCurvature; }

Curvature endCurvatureOf(const LineSegment&) noexcept { return 0.0; }
Curvature endCurvatureOf(const CircularArcSegment& s) noexcept { return s.curvature; }
Curvature endCurvatureOf(const ClothoidSegment& s) noexcept { return s.endCurvature; }

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

std::optional<RoadDiagnostic> validateOne(const ClothoidSegment& s) noexcept {
    if (auto common = validateCommon(s.start, s.startHeading, s.length)) {
        return common;
    }
    if (!std::isfinite(s.startCurvature) || !std::isfinite(s.endCurvature)) {
        return RoadDiagnostic{RoadErrorCode::NonFiniteParameter,
            "clothoid curvatures must be finite"};
    }
    return std::nullopt;
}

// 8-point Gauss-Legendre nodes/weights on [-1, 1]. Fixed constants make the
// clothoid position integral deterministic and allocation-free.
struct GlNode {
    double x;
    double w;
};
constexpr std::array<GlNode, 8> kGl8{{
    {-0.960289856497536286, 0.101228536290376259},
    {-0.796666477413626740, 0.222381034453374470},
    {-0.525532409916328982, 0.313706645877887287},
    {-0.183434642495657470, 0.362683783378361983},
    { 0.183434642495657470, 0.362683783378361983},
    { 0.525532409916328982, 0.313706645877887287},
    { 0.796666477413626740, 0.222381034453374470},
    { 0.960289856497536286, 0.101228536290376259},
}};

constexpr double kPi = 3.14159265358979323846;

// Integrates (cos theta, sin theta) over [0, s] where theta(t) = theta0 + kappa0*t + alpha*t^2,
// using composite 8-point Gauss-Legendre. Subdivision keeps each subinterval's
// phase change <= pi/4 so the smooth integrand is integrated to near machine
// precision; the subinterval count is bounded and deterministic. No heap
// allocations are performed per evaluation.
void integrateClothoid(double theta0, double kappa0, double alpha, double s,
    double& outCos, double& outSin) noexcept {
    if (s == 0.0) {
        outCos = 0.0;
        outSin = 0.0;
        return;
    }
    const double phaseChange = std::abs(kappa0 * s + alpha * s * s);
    if (!std::isfinite(phaseChange)) {
        // Finite parameters can overflow through multiplication; return zero
        // rather than producing non-finite results or unbounded subdivision.
        outCos = 0.0;
        outSin = 0.0;
        return;
    }
    constexpr double kMaxPhasePerSub = kPi / 4.0;
    long long m = static_cast<long long>(phaseChange / kMaxPhasePerSub) + 1;
    constexpr long long kMaxSub = 4096;
    if (m > kMaxSub) {
        m = kMaxSub;
    }
    const double h = s / static_cast<double>(m);

    double sumCos = 0.0;
    double sumSin = 0.0;
    for (long long i = 0; i < m; ++i) {
        const double a = static_cast<double>(i) * h;
        const double mid = a + h * 0.5;
        const double half = h * 0.5;
        for (const GlNode& node : kGl8) {
            const double t = mid + half * node.x;
            const double theta = theta0 + kappa0 * t + alpha * t * t;
            const double wdt = node.w * half;
            sumCos += std::cos(theta) * wdt;
            sumSin += std::sin(theta) * wdt;
        }
    }
    outCos = sumCos;
    outSin = sumSin;
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

// ---- Clothoid ----

AlignmentSample ClothoidSegment::evaluate(const double sLocal) const noexcept {
    const double alpha = (endCurvature - startCurvature) / (2.0 * length);
    const double theta = startHeading + startCurvature * sLocal + alpha * sLocal * sLocal;
    AlignmentSample sample;
    sample.heading = theta;
    sample.curvature = startCurvature + (endCurvature - startCurvature) * sLocal / length;
    double dx = 0.0;
    double dy = 0.0;
    integrateClothoid(startHeading, startCurvature, alpha, sLocal, dx, dy);
    sample.position.easting = start.easting + dx;
    sample.position.northing = start.northing + dy;
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

Curvature segmentEndCurvature(const AlignmentSegment& segment) noexcept {
    return std::visit([](const auto& s) { return endCurvatureOf(s); }, segment);
}

AlignmentSample segmentEndSample(const AlignmentSegment& segment) noexcept {
    return std::visit([](const auto& s) { return s.endSample(); }, segment);
}

std::optional<RoadDiagnostic> validateSegment(const AlignmentSegment& segment) noexcept {
    return std::visit([](const auto& s) { return validateOne(s); }, segment);
}

} // namespace infraforge::domain::road
