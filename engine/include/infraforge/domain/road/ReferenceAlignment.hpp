#pragma once

#include "infraforge/domain/road/AlignmentPrimitives.hpp"
#include "infraforge/domain/road/RoadTypes.hpp"

#include <cstddef>
#include <expected>
#include <vector>

namespace infraforge::domain::road {

// One alignment segment plus the absolute station where it begins within the
// ReferenceAlignment's continuous stationing. The segment's local station
// range is [0, segmentLength()]; its absolute range is
// [startStation, startStation + segmentLength()].
struct StationedSegment {
    Station startStation{0.0};
    AlignmentSegment segment;

    [[nodiscard]] StationRange stationRange() const noexcept {
        return StationRange{startStation, startStation + segmentLength(segment)};
    }
};

// Default engineering tolerances for alignment continuity validation, in
// canonical project units. Callers may pass tighter/looser tolerances to
// build() when a domain policy requires it.
inline constexpr double kDefaultPositionTolerance = 1e-6;   // metres
inline constexpr double kDefaultHeadingTolerance = 1e-6;    // radians
inline constexpr double kDefaultCurvatureTolerance = 1e-9;  // 1/metres

// Canonical reference alignment: an ordered sequence of line/arc/clothoid
// segments with continuous stationing. Owns the road's horizontal mathematical
// geometry. Road truth lives here (and in the elevation/superelevation
// profiles), never in renderer meshes or frontend state.
//
// Stationing is continuous with no hidden gaps or overlaps: segment i ends at
// the station segment i+1 begins, and the first segment begins at station 0.
// Evaluation at segment boundaries is deterministic.
class ReferenceAlignment {
public:
    ReferenceAlignment() = default;

    // Builds a continuous alignment from self-contained segments. Each
    // segment is structurally validated, then G0 (position), G1 (heading),
    // and curvature continuity are checked between adjacent segments. The
    // builder never silently repairs invalid engineering geometry: on any
    // failure it returns the collected typed diagnostics instead of an
    // alignment.
    [[nodiscard]] static std::expected<ReferenceAlignment, std::vector<RoadDiagnostic>> build(
        std::vector<AlignmentSegment> segments,
        double positionTolerance = kDefaultPositionTolerance,
        double headingTolerance = kDefaultHeadingTolerance,
        double curvatureTolerance = kDefaultCurvatureTolerance);

    // Evaluates position, heading, and curvature at station s. Stations
    // outside [0, totalLength()] clamp to the nearest end deterministically
    // (no extrapolation). Allocation-free.
    [[nodiscard]] AlignmentSample evaluate(Station s) const noexcept;

    // Index of the segment whose station range contains s, or nullopt when s
    // is outside the alignment. For s exactly at an internal boundary the
    // earlier segment is returned.
    [[nodiscard]] std::optional<std::size_t> segmentIndexAt(Station s) const noexcept;

    [[nodiscard]] StationRange stationRange() const noexcept;
    [[nodiscard]] double totalLength() const noexcept { return totalLength_; }
    [[nodiscard]] bool isEmpty() const noexcept { return segments_.empty(); }
    [[nodiscard]] std::size_t segmentCount() const noexcept { return segments_.size(); }
    [[nodiscard]] const std::vector<StationedSegment>& segments() const noexcept { return segments_; }

private:
    ReferenceAlignment(std::vector<StationedSegment> segments, double totalLength) noexcept
        : segments_(std::move(segments)), totalLength_(totalLength) {}

    std::vector<StationedSegment> segments_;
    double totalLength_{0.0};
};

} // namespace infraforge::domain::road
