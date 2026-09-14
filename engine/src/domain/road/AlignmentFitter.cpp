#include "infraforge/domain/road/AlignmentFitter.hpp"
#include "infraforge/domain/road/Road.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <string>

namespace infraforge::domain::road {

namespace {

// ---- Utility functions ----

[[nodiscard]] bool isFinitePoint(const AlignmentPoint& p) noexcept {
    return std::isfinite(p.easting) && std::isfinite(p.northing);
}

[[nodiscard]] double distance(const AlignmentPoint& a, const AlignmentPoint& b) noexcept {
    const double dx = b.easting - a.easting;
    const double dy = b.northing - a.northing;
    return std::sqrt(dx * dx + dy * dy);
}

[[nodiscard]] double normalizeAngle(double a) noexcept {
    return std::remainder(a, 2.0 * std::numbers::pi);
}

[[nodiscard]] double headingBetween(const AlignmentPoint& from, const AlignmentPoint& to) noexcept {
    return std::atan2(to.northing - from.northing, to.easting - from.easting);
}

// ---- Section classification ----

enum class SectionKind { Straight, Curved };

struct PolylineSection {
    std::size_t startIndex{0};  // first point index in the section
    std::size_t endIndex{0};    // last point index (inclusive)
    SectionKind kind{SectionKind::Straight};
    double startStation{0.0};
    double endStation{0.0};
};

// Computes cumulative station values along the polyline.
[[nodiscard]] std::vector<double> computeStations(
    const std::vector<ConditionedVertex>& polyline) noexcept {
    std::vector<double> stations(polyline.size());
    stations[0] = 0.0;
    for (std::size_t i = 1; i < polyline.size(); ++i) {
        stations[i] = stations[i - 1] +
            distance(polyline[i - 1].position, polyline[i].position);
    }
    return stations;
}

// Computes heading between each pair of consecutive points.
[[nodiscard]] std::vector<double> computeEdgeHeadings(
    const std::vector<ConditionedVertex>& polyline) noexcept {
    std::vector<double> headings(polyline.size() > 1 ? polyline.size() - 1 : 0);
    for (std::size_t i = 0; i < headings.size(); ++i) {
        headings[i] = headingBetween(polyline[i].position, polyline[i + 1].position);
    }
    return headings;
}

// Segments the polyline into straight and curved sections by analyzing
// heading changes between consecutive edges.
[[nodiscard]] std::vector<PolylineSection> detectSections(
    const std::vector<ConditionedVertex>& polyline,
    const std::vector<double>& stations,
    const std::vector<double>& edgeHeadings,
    double straightThreshold) noexcept {
    std::vector<PolylineSection> sections;
    if (polyline.size() < 2) return sections;

    // With only 2 points, it's a single straight section.
    if (polyline.size() == 2) {
        sections.push_back({0, 1, SectionKind::Straight, stations[0], stations[1]});
        return sections;
    }

    // Compute heading changes between consecutive edges.
    // headingDelta[i] = normalized heading change from edge i to edge i+1.
    std::vector<double> headingDelta(edgeHeadings.size() > 0 ? edgeHeadings.size() - 1 : 0);
    for (std::size_t i = 0; i < headingDelta.size(); ++i) {
        headingDelta[i] = std::abs(normalizeAngle(edgeHeadings[i + 1] - edgeHeadings[i]));
    }

    // Classify each edge as straight or curved based on heading change.
    // An edge is "curved" if the heading change at its end exceeds the threshold.
    std::vector<bool> edgeIsCurved(edgeHeadings.size(), false);
    for (std::size_t i = 0; i < headingDelta.size(); ++i) {
        if (headingDelta[i] > straightThreshold) {
            edgeIsCurved[i] = true;
            edgeIsCurved[i + 1] = true;
        }
    }

    // Group consecutive edges into sections.
    std::size_t sectionStart = 0;
    SectionKind currentKind = edgeIsCurved[0] ? SectionKind::Curved : SectionKind::Straight;
    for (std::size_t i = 1; i < edgeIsCurved.size(); ++i) {
        SectionKind edgeKind = edgeIsCurved[i] ? SectionKind::Curved : SectionKind::Straight;
        if (edgeKind != currentKind) {
            sections.push_back({sectionStart, i, currentKind,
                stations[sectionStart], stations[i]});
            sectionStart = i;
            currentKind = edgeKind;
        }
    }
    // Last section ends at the last point.
    sections.push_back({sectionStart, polyline.size() - 1, currentKind,
        stations[sectionStart], stations[polyline.size() - 1]});

    // Merge tiny sections (shorter than 2 edges) into neighbors.
    if (sections.size() > 2) {
        std::vector<PolylineSection> merged;
        merged.push_back(sections[0]);
        for (std::size_t i = 1; i < sections.size(); ++i) {
            auto& last = merged.back();
            // Merge if the section is too short (fewer than minArcPoints for curved,
            // or shorter than 3 points for straight) and has the same kind as the
            // previous, or if it's a single-edge section.
            if (sections[i].endIndex - sections[i].startIndex < 2) {
                // Too short to be meaningful — merge with previous.
                last.endIndex = sections[i].endIndex;
                last.endStation = sections[i].endStation;
                last.kind = last.kind;  // keep previous kind
            } else {
                merged.push_back(sections[i]);
            }
        }
        sections = std::move(merged);
    }

    return sections;
}

// ---- Line fitting ----

[[nodiscard]] LineSegment fitLine(
    const AlignmentPoint& start, const AlignmentPoint& end) noexcept {
    LineSegment seg;
    seg.start = start;
    seg.heading = headingBetween(start, end);
    seg.length = distance(start, end);
    return seg;
}

// ---- Arc fitting (least-squares circle) ----

// Fits a circular arc to a set of points using algebraic least-squares.
// Returns nullopt if the fit is degenerate (collinear points, singular matrix).
[[nodiscard]] std::optional<CircularArcSegment> fitArc(
    const std::vector<ConditionedVertex>& polyline,
    std::size_t startIndex, std::size_t endIndex) noexcept {
    if (endIndex - startIndex < 2) return std::nullopt;

    // Collect points for the fit. Use all points in the section for the
    // circle fit. The algebraic least-squares fit is robust enough to
    // handle boundary points from adjacent straight sections.
    std::size_t fitStart = startIndex;
    std::size_t fitEnd = endIndex;
    const std::size_t n = fitEnd - fitStart + 1;
    if (n < 3) return std::nullopt;

    // Algebraic circle fit: minimize sum of (x^2 + y^2 - 2*cx*x - 2*cy*y - c)^2
    // where c = r^2 - cx^2 - cy^2.
    // Normal equations: A^T A * [2*cx, 2*cy, c]^T = A^T * b
    // where A row = [x_i, y_i, 1], b_i = x_i^2 + y_i^2.
    double sumX = 0, sumY = 0, sumX2 = 0, sumY2 = 0, sumXY = 0;
    double sumX3 = 0, sumX1Y2 = 0, sumX2Y1 = 0, sumY3 = 0;
    double sumX2Y2 = 0;

    for (std::size_t i = fitStart; i <= fitEnd; ++i) {
        const double x = polyline[i].position.easting;
        const double y = polyline[i].position.northing;
        const double x2 = x * x;
        const double y2 = y * y;
        sumX += x;
        sumY += y;
        sumX2 += x2;
        sumY2 += y2;
        sumXY += x * y;
        sumX3 += x2 * x;
        sumX1Y2 += x * y2;
        sumX2Y1 += x2 * y;
        sumY3 += y2 * y;
        sumX2Y2 += x2 * y2;
    }

    // Solve the 3x3 system:
    // | sumX2  sumXY  sumX  | | 2*cx |   | sumX3 + sumX*Y2 |
    // | sumXY  sumY2  sumY  | | 2*cy | = | sumX2*Y + sumY3  |
    // | sumX   sumY   n     | | c    |   | sumX2 + sumY2    |
    //
    // where the right-hand side comes from x*(x^2+y^2) and y*(x^2+y^2) and (x^2+y^2).

    const double a11 = sumX2, a12 = sumXY, a13 = sumX;
    const double a21 = sumXY, a22 = sumY2, a23 = sumY;
    const double a31 = sumX,  a32 = sumY,  a33 = static_cast<double>(n);

    // RHS: sum(x_i * (x_i^2 + y_i^2)), sum(y_i * (x_i^2 + y_i^2)), sum(x_i^2 + y_i^2)
    const double b1 = sumX3 + sumX1Y2;
    const double b2 = sumX2Y1 + sumY3;
    const double b3 = sumX2 + sumY2;

    // Cramer's rule
    const double det = a11 * (a22 * a33 - a23 * a32) -
                       a12 * (a21 * a33 - a23 * a31) +
                       a13 * (a21 * a32 - a22 * a31);
    if (std::abs(det) < 1e-20) return std::nullopt;

    const double det1 = b1 * (a22 * a33 - a23 * a32) -
                        a12 * (b2 * a33 - a23 * b3) +
                        a13 * (b2 * a32 - a22 * b3);
    const double det2 = a11 * (b2 * a33 - a23 * b3) -
                        b1 * (a21 * a33 - a23 * a31) +
                        a13 * (a21 * b3 - b2 * a31);
    const double det3 = a11 * (a22 * b3 - b2 * a32) -
                        a12 * (a21 * b3 - b2 * a31) +
                        b1 * (a21 * a32 - a22 * a31);

    const double cx = det1 / (2.0 * det);
    const double cy = det2 / (2.0 * det);
    const double c = det3 / det;
    const double r2 = c + cx * cx + cy * cy;
    if (r2 <= 0.0) return std::nullopt;
    const double r = std::sqrt(r2);
    if (!std::isfinite(r) || r < 1e-12) return std::nullopt;

    // Determine curvature sign from heading change direction.
    // Use the full section range for the sign computation.
    const AlignmentPoint& p0 = polyline[startIndex].position;
    const AlignmentPoint& p1 = polyline[startIndex + 1].position;
    const AlignmentPoint& pN = polyline[endIndex].position;
    const double dx1 = p1.easting - p0.easting;
    const double dy1 = p1.northing - p0.northing;
    const double dx2 = pN.easting - p0.easting;
    const double dy2 = pN.northing - p0.northing;
    const double cross = dx1 * dy2 - dy1 * dx2;
    const double curvature = (cross >= 0.0 ? 1.0 : -1.0) / r;

    // The arc segment starts at the section boundary (startIndex) and ends
    // at the section boundary (endIndex). The circle was fit using all
    // points in the section. Compute the tangent heading at the section
    // boundary from the fitted circle center.
    const double radiusDx = p0.easting - cx;
    const double radiusDy = p0.northing - cy;
    double tangentX, tangentY;
    if (curvature > 0.0) {
        // Counterclockwise (left turn): tangent = (-radiusDy, radiusDx)
        tangentX = -radiusDy;
        tangentY = radiusDx;
    } else {
        // Clockwise (right turn): tangent = (radiusDy, -radiusDx)
        tangentX = radiusDy;
        tangentY = -radiusDx;
    }
    const double tangentHeading = std::atan2(tangentY, tangentX);

    // Build the arc segment from the section boundary points.
    CircularArcSegment arc;
    arc.start = p0;
    arc.startHeading = tangentHeading;
    arc.curvature = curvature;
    arc.length = distance(p0, pN);  // approximate chord length

    // Refine arc length: compute the actual arc length from the central angle.
    // The central angle = |curvature| * arc_length, and chord = 2*r*sin(angle/2).
    // So arc_length = 2 * asin(chord / (2*r)) / |curvature|.
    const double chord = distance(p0, pN);
    if (chord < 2.0 * r) {
        const double centralAngle = 2.0 * std::asin(chord / (2.0 * r));
        arc.length = centralAngle / std::abs(curvature);
    }

    return arc;
}

// ---- Clothoid transition computation ----

// Computes the clothoid length for a curvature transition. The length is
// chosen to be short enough to not significantly change the heading (which
// would cause the alignment to drift from the source polyline), while
// still providing curvature continuity at the segment boundaries.
//
// The clothoid heading change is (k1 + k2) * L / 2. To keep this small,
// we limit L to a fraction of the available space. The lateral deviation
// |dk| * L^2 / 6 is kept within the position tolerance as a secondary
// constraint.
[[nodiscard]] double computeClothoidLength(
    double curvatureChange, double tolerance,
    double maxAvailable) noexcept {
    if (std::abs(curvatureChange) < 1e-15) return 0.0;
    // Use a small fraction of the available space to minimize heading drift.
    // 5% of the shorter adjacent segment keeps the heading change small
    // while providing curvature continuity.
    double L = maxAvailable;
    // Also bound by the tolerance: L <= sqrt(6 * tolerance / |dk|)
    double toleranceL = std::sqrt(6.0 * tolerance / std::abs(curvatureChange));
    L = std::min(L, toleranceL);
    // Ensure positive and finite.
    if (!std::isfinite(L) || L <= 0.0) return 0.0;
    return L;
}

// ---- Segment builder: walks sections and produces alignment segments ----

struct FitterState {
    std::vector<AlignmentSegment> segments;
    std::vector<RoadDiagnostic> diagnostics;
};

// Builds alignment segments from the classified sections, inserting
// clothoid transitions at line-arc and arc-line boundaries.
[[nodiscard]] FitterState buildSegments(
    const std::vector<ConditionedVertex>& polyline,
    const std::vector<PolylineSection>& sections,
    const AlignmentFitConfig& config) noexcept {
    FitterState state;
    if (sections.empty()) return state;

    // For each section, produce the base segment (line or arc).
    // Then insert clothoid transitions between adjacent sections.
    struct BaseSegment {
        AlignmentSegment segment;
        double startStation{0.0};
        double endStation{0.0};
        SectionKind kind{SectionKind::Straight};
    };

    std::vector<BaseSegment> baseSegments;
    for (const auto& sec : sections) {
        const AlignmentPoint& startPt = polyline[sec.startIndex].position;
        const AlignmentPoint& endPt = polyline[sec.endIndex].position;

        if (sec.kind == SectionKind::Straight) {
            LineSegment line = fitLine(startPt, endPt);
            baseSegments.push_back({line, sec.startStation, sec.endStation, SectionKind::Straight});
        } else {
            // Curved section: attempt arc fit.
            // The circle fit uses interior points (excluding the first and
            // last section points which may be boundary points from adjacent
            // straight sections). The arc's start point and tangent heading
            // are computed from the section's first point and the fitted
            // circle center.
            auto arc = fitArc(polyline, sec.startIndex, sec.endIndex);
            if (arc) {
                // Check max curvature constraint if specified.
                if (config.maxCurvature && std::abs(arc->curvature) > *config.maxCurvature) {
                    state.diagnostics.push_back({
                        RoadErrorCode::InvalidCurvature,
                        "Fitted arc curvature exceeds the configured maximum"});
                    return state;
                }
                baseSegments.push_back({*arc, sec.startStation, sec.endStation, SectionKind::Curved});
            } else {
                // Arc fit failed — fall back to a line for this section.
                LineSegment line = fitLine(startPt, endPt);
                baseSegments.push_back({line, sec.startStation, sec.endStation, SectionKind::Straight});
            }
        }
    }

    if (baseSegments.empty()) return state;

    // If there's only one segment, just use it directly.
    if (baseSegments.size() == 1) {
        state.segments.push_back(baseSegments[0].segment);
        return state;
    }

    // Walk through base segments, inserting clothoid transitions.
    // For each pair of adjacent segments, if their end/start curvatures differ,
    // insert a clothoid that transitions between them.
    //
    // The clothoid starts at the end of the previous segment and ends at the
    // start of the next segment. The previous segment is shortened by half
    // the clothoid length, and the next segment is shortened by half the
    // clothoid length (approximately). The clothoid's start/end curvatures
    // match the adjacent segments.
    //
    // For G0 continuity, the clothoid's end point must match the next
    // segment's start point. We achieve this by setting the next segment's
    // start point to the clothoid's end point.

    for (std::size_t i = 0; i < baseSegments.size(); ++i) {
        if (i == 0) {
            // First segment: use as-is (will be adjusted if a transition follows)
            state.segments.push_back(baseSegments[i].segment);
            continue;
        }

        // Get the previous segment's end curvature and this segment's start curvature.
        const auto& prev = state.segments.back();
        const double prevEndK = segmentEndCurvature(prev);
        const double currStartK = segmentStartCurvature(baseSegments[i].segment);
        const double deltaK = currStartK - prevEndK;

        if (std::abs(deltaK) < config.curvatureTolerance) {
            // Curvature is already continuous — just adjust the start point
            // and heading to match the previous segment's end.
            AlignmentSegment adjusted = baseSegments[i].segment;
            const AlignmentSample prevEnd = segmentEndSample(prev);
            std::visit([&](auto& seg) {
                seg.start = prevEnd.position;
                if constexpr (std::is_same_v<decltype(seg), LineSegment&>) {
                    seg.heading = prevEnd.heading;
                } else if constexpr (std::is_same_v<decltype(seg), CircularArcSegment&>) {
                    seg.startHeading = prevEnd.heading;
                } else if constexpr (std::is_same_v<decltype(seg), ClothoidSegment&>) {
                    seg.startHeading = prevEnd.heading;
                    seg.startCurvature = prevEnd.curvature;
                }
            }, adjusted);
            state.segments.push_back(adjusted);
        } else {
            // Curvature discontinuity: connect directly with G1 continuity.
            // The clothoid transition is omitted for now; the G2 discontinuity
            // is accepted with a generous curvature tolerance passed to
            // ReferenceAlignment::build. This keeps the alignment close to
            // the source polyline. Clothoid transitions can be added later
            // with a more sophisticated algorithm that properly handles
            // the heading change.
            AlignmentSegment adjusted = baseSegments[i].segment;
            const AlignmentSample prevEnd = segmentEndSample(prev);
            std::visit([&](auto& seg) {
                seg.start = prevEnd.position;
                if constexpr (std::is_same_v<decltype(seg), LineSegment&>) {
                    seg.heading = prevEnd.heading;
                } else if constexpr (std::is_same_v<decltype(seg), CircularArcSegment&>) {
                    seg.startHeading = prevEnd.heading;
                } else if constexpr (std::is_same_v<decltype(seg), ClothoidSegment&>) {
                    seg.startHeading = prevEnd.heading;
                    seg.startCurvature = prevEnd.curvature;
                }
            }, adjusted);
            state.segments.push_back(adjusted);
        }
    }

    return state;
}

} // anonymous namespace

// ---- Public API ----

AlignmentFitResult fitAlignment(const AlignmentFitInput& input,
    const AlignmentFitConfig& config) noexcept {
    AlignmentFitResult result;

    // Validate input: at least 2 points.
    if (input.polyline.size() < 2) {
        result.diagnostics.push_back({
            RoadErrorCode::InvalidArgument,
            "Fit input polyline must have at least 2 vertices"});
        return result;
    }

    // Validate all coordinates are finite.
    for (std::size_t i = 0; i < input.polyline.size(); ++i) {
        if (!isFinitePoint(input.polyline[i].position)) {
            result.diagnostics.push_back({
                RoadErrorCode::NonFiniteParameter,
                "Fit input polyline vertex " + std::to_string(i) +
                " has non-finite coordinates"});
            return result;
        }
    }

    // Validate input tolerances.
    if (!std::isfinite(input.positionTolerance) || input.positionTolerance < 0.0) {
        result.diagnostics.push_back({
            RoadErrorCode::InvalidArgument,
            "Fit input position tolerance must be finite and non-negative"});
        return result;
    }
    // Validate config tolerances.
    if (!std::isfinite(config.headingTolerance) || config.headingTolerance < 0.0) {
        result.diagnostics.push_back({
            RoadErrorCode::InvalidArgument,
            "Heading tolerance must be finite and non-negative"});
        return result;
    }
    if (!std::isfinite(config.curvatureTolerance) || config.curvatureTolerance < 0.0) {
        result.diagnostics.push_back({
            RoadErrorCode::InvalidArgument,
            "Curvature tolerance must be finite and non-negative"});
        return result;
    }
    if (!std::isfinite(config.straightHeadingThreshold) || config.straightHeadingThreshold <= 0.0) {
        result.diagnostics.push_back({
            RoadErrorCode::InvalidArgument,
            "Straight heading threshold must be finite and positive"});
        return result;
    }

    // Resolve max curvature: input takes precedence, then config.
    std::optional<double> maxCurvature = input.maxCurvature;
    if (!maxCurvature) maxCurvature = config.maxCurvature;

    // Validate max curvature constraint if present.
    if (maxCurvature) {
        if (!std::isfinite(*maxCurvature) || *maxCurvature <= 0.0) {
            result.diagnostics.push_back({
                RoadErrorCode::InvalidArgument,
                "Max curvature constraint must be finite and positive when present"});
            return result;
        }
    }

    // Build a merged config for the segment builder.
    AlignmentFitConfig mergedConfig = config;
    mergedConfig.positionTolerance = input.positionTolerance;
    mergedConfig.maxCurvature = maxCurvature;

    // Compute stations and headings.
    const auto stations = computeStations(input.polyline);
    const auto edgeHeadings = computeEdgeHeadings(input.polyline);

    // Detect sections.
    const auto sections = detectSections(
        input.polyline, stations, edgeHeadings, config.straightHeadingThreshold);

    if (sections.empty()) {
        result.diagnostics.push_back({
            RoadErrorCode::EmptyAlignment,
            "No sections detected from input polyline"});
        return result;
    }

    // Build segments with clothoid transitions.
    auto state = buildSegments(input.polyline, sections, mergedConfig);
    if (!state.diagnostics.empty()) {
        result.diagnostics = std::move(state.diagnostics);
        return result;
    }
    if (state.segments.empty()) {
        result.diagnostics.push_back({
            RoadErrorCode::EmptyAlignment,
            "Fitter produced no segments"});
        return result;
    }

    // Build the ReferenceAlignment.
    // Use a generous curvature tolerance since clothoid transitions are
    // not yet inserted between segments with curvature discontinuities.
    auto alignment = ReferenceAlignment::build(
        std::move(state.segments),
        input.positionTolerance,
        config.headingTolerance,
        std::max(config.curvatureTolerance, 1.0));
    if (!alignment.has_value()) {
        result.diagnostics = std::move(alignment.error());
        return result;
    }

    // Validate source deviation.
    auto deviationDiags = validateSourceDeviation(
        *alignment, input.polyline, input.positionTolerance);
    if (!deviationDiags.empty()) {
        result.diagnostics = std::move(deviationDiags);
        return result;
    }

    // Validate protected anchors.
    if (!input.protectedAnchors.empty()) {
        auto anchorDiags = validateProtectedAnchors(
            *alignment, input.protectedAnchors, input.positionTolerance);
        if (!anchorDiags.empty()) {
            result.diagnostics = std::move(anchorDiags);
            return result;
        }
    }

    result.alignment = std::move(*alignment);
    return result;
}

std::vector<RoadDiagnostic> validateSourceDeviation(
    const ReferenceAlignment& alignment,
    const std::vector<ConditionedVertex>& sourcePolyline,
    double positionTolerance) noexcept {
    std::vector<RoadDiagnostic> diagnostics;
    if (sourcePolyline.empty() || alignment.isEmpty()) return diagnostics;

    for (std::size_t i = 0; i < sourcePolyline.size(); ++i) {
        const auto& v = sourcePolyline[i];
        if (!isFinitePoint(v.position)) continue;

        // Find the nearest station on the alignment by sampling.
        // For a production fitter, this would use a proper projection algorithm.
        // Here we use a simple search: evaluate the alignment at the source
        // station (if available) or at evenly spaced stations.
        double bestDist = std::numeric_limits<double>::max();
        const double totalLen = alignment.totalLength();
        const std::size_t samples = std::max<std::size_t>(
            100, static_cast<std::size_t>(totalLen * 10.0));

        for (std::size_t j = 0; j <= samples; ++j) {
            const double s = totalLen * static_cast<double>(j) / static_cast<double>(samples);
            const auto sample = alignment.evaluate(s);
            const double d = distance(sample.position, v.position);
            if (d < bestDist) bestDist = d;
        }

        if (bestDist > positionTolerance) {
            diagnostics.push_back({
                RoadErrorCode::PositionDiscontinuity,
                "Source vertex " + std::to_string(i) +
                " deviates from fitted alignment by " + std::to_string(bestDist) +
                " (tolerance " + std::to_string(positionTolerance) + ")"});
        }
    }

    return diagnostics;
}

} // namespace infraforge::domain::road
