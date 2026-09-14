#include "infraforge/domain/road/ReferenceAlignment.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace infraforge::domain::road {
namespace {

constexpr double kPi = 3.14159265358979323846;

// Smallest signed angular difference in (-pi, pi]. Uses std::remainder which
// is bounded (no iterative loop) and handles non-finite inputs by returning
// NaN, which callers detect via std::isfinite before comparing.
double angleDelta(double a, double b) noexcept {
    return std::remainder(a - b, 2.0 * kPi);
}

} // namespace

std::expected<ReferenceAlignment, std::vector<RoadDiagnostic>> ReferenceAlignment::build(
    std::vector<AlignmentSegment> segments,
    const double positionTolerance,
    const double headingTolerance,
    const double curvatureTolerance) {
    std::vector<RoadDiagnostic> diagnostics;

    // Validate tolerances before using them for continuity checks. A NaN
    // or infinite tolerance would disable validation (NaN comparisons are
    // always false), so they must be rejected explicitly.
    if (!std::isfinite(positionTolerance) || positionTolerance < 0.0) {
        diagnostics.push_back({RoadErrorCode::InvalidArgument,
            "position tolerance must be finite and non-negative"});
    }
    if (!std::isfinite(headingTolerance) || headingTolerance < 0.0) {
        diagnostics.push_back({RoadErrorCode::InvalidArgument,
            "heading tolerance must be finite and non-negative"});
    }
    if (!std::isfinite(curvatureTolerance) || curvatureTolerance < 0.0) {
        diagnostics.push_back({RoadErrorCode::InvalidArgument,
            "curvature tolerance must be finite and non-negative"});
    }
    if (!diagnostics.empty()) {
        return std::unexpected(std::move(diagnostics));
    }

    if (segments.empty()) {
        diagnostics.push_back({RoadErrorCode::EmptyAlignment, "alignment must contain at least one segment"});
        return std::unexpected(std::move(diagnostics));
    }

    // Per-segment structural validation.
    for (std::size_t i = 0; i < segments.size(); ++i) {
        if (auto d = validateSegment(segments[i])) {
            diagnostics.push_back(*d);
        }
    }
    if (!diagnostics.empty()) {
        return std::unexpected(std::move(diagnostics));
    }

    // Continuous stationing: segment i starts where segment i-1 ends.
    // Also validate that derived end geometry (position, heading, curvature)
    // is finite — individually finite parameters can produce non-finite
    // results through overflow (e.g. huge curvature * length).
    std::vector<StationedSegment> stationed;
    stationed.reserve(segments.size());
    Station cursor = 0.0;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        // Validate derived end geometry before accepting this segment.
        const AlignmentSample endSample = segmentEndSample(segments[i]);
        if (!std::isfinite(endSample.position.easting)
            || !std::isfinite(endSample.position.northing)
            || !std::isfinite(endSample.heading)
            || !std::isfinite(endSample.curvature)) {
            diagnostics.push_back({RoadErrorCode::NonFiniteParameter,
                "segment " + std::to_string(i)
                    + " produces non-finite end geometry from finite parameters"});
        }
        stationed.push_back({cursor, std::move(segments[i])});
        cursor += segmentLength(stationed.back().segment);
        if (!std::isfinite(cursor)) {
            diagnostics.push_back({RoadErrorCode::NonFiniteParameter,
                "station accumulation overflowed to non-finite at segment "
                    + std::to_string(i)});
        }
    }
    if (!diagnostics.empty()) {
        return std::unexpected(std::move(diagnostics));
    }
    const double totalLength = cursor;

    // Continuity between adjacent segments. Each segment is self-contained
    // (stores its own start point/heading), so the chain is validated against
    // the previous segment's computed end.
    for (std::size_t i = 1; i < stationed.size(); ++i) {
        const AlignmentSample prevEnd = segmentEndSample(stationed[i - 1].segment);
        const AlignmentPoint nextStart = segmentStartPoint(stationed[i].segment);
        const Heading nextHeading = segmentStartHeading(stationed[i].segment);
        const Curvature nextCurvature = segmentStartCurvature(stationed[i].segment);
        const Curvature prevEndCurvature = segmentEndCurvature(stationed[i - 1].segment);

        // G0: positional continuity.
        const double dx = nextStart.easting - prevEnd.position.easting;
        const double dy = nextStart.northing - prevEnd.position.northing;
        if (std::sqrt(dx * dx + dy * dy) > positionTolerance) {
            diagnostics.push_back({RoadErrorCode::PositionDiscontinuity,
                "segment " + std::to_string(i) + " start does not match segment "
                    + std::to_string(i - 1) + " end (G0 failure)"});
        }
        // G1: tangent/heading continuity (modulo 2*pi). std::remainder
        // returns NaN for non-finite inputs; detect and report that.
        const double hd = angleDelta(nextHeading, prevEnd.heading);
        if (!std::isfinite(hd) || std::abs(hd) > headingTolerance) {
            diagnostics.push_back({RoadErrorCode::HeadingDiscontinuity,
                "segment " + std::to_string(i) + " start heading does not match segment "
                    + std::to_string(i - 1) + " end heading (G1 failure)"});
        }
        // Curvature continuity.
        if (std::abs(nextCurvature - prevEndCurvature) > curvatureTolerance) {
            diagnostics.push_back({RoadErrorCode::CurvatureDiscontinuity,
                "segment " + std::to_string(i) + " start curvature does not match segment "
                    + std::to_string(i - 1) + " end curvature"});
        }
    }

    if (!diagnostics.empty()) {
        return std::unexpected(std::move(diagnostics));
    }
    return ReferenceAlignment{std::move(stationed), totalLength};
}

AlignmentSample ReferenceAlignment::evaluate(const Station s) const noexcept {
    if (segments_.empty()) {
        return AlignmentSample{};
    }
    // Non-finite station policy: NaN returns a default (zero) sample rather
    // than silently falling through to an arbitrary segment. +inf clamps to
    // the end; -inf clamps to the start. This keeps evaluation deterministic
    // and avoids undefined behavior in downstream comparisons.
    if (!std::isfinite(s)) {
        if (s == std::numeric_limits<double>::infinity()) {
            return segmentEndSample(segments_.back().segment);
        }
        if (s == -std::numeric_limits<double>::infinity()) {
            return evaluateSegment(segments_.front().segment, 0.0);
        }
        // NaN: return a zero/default sample.
        return AlignmentSample{};
    }
    Station clamped = s;
    if (clamped <= 0.0) {
        return evaluateSegment(segments_.front().segment, 0.0);
    }
    if (clamped >= totalLength_) {
        return segmentEndSample(segments_.back().segment);
    }
    for (const StationedSegment& stood : segments_) {
        const double len = segmentLength(stood.segment);
        if (clamped <= stood.startStation + len) {
            return evaluateSegment(stood.segment, clamped - stood.startStation);
        }
    }
    return segmentEndSample(segments_.back().segment);
}

std::optional<std::size_t> ReferenceAlignment::segmentIndexAt(const Station s) const noexcept {
    if (segments_.empty()) {
        return std::nullopt;
    }
    // Non-finite station policy: NaN and infinities are outside the valid
    // station range [0, totalLength]. NaN comparisons are always false, so
    // it would fall through to returning the last segment — that is wrong.
    // +inf and -inf are also outside the closed range.
    if (!std::isfinite(s) || s < 0.0 || s > totalLength_) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < segments_.size(); ++i) {
        const double len = segmentLength(segments_[i].segment);
        if (s <= segments_[i].startStation + len) {
            return i;
        }
    }
    return segments_.size() - 1;
}

StationRange ReferenceAlignment::stationRange() const noexcept {
    return StationRange{0.0, totalLength_};
}

} // namespace infraforge::domain::road
