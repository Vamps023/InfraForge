#pragma once

#include "infraforge/domain/road/LaneTypes.hpp"
#include "infraforge/domain/road/ReferenceAlignment.hpp"
#include "infraforge/domain/road/RoadSource.hpp"
#include "infraforge/domain/road/RoadTypes.hpp"
#include "infraforge/domain/road/RoadWidthProfile.hpp"
#include "infraforge/domain/road/VerticalProfiles.hpp"

#include <expected>
#include <string>
#include <utility>
#include <vector>

namespace infraforge::domain::road {

// A canonical road: stable identity, horizontal reference alignment,
// vertical profiles, and (for imported roads) source geometry/provenance.
// Road truth lives here in canonical project coordinates, independent of
// renderer meshes and frontend state (ADR-0006, ADR-0009).
//
// A Road is constructed via the build() factory, which validates the
// alignment and profiles. The Road owns its alignment and profiles by
// value; source geometry is optional and stored separately.
class Road {
public:
    // Builds a canonical road from its constituent parts. Validates the
    // alignment and profiles; returns typed diagnostics on failure. The
    // source geometry is optional (absent for authored roads).
    struct BuildInput {
        RoadId id{};
        std::string displayName;
        ReferenceAlignment alignment{};
        ElevationProfile elevation{};
        SuperelevationProfile superelevation{};
        RoadWidthProfile width{};
        std::vector<RoadLaneSection> laneSections{};
        RoadSource source{};
    };

    [[nodiscard]] static std::expected<Road, std::vector<RoadDiagnostic>> build(BuildInput input);

    [[nodiscard]] const RoadId& id() const noexcept { return id_; }
    [[nodiscard]] const std::string& displayName() const noexcept { return displayName_; }
    [[nodiscard]] const ReferenceAlignment& alignment() const noexcept { return alignment_; }
    [[nodiscard]] const ElevationProfile& elevation() const noexcept { return elevation_; }
    [[nodiscard]] const SuperelevationProfile& superelevation() const noexcept { return superelevation_; }
    [[nodiscard]] const RoadWidthProfile& width() const noexcept { return width_; }
    [[nodiscard]] const std::vector<RoadLaneSection>& laneSections() const noexcept { return laneSections_; }
    [[nodiscard]] const RoadSource& source() const noexcept { return source_; }

    // Evaluates the full 3D road sample at station s: horizontal position
    // from the alignment, height from the elevation profile, cross-slope
    // from the superelevation profile. Allocation-free.
    struct RoadSample {
        AlignmentPoint position{};
        Heading heading{0.0};
        Curvature curvature{0.0};
        double height{0.0};
        double crossSlope{0.0};
    };
    [[nodiscard]] RoadSample evaluate(Station s) const noexcept;

private:
    Road(BuildInput input) noexcept
        : id_(input.id),
          displayName_(std::move(input.displayName)),
          alignment_(std::move(input.alignment)),
          elevation_(std::move(input.elevation)),
          superelevation_(std::move(input.superelevation)),
          width_(std::move(input.width)),
          laneSections_(std::move(input.laneSections)),
          source_(std::move(input.source)) {}

    RoadId id_{};
    std::string displayName_;
    ReferenceAlignment alignment_;
    ElevationProfile elevation_;
    SuperelevationProfile superelevation_;
    RoadWidthProfile width_;
    std::vector<RoadLaneSection> laneSections_;
    RoadSource source_;
};

// Validates that protected anchors lie within the alignment station range
// and match the alignment position at their station (within tolerance).
// Returns diagnostics for any anchor that is out of range or displaced.
[[nodiscard]] std::vector<RoadDiagnostic> validateProtectedAnchors(
    const ReferenceAlignment& alignment,
    const std::vector<ProtectedAnchor>& anchors,
    double positionTolerance = kDefaultPositionTolerance) noexcept;

} // namespace infraforge::domain::road
