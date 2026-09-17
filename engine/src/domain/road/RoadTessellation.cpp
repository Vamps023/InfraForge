#include "infraforge/domain/road/RoadTessellation.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace infraforge::domain::road {

namespace {

// Computes the perpendicular offset point from a centerline position.
AlignmentPoint offsetPerpendicular(const AlignmentPoint& center, Heading heading, double offset) noexcept {
    // Perpendicular to heading: rotate heading by +90 degrees for left, -90 for right.
    // Heading is counterclockwise from +easting. Perpendicular left = heading + pi/2.
    const double perpX = std::cos(heading + std::numbers::pi / 2.0);
    const double perpY = std::sin(heading + std::numbers::pi / 2.0);
    return AlignmentPoint{center.easting + perpX * offset, center.northing + perpY * offset};
}

} // namespace

RoadTessellation tessellateRoad(
    const ReferenceAlignment& alignment,
    const ElevationProfile& elevation,
    const SuperelevationProfile& superelevation,
    const RoadWidthProfile& width,
    const RoadTessellationParams& params) {

    RoadTessellation tess;

    if (alignment.isEmpty()) {
        return tess;
    }

    const double totalLength = alignment.totalLength();
    if (totalLength <= 0.0 || !std::isfinite(totalLength)) {
        return tess;
    }

    // Blocker 13: harden tessellation inputs. Reject non-finite and
    // negative parameters rather than silently clamping them — a NaN
    // interval or negative width would produce undefined geometry.
    if (!std::isfinite(params.stationInterval) || params.stationInterval <= 0.0) {
        return tess;
    }
    if (!std::isfinite(params.halfWidth) || params.halfWidth < 0.0) {
        return tess;
    }

    // Clamp station interval to a safe minimum and ensure at least 2 samples.
    const double interval = std::max(params.stationInterval, 1e-3);

    // Sample the alignment at regular station intervals.
    // Always include station 0 and the final station.
    std::vector<Station> stations;
    const std::size_t approxCount = static_cast<std::size_t>(std::ceil(totalLength / interval)) + 1;
    stations.reserve(approxCount);
    for (double s = 0.0; s < totalLength; s += interval) {
        stations.push_back(s);
    }
    // Ensure the final station is included exactly.
    if (stations.empty() || stations.back() < totalLength) {
        stations.push_back(totalLength);
    }

    // Build cross-sections.
    tess.crossSections.reserve(stations.size());
    for (const Station s : stations) {
        RoadCrossSection cs;
        cs.station = s;

        const auto horiz = alignment.evaluate(s);
        cs.center = horiz.position;
        cs.heading = horiz.heading;
        cs.curvature = horiz.curvature;

        // Vertical profiles.
        cs.height = elevation.evaluate(s);
        cs.crossSlope = superelevation.evaluate(s);
        const RoadSurfaceWidth surfaceWidth = width.evaluate(s);
        cs.leftWidth = surfaceWidth.left;
        cs.rightWidth = surfaceWidth.right;

        // Compute left/right edges perpendicular to the heading.
        // The cross-slope tilts the road surface: left edge rises when
        // crossSlope > 0 (positive superelevation banks to the left).
        cs.leftEdge = offsetPerpendicular(cs.center, cs.heading, cs.leftWidth);
        cs.rightEdge = offsetPerpendicular(cs.center, cs.heading, -cs.rightWidth);
        cs.leftHeight = cs.height + std::tan(cs.crossSlope) * cs.leftWidth;
        cs.rightHeight = cs.height - std::tan(cs.crossSlope) * cs.rightWidth;

        tess.crossSections.push_back(cs);
    }

    // Build triangle indices. Each pair of adjacent cross-sections produces
    // a quad (2 triangles). Vertex layout: for cross-section i, left = 2*i,
    // right = 2*i + 1.
    const std::size_t n = tess.crossSections.size();
    if (n < 2) {
        return tess;
    }
    tess.indices.reserve((n - 1) * 6);
    for (std::uint32_t i = 0; i + 1 < static_cast<std::uint32_t>(n); ++i) {
        const std::uint32_t i0 = 2 * i;       // left i
        const std::uint32_t i1 = 2 * i + 1;   // right i
        const std::uint32_t i2 = 2 * (i + 1);       // left i+1
        const std::uint32_t i3 = 2 * (i + 1) + 1;   // right i+1
        // Triangle 1: left i, right i, left i+1
        tess.indices.push_back(i0);
        tess.indices.push_back(i1);
        tess.indices.push_back(i2);
        // Triangle 2: right i, right i+1, left i+1
        tess.indices.push_back(i1);
        tess.indices.push_back(i3);
        tess.indices.push_back(i2);
    }

    return tess;
}

} // namespace infraforge::domain::road
