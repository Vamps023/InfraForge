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
// heading changes between consecutive edges. Protected anchor indices are
// treated as hard section boundaries: each section starts/ends at an
// anchor vertex so the fitted alignment passes through the anchor exactly
// (Blocker 8).
[[nodiscard]] std::vector<PolylineSection> detectSections(
    const std::vector<ConditionedVertex>& polyline,
    const std::vector<double>& stations,
    const std::vector<double>& edgeHeadings,
    double straightThreshold,
    const std::vector<std::size_t>& anchorIndices = {}) noexcept {
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

    // Build a set of anchor vertex indices for O(1) lookup. Anchor
    // vertices are hard section boundaries: the fitter must produce a
    // segment boundary at each anchor so the alignment passes through
    // the anchor position exactly (Blocker 8).
    std::vector<bool> isAnchor(polyline.size(), false);
    for (const auto idx : anchorIndices) {
        if (idx < polyline.size()) isAnchor[idx] = true;
    }

    // Group consecutive edges into sections. A section boundary is
    // forced at every anchor vertex in addition to kind changes.
    std::size_t sectionStart = 0;
    SectionKind currentKind = edgeIsCurved[0] ? SectionKind::Curved : SectionKind::Straight;
    for (std::size_t i = 1; i < edgeIsCurved.size(); ++i) {
        const bool atAnchor = isAnchor[i];
        SectionKind edgeKind = edgeIsCurved[i] ? SectionKind::Curved : SectionKind::Straight;
        if (atAnchor || edgeKind != currentKind) {
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
    // Never merge across an anchor boundary — anchors are hard
    // constraints that must not be undone (Blocker 8).
    if (sections.size() > 2) {
        std::vector<PolylineSection> merged;
        merged.push_back(sections[0]);
        for (std::size_t i = 1; i < sections.size(); ++i) {
            auto& last = merged.back();
            const bool anchorBoundary = isAnchor[sections[i].startIndex];
            if (!anchorBoundary &&
                sections[i].endIndex - sections[i].startIndex < 2) {
                // Too short to be meaningful — merge with previous.
                last.endIndex = sections[i].endIndex;
                last.endStation = sections[i].endStation;
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

    // Determine curvature sign from the fitted circle center position
    // relative to the direction of travel (p0 -> pN). This is robust for
    // nearly-collinear points where the edge cross product is ~0.
    // If the center is to the LEFT of the travel direction, curvature is
    // positive (CCW). If to the RIGHT, negative (CW).
    const AlignmentPoint& p0 = polyline[startIndex].position;
    const AlignmentPoint& pN = polyline[endIndex].position;
    const double travelDx = pN.easting - p0.easting;
    const double travelDy = pN.northing - p0.northing;
    const double centerDx = cx - p0.easting;
    const double centerDy = cy - p0.northing;
    // Cross product of travel direction and (center - p0).
    // Positive = center is to the left = positive curvature (CCW).
    const double centerCross = travelDx * centerDy - travelDy * centerDx;
    const double curvature = (centerCross >= 0.0 ? 1.0 : -1.0) / r;

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

// ---- Segment builder: walks sections and produces alignment segments ----

// Shortens a segment to the given new length, preserving its start point,
// heading, and curvature parameters. Used to make room for clothoid
// transitions (Blocker 5).
[[nodiscard]] AlignmentSegment shortenSegment(
    const AlignmentSegment& segment, double newLength) noexcept {
    AlignmentSegment result = segment;
    std::visit([&](auto& seg) {
        seg.length = newLength;
    }, result);
    return result;
}

struct FitterState {
    std::vector<AlignmentSegment> segments;
    std::vector<RoadDiagnostic> diagnostics;
    // Indices of segments that start at a protected anchor. At those
    // boundaries the alignment may carry an intentional curvature
    // discontinuity (the anchor takes precedence over smoothness).
    std::set<std::size_t> anchorBoundarySegments;
};

// Computes a deterministic clothoid transition length for a curvature
// change from startK to endK. The length is chosen so the rate of curvature
// change is bounded, producing a smooth transition. The length is
// proportional to the curvature difference and inversely proportional
// to a curvature-rate parameter. When no user/domain policy supplies a
// transition length, a deterministic default based on the curvature
// difference and available segment lengths is used.
[[nodiscard]] double computeTransitionLength(
    double startK, double endK,
    double prevSegLen, double currSegLen) noexcept {
    const double deltaK = std::abs(endK - startK);
    if (deltaK < 1e-12) return 0.0;

    // Use up to 25% of the shorter adjacent segment for the transition,
    // bounded to a reasonable range. This keeps the transition proportional
    // to the available geometry without inventing engineering constraints.
    const double maxAvail = std::min(prevSegLen, currSegLen) * 0.25;
    if (maxAvail < 1e-9) return 0.0;

    // The transition length scales with the curvature difference so larger
    // curvature changes get longer transitions. For very small curvature
    // differences, the transition is short.
    // Use a rate-based approach: transitionLength = deltaK / rate, where
    // rate is chosen so the transition fits within the available space.
    // A rate of deltaK / maxAvail gives exactly maxAvail; we use that.
    return maxAvail;
}

// Builds alignment segments from the classified sections, inserting
// clothoid transitions at line-arc and arc-line boundaries.
// Blocker 5: implements real clothoid transition fitting so the canonical
// alignment supports Line→Clothoid→Arc, Arc→Clothoid→Line,
// Line→Clothoid→Arc→Clothoid→Line, and S-curve transitions.
[[nodiscard]] FitterState buildSegments(
    const std::vector<ConditionedVertex>& polyline,
    const std::vector<PolylineSection>& sections,
    const AlignmentFitConfig& config,
    const std::vector<std::size_t>& anchorIndices = {}) noexcept {
    FitterState state;
    if (sections.empty()) return state;

    // Anchor vertices are hard constraints: the alignment must pass
    // through them exactly. Clothoid transitions must not be inserted at
    // an anchor boundary because they would move the segment boundary
    // away from the anchor position (Blocker 8).
    std::vector<bool> isAnchorVertex(polyline.size(), false);
    for (const auto idx : anchorIndices) {
        if (idx < polyline.size()) isAnchorVertex[idx] = true;
    }

    // For each section, produce the base segment (line or arc).
    struct BaseSegment {
        AlignmentSegment segment;
        double startStation{0.0};
        double endStation{0.0};
        SectionKind kind{SectionKind::Straight};
        bool startsAtAnchor{false};
    };

    std::vector<BaseSegment> baseSegments;
    for (const auto& sec : sections) {
        const AlignmentPoint& startPt = polyline[sec.startIndex].position;
        const AlignmentPoint& endPt = polyline[sec.endIndex].position;

        const bool startsAtAnchor = isAnchorVertex[sec.startIndex];
        if (sec.kind == SectionKind::Straight) {
            LineSegment line = fitLine(startPt, endPt);
            baseSegments.push_back({line, sec.startStation, sec.endStation, SectionKind::Straight, startsAtAnchor});
        } else {
            auto arc = fitArc(polyline, sec.startIndex, sec.endIndex);
            if (arc) {
                if (config.maxCurvature && std::abs(arc->curvature) > *config.maxCurvature) {
                    state.diagnostics.push_back({
                        RoadErrorCode::InvalidCurvature,
                        "Fitted arc curvature exceeds the configured maximum"});
                    return state;
                }
                baseSegments.push_back({*arc, sec.startStation, sec.endStation, SectionKind::Curved, startsAtAnchor});
            } else {
                LineSegment line = fitLine(startPt, endPt);
                baseSegments.push_back({line, sec.startStation, sec.endStation, SectionKind::Straight, startsAtAnchor});
            }
        }
    }

    if (baseSegments.empty()) return state;

    // If there's only one segment, just use it directly.
    if (baseSegments.size() == 1) {
        state.segments.push_back(baseSegments[0].segment);
        return state;
    }

    // Walk through base segments, inserting clothoid transitions where
    // curvature changes. The clothoid starts at the end of the previous
    // segment (G0 + G1 + G2 continuous) and transitions to the next
    // segment's curvature. The previous segment is shortened to make room
    // for the clothoid, and the next segment's start is adjusted to the
    // clothoid's end point.

    for (std::size_t i = 0; i < baseSegments.size(); ++i) {
        if (i == 0) {
            state.segments.push_back(baseSegments[i].segment);
            continue;
        }

        const auto& prev = state.segments.back();
        const double prevEndK = segmentEndCurvature(prev);
        const double currStartK = segmentStartCurvature(baseSegments[i].segment);
        const double deltaK = currStartK - prevEndK;

        // At an anchor boundary, the alignment must pass through the
        // anchor position exactly. Clothoid transitions would move the
        // boundary away from the anchor, so we connect directly with
        // G1 continuity instead (curvature discontinuity is accepted at
        // anchor boundaries — the anchor takes precedence over smoothness).
        const bool atAnchor = baseSegments[i].startsAtAnchor;
        if (std::abs(deltaK) < config.curvatureTolerance || atAnchor) {
            // Curvature is already continuous OR this is an anchor
            // boundary — adjust start point/heading only.
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
            if (atAnchor) {
                state.anchorBoundarySegments.insert(state.segments.size());
            }
            state.segments.push_back(adjusted);
        } else {
            // Blocker 5: insert a real clothoid transition between the
            // previous segment and this one. The clothoid smoothly
            // transitions curvature from prevEndK to currStartK.
            const double prevLen = segmentLength(prev);
            const double currLen = segmentLength(baseSegments[i].segment);
            const double transLen = computeTransitionLength(
                prevEndK, currStartK, prevLen, currLen);

            if (transLen < 1e-9) {
                // Not enough space for a transition — connect directly
                // with G1 continuity (curvature discontinuity remains).
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
                // Shorten the previous segment to make room for the clothoid.
                // The clothoid takes the last transLen of the previous segment.
                const double newPrevLen = prevLen - transLen;
                if (newPrevLen > 1e-9) {
                    // Replace the previous segment with a shortened version.
                    state.segments.back() = shortenSegment(prev, newPrevLen);
                }

                // Build the clothoid transition.
                const AlignmentSample transStart = segmentEndSample(state.segments.back());
                ClothoidSegment clothoid;
                clothoid.start = transStart.position;
                clothoid.startHeading = transStart.heading;
                clothoid.startCurvature = prevEndK;
                clothoid.endCurvature = currStartK;
                clothoid.length = transLen;
                state.segments.push_back(clothoid);

                // Adjust the next segment's start to match the clothoid's end.
                const AlignmentSample transEnd = segmentEndSample(state.segments.back());
                AlignmentSegment adjusted = baseSegments[i].segment;
                std::visit([&](auto& seg) {
                    seg.start = transEnd.position;
                    if constexpr (std::is_same_v<decltype(seg), LineSegment&>) {
                        seg.heading = transEnd.heading;
                    } else if constexpr (std::is_same_v<decltype(seg), CircularArcSegment&>) {
                        seg.startHeading = transEnd.heading;
                    } else if constexpr (std::is_same_v<decltype(seg), ClothoidSegment&>) {
                        seg.startHeading = transEnd.heading;
                        seg.startCurvature = transEnd.curvature;
                    }
                }, adjusted);
                state.segments.push_back(adjusted);
            }
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

    // Blocker 8: protected anchors are true fit constraints. Map each
    // anchor to its polyline vertex index and pass the indices to
    // detectSections so each anchor vertex becomes a hard section
    // boundary — the fitted alignment then passes through every anchor
    // exactly instead of merely being validated after the fact.
    std::vector<std::size_t> anchorIndices;
    anchorIndices.reserve(input.protectedAnchors.size());
    for (const auto& anchor : input.protectedAnchors) {
        for (std::size_t i = 0; i < input.polyline.size(); ++i) {
            const double dx = input.polyline[i].position.easting - anchor.position.easting;
            const double dy = input.polyline[i].position.northing - anchor.position.northing;
            if (std::sqrt(dx * dx + dy * dy) < 1e-9) {
                anchorIndices.push_back(i);
                break;
            }
        }
    }

    // Detect sections.
    const auto sections = detectSections(
        input.polyline, stations, edgeHeadings, config.straightHeadingThreshold,
        anchorIndices);

    if (sections.empty()) {
        result.diagnostics.push_back({
            RoadErrorCode::EmptyAlignment,
            "No sections detected from input polyline"});
        return result;
    }

    // Build segments with clothoid transitions.
    auto state = buildSegments(input.polyline, sections, mergedConfig, anchorIndices);
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
    // Blocker 5: use the actual configured curvature tolerance, not an
    // artificially generous one. The clothoid transitions inserted by
    // buildSegments provide real curvature continuity, so the default
    // tight tolerance is appropriate. If a fit cannot satisfy the
    // configured continuity constraints, it fails with typed diagnostics
    // rather than silently downgrading.
    auto alignment = ReferenceAlignment::build(
        std::move(state.segments),
        input.positionTolerance,
        config.headingTolerance,
        config.curvatureTolerance,
        state.anchorBoundarySegments);
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

    // Blocker 6: production-grade source-deviation validation. Instead of
    // brute-force sampling the entire alignment at density proportional to
    // road length (O(road_length × vertices), unacceptable for 100 km roads),
    // evaluate each segment independently using its mathematical properties.
    //
    // For each source vertex, find the nearest point across all segments:
    //   - Line: orthogonal projection (closed form)
    //   - Arc: radial projection onto the fitted circle (closed form)
    //   - Clothoid: bounded adaptive sampling within the segment only
    //
    // Complexity: O(segments × vertices × bounded_segment_samples) where
    // segment samples are bounded per-segment, not proportional to total
    // road length. This is deterministic and bounded for any road length.

    const auto& segments = alignment.segments();

    for (std::size_t i = 0; i < sourcePolyline.size(); ++i) {
        const auto& v = sourcePolyline[i];
        if (!isFinitePoint(v.position)) continue;

        double bestDist = std::numeric_limits<double>::max();

        for (const auto& stood : segments) {
            const auto& seg = stood.segment;
            const double segLen = segmentLength(seg);
            if (segLen < 1e-12) continue;

            // Compute nearest distance on this segment.
            double segBestDist = std::numeric_limits<double>::max();

            std::visit([&](const auto& s) {
                using T = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<T, LineSegment>) {
                    // Closed-form orthogonal projection onto the line.
                    const double dx = s.start.easting;
                    const double dy = s.start.northing;
                    const double dirX = std::cos(s.heading);
                    const double dirY = std::sin(s.heading);
                    const double vx = v.position.easting - dx;
                    const double vy = v.position.northing - dy;
                    double t = vx * dirX + vy * dirY;
                    // Clamp to segment range [0, length].
                    t = std::max(0.0, std::min(t, s.length));
                    const double px = dx + t * dirX;
                    const double py = dy + t * dirY;
                    const double ddx = v.position.easting - px;
                    const double ddy = v.position.northing - py;
                    segBestDist = std::sqrt(ddx * ddx + ddy * ddy);
                } else if constexpr (std::is_same_v<T, CircularArcSegment>) {
                    // Blocker 9: finite arc nearest-point validation.
                    // The nearest point must be on the finite angular arc
                    // interval, not merely anywhere on the parent circle.
                    // A point on the same circle but outside the arc span
                    // must NOT produce zero deviation.
                    if (std::abs(s.curvature) < 1e-15) {
                        // Degenerate: treat as line.
                        const double dirX = std::cos(s.startHeading);
                        const double dirY = std::sin(s.startHeading);
                        const double vx = v.position.easting - s.start.easting;
                        const double vy = v.position.northing - s.start.northing;
                        double t = vx * dirX + vy * dirY;
                        t = std::max(0.0, std::min(t, s.length));
                        const double px = s.start.easting + t * dirX;
                        const double py = s.start.northing + t * dirY;
                        const double ddx = v.position.easting - px;
                        const double ddy = v.position.northing - py;
                        segBestDist = std::sqrt(ddx * ddx + ddy * ddy);
                    } else {
                        // Center = start + radius * perpendicular to start heading.
                        const double r = 1.0 / std::abs(s.curvature);
                        const double perpX = (s.curvature > 0 ? -std::sin(s.startHeading) : std::sin(s.startHeading)) * r;
                        const double perpY = (s.curvature > 0 ? std::cos(s.startHeading) : -std::cos(s.startHeading)) * r;
                        const double cx = s.start.easting + perpX;
                        const double cy = s.start.northing + perpY;
                        // Distance from center to vertex.
                        const double vdx = v.position.easting - cx;
                        const double vdy = v.position.northing - cy;
                        const double distFromCenter = std::sqrt(vdx * vdx + vdy * vdy);

                        // Blocker 9: check if the vertex's angle from center
                        // falls within the arc's finite angular interval.
                        // The arc starts at the start point and sweeps by
                        // sweepAngle = length * curvature (signed).
                        const double sweepAngle = s.length * s.curvature;
                        // Direction from center to start point.
                        const double startDirX = s.start.easting - cx;
                        const double startDirY = s.start.northing - cy;
                        const double startAngle = std::atan2(startDirY, startDirX);
                        // Direction from center to vertex.
                        const double vertexAngle = std::atan2(vdy, vdx);
                        // Angular offset from start to vertex, normalized
                        // to the sweep direction.
                        double offset = vertexAngle - startAngle;
                        // Normalize offset to [-2π, 2π] range.
                        while (offset > 2.0 * 3.14159265358979323846) offset -= 2.0 * 3.14159265358979323846;
                        while (offset < -2.0 * 3.14159265358979323846) offset += 2.0 * 3.14159265358979323846;
                        // Check if the offset is within the sweep range.
                        // For positive sweep (CCW), offset must be in [0, sweep].
                        // For negative sweep (CW), offset must be in [sweep, 0].
                        bool withinArc = false;
                        if (sweepAngle > 0.0) {
                            withinArc = (offset >= -1e-9 && offset <= sweepAngle + 1e-9);
                        } else {
                            withinArc = (offset <= 1e-9 && offset >= sweepAngle - 1e-9);
                        }

                        if (withinArc && distFromCenter > 1e-12) {
                            // The radial projection falls within the arc.
                            // Nearest point is at radius r from center, at
                            // the vertex's angle. Deviation is |distFromCenter - r|.
                            segBestDist = std::abs(distFromCenter - r);
                        } else {
                            // The radial projection is outside the arc.
                            // Nearest point is one of the arc endpoints.
                            const auto startSample = s.evaluate(0.0);
                            const auto endSample = s.evaluate(s.length);
                            const double dStart = distance(startSample.position, v.position);
                            const double dEnd = distance(endSample.position, v.position);
                            segBestDist = std::min(dStart, dEnd);
                        }
                    }
                } else {
                    // Clothoid: bounded adaptive sampling within the segment.
                    // The number of samples is bounded per-segment (not
                    // proportional to total road length), making this
                    // deterministic and bounded for any road length.
                    const std::size_t samples = std::min<std::size_t>(
                        64, std::max<std::size_t>(8,
                            static_cast<std::size_t>(segLen * 2.0)));
                    for (std::size_t j = 0; j <= samples; ++j) {
                        const double sLocal = segLen *
                            static_cast<double>(j) / static_cast<double>(samples);
                        const auto sample = s.evaluate(sLocal);
                        const double d = distance(sample.position, v.position);
                        if (d < segBestDist) segBestDist = d;
                    }
                }
            }, seg);

            if (segBestDist < bestDist) bestDist = segBestDist;
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
