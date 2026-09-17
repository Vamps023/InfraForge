#include "infraforge/domain/road/RoadTessellation.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>

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

RoadCrossSection evaluateCrossSection(
    const ReferenceAlignment& alignment,
    const ElevationProfile& elevation,
    const SuperelevationProfile& superelevation,
    const RoadWidthProfile& width,
    const Station station) noexcept {
    const auto sample = alignment.evaluate(station);
    RoadCrossSection section;
    section.station = station;
    section.center = sample.position;
    section.heading = sample.heading;
    section.curvature = sample.curvature;
    section.height = elevation.evaluate(station);
    section.crossSlope = superelevation.evaluate(station);
    const auto surfaceWidth = width.evaluate(station);
    section.leftWidth = surfaceWidth.left;
    section.rightWidth = surfaceWidth.right;
    section.leftEdge = offsetPerpendicular(section.center, section.heading, section.leftWidth);
    section.rightEdge = offsetPerpendicular(section.center, section.heading, -section.rightWidth);
    section.leftHeight = section.height + std::tan(section.crossSlope) * section.leftWidth;
    section.rightHeight = section.height - std::tan(section.crossSlope) * section.rightWidth;
    return section;
}

double midpointDeviation(
    const AlignmentPoint& start, const double startHeight,
    const AlignmentPoint& midpoint, const double midpointHeight,
    const AlignmentPoint& end, const double endHeight) noexcept {
    return std::hypot(midpoint.easting - (start.easting + end.easting) * 0.5,
        midpoint.northing - (start.northing + end.northing) * 0.5,
        midpointHeight - (startHeight + endHeight) * 0.5);
}

double surfaceDeviation(
    const RoadCrossSection& start,
    const RoadCrossSection& midpoint,
    const RoadCrossSection& end) noexcept {
    return std::max({
        midpointDeviation(start.center, start.height, midpoint.center, midpoint.height,
            end.center, end.height),
        midpointDeviation(start.leftEdge, start.leftHeight, midpoint.leftEdge, midpoint.leftHeight,
            end.leftEdge, end.leftHeight),
        midpointDeviation(start.rightEdge, start.rightHeight, midpoint.rightEdge, midpoint.rightHeight,
            end.rightEdge, end.rightHeight),
    });
}

template <typename Breakpoint>
void appendBreakpointStations(std::vector<Station>& stations,
    const std::vector<Breakpoint>& breakpoints, const double totalLength) {
    for (const auto& breakpoint : breakpoints) {
        if (breakpoint.station > 0.0 && breakpoint.station < totalLength) {
            stations.push_back(breakpoint.station);
        }
    }
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

    // Reject malformed controls rather than silently clamping them.
    if (!std::isfinite(params.stationInterval) || params.stationInterval <= 0.0) {
        return tess;
    }
    if (!std::isfinite(params.maximumSurfaceError) || params.maximumSurfaceError <= 0.0
        || params.maximumCrossSections < 2) {
        return tess;
    }
    if (!std::isfinite(params.halfWidth) || params.halfWidth < 0.0) {
        return tess;
    }
    if (elevation.validate().has_value() || superelevation.validate().has_value()
        || width.validate().has_value()) return tess;

    // Preserve all canonical boundaries so refinement never bridges an
    // alignment primitive or an authored profile/taper breakpoint.
    std::vector<Station> stations{0.0, totalLength};
    for (const auto& stationed : alignment.segments()) {
        const double boundary = stationed.stationRange().end;
        if (boundary > 0.0 && boundary < totalLength) stations.push_back(boundary);
    }
    appendBreakpointStations(stations, elevation.breakpoints(), totalLength);
    appendBreakpointStations(stations, superelevation.breakpoints(), totalLength);
    appendBreakpointStations(stations, width.breakpoints(), totalLength);
    std::sort(stations.begin(), stations.end());
    stations.erase(std::unique(stations.begin(), stations.end()), stations.end());

    std::vector<Station> seededStations;
    seededStations.reserve(stations.size());
    seededStations.push_back(stations.front());
    for (std::size_t index = 0; index + 1 < stations.size(); ++index) {
        const double start = stations[index];
        const double span = stations[index + 1] - start;
        const double piecesRequired = std::ceil(span / params.stationInterval);
        const std::size_t remaining = params.maximumCrossSections - seededStations.size();
        if (!std::isfinite(piecesRequired) || piecesRequired < 1.0
            || piecesRequired > static_cast<double>(remaining)) return tess;
        const std::size_t pieces = static_cast<std::size_t>(piecesRequired);
        for (std::size_t piece = 1; piece <= pieces; ++piece) {
            seededStations.push_back(start + span * static_cast<double>(piece) / static_cast<double>(pieces));
        }
    }
    stations = std::move(seededStations);

    struct Interval { RoadCrossSection start; RoadCrossSection end; };
    tess.crossSections.reserve(std::min(params.maximumCrossSections, stations.size() * 2));
    tess.crossSections.push_back(evaluateCrossSection(
        alignment, elevation, superelevation, width, stations.front()));

    for (std::size_t index = 0; index + 1 < stations.size(); ++index) {
        std::vector<Interval> pending;
        pending.push_back({
            evaluateCrossSection(alignment, elevation, superelevation, width, stations[index]),
            evaluateCrossSection(alignment, elevation, superelevation, width, stations[index + 1]),
        });
        while (!pending.empty()) {
            Interval interval = std::move(pending.back());
            pending.pop_back();
            const double midpointStation = std::midpoint(interval.start.station, interval.end.station);
            const auto midpoint = evaluateCrossSection(
                alignment, elevation, superelevation, width, midpointStation);
            const bool requiresRefinement = surfaceDeviation(
                interval.start, midpoint, interval.end) > params.maximumSurfaceError;
            if (requiresRefinement && midpointStation > interval.start.station
                && midpointStation < interval.end.station) {
                if (tess.crossSections.size() + pending.size() + 2 > params.maximumCrossSections) {
                    return {};
                }
                // LIFO: push right first so output remains station ordered.
                pending.push_back({midpoint, interval.end});
                pending.push_back({interval.start, midpoint});
            } else {
                tess.crossSections.push_back(interval.end);
                if (tess.crossSections.size() > params.maximumCrossSections) return {};
            }
        }
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
