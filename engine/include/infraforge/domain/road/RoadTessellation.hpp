#pragma once

#include "infraforge/domain/road/RoadTypes.hpp"
#include "infraforge/domain/road/ReferenceAlignment.hpp"
#include "infraforge/domain/road/VerticalProfiles.hpp"
#include "infraforge/domain/road/RoadWidthProfile.hpp"

#include <cstdint>
#include <vector>

namespace infraforge::domain::road {

// A tessellated road mesh: a sampled ribbon derived from the canonical
// ReferenceAlignment and vertical profiles. This is derived data — the
// renderer consumes it, but road truth remains in the alignment/profiles.
//
// The tessellation is a centerline + cross-section ribbon:
//   - Cross-sections are sampled adaptively from a world-space error bound.
//   - Each cross-section has left/right edge offsets from the centerline.
//   - The mesh is a triangle strip between adjacent cross-sections.
//
// Future lane/cross-section topology (Issue #8) will extend this structure;
// for now the ribbon is a fixed-width road surface.

// One sampled cross-section of the tessellated road.
struct RoadCrossSection {
    Station station{0.0};
    // Centerline sample at this station.
    AlignmentPoint center{};
    Heading heading{0.0};
    Curvature curvature{0.0};
    double height{0.0};
    double crossSlope{0.0};
    double leftWidth{5.0};
    double rightWidth{5.0};
    // Left/right edge points (offset perpendicular to heading).
    AlignmentPoint leftEdge{};
    AlignmentPoint rightEdge{};
    double leftHeight{0.0};
    double rightHeight{0.0};
};

// Tessellation parameters. All values in canonical project units.
struct RoadTessellationParams {
    // Maximum station spacing along the alignment. Straight, flat portions
    // use this spacing; curved/banked/tapered portions refine further.
    double stationInterval{10.0};
    // Maximum permitted midpoint deviation between the evaluated road surface
    // and its tessellated chord, measured in canonical project units.
    double maximumSurfaceError{0.05};
    // Hard safety bound for pathological inputs.
    std::size_t maximumCrossSections{1'000'000};
    // Half-width of the road surface (distance from centerline to each edge).
    double halfWidth{5.0};
};

// A complete road tessellation: ordered cross-sections plus triangle indices.
struct RoadTessellation {
    std::vector<RoadCrossSection> crossSections;
    // Triangle indices into a vertex array derived from crossSections.
    // Each pair of adjacent cross-sections produces 2 triangles (6 indices).
    std::vector<std::uint32_t> indices;

    [[nodiscard]] bool isEmpty() const noexcept { return crossSections.empty(); }
    [[nodiscard]] std::size_t crossSectionCount() const noexcept { return crossSections.size(); }
    [[nodiscard]] std::size_t triangleCount() const noexcept { return indices.size() / 3; }
};

// Generates a road tessellation from a reference alignment and vertical
// profiles. Sampling includes every alignment segment and authored profile
// breakpoint, then recursively refines until the center and both surface
// edges satisfy maximumSurfaceError.
//
// stationInterval and maximumSurfaceError must be positive; halfWidth must be
// non-negative and maximumCrossSections must be at least two.
// Returns an empty tessellation for an empty alignment.
[[nodiscard]] RoadTessellation tessellateRoad(
    const ReferenceAlignment& alignment,
    const ElevationProfile& elevation,
    const SuperelevationProfile& superelevation,
    const RoadWidthProfile& width,
    const RoadTessellationParams& params = {});

// Compatibility overload for callers that intentionally use the canonical
// default 5 m side widths.
[[nodiscard]] inline RoadTessellation tessellateRoad(
    const ReferenceAlignment& alignment,
    const ElevationProfile& elevation,
    const SuperelevationProfile& superelevation,
    const RoadTessellationParams& params = {}) {
    return tessellateRoad(alignment, elevation, superelevation,
        RoadWidthProfile{{RoadWidthBreakpoint{.station = 0.0,
            .leftWidth = params.halfWidth, .rightWidth = params.halfWidth}}}, params);
}

} // namespace infraforge::domain::road
