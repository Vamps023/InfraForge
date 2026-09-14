#pragma once

#include "infraforge/domain/road/RoadTypes.hpp"
#include "infraforge/domain/road/ReferenceAlignment.hpp"
#include "infraforge/domain/road/VerticalProfiles.hpp"

#include <cstdint>
#include <vector>

namespace infraforge::domain::road {

// A tessellated road mesh: a sampled ribbon derived from the canonical
// ReferenceAlignment and vertical profiles. This is derived data — the
// renderer consumes it, but road truth remains in the alignment/profiles.
//
// The tessellation is a simple centerline + cross-section ribbon:
//   - Cross-sections are sampled at regular station intervals.
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
    // Left/right edge points (offset perpendicular to heading).
    AlignmentPoint leftEdge{};
    AlignmentPoint rightEdge{};
    double leftHeight{0.0};
    double rightHeight{0.0};
};

// Tessellation parameters. All values in canonical project units.
struct RoadTessellationParams {
    // Station sampling interval along the alignment.
    double stationInterval{10.0};
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
// profiles. The alignment is sampled at regular station intervals; each
// sample produces a cross-section with left/right edges offset perpendicular
// to the tangent heading. Heights come from the elevation profile; cross-
// slopes from the superelevation profile.
//
// The stationInterval must be positive; halfWidth must be non-negative.
// Returns an empty tessellation for an empty alignment.
[[nodiscard]] RoadTessellation tessellateRoad(
    const ReferenceAlignment& alignment,
    const ElevationProfile& elevation,
    const SuperelevationProfile& superelevation,
    const RoadTessellationParams& params = {});

} // namespace infraforge::domain::road
