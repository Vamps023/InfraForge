#pragma once

#include "infraforge/domain/road/RoadTypes.hpp"

#include <expected>
#include <optional>
#include <utility>
#include <vector>

namespace infraforge::domain::road {

// Station-aware canonical road surface width, adapted from the donor's
// RoadProfile/LaneSection widthAt concept. This models the whole road surface,
// not lanes: left/right widths interpolate independently and remain engine
// owned. An empty profile preserves the historical 5 m + 5 m ribbon.
struct RoadWidthBreakpoint {
    Station station{0.0};
    double leftWidth{5.0};
    double rightWidth{5.0};

    friend bool operator==(const RoadWidthBreakpoint&, const RoadWidthBreakpoint&) = default;
};

struct RoadSurfaceWidth {
    double left{5.0};
    double right{5.0};
};

class RoadWidthProfile {
public:
    RoadWidthProfile() = default;
    explicit RoadWidthProfile(std::vector<RoadWidthBreakpoint> breakpoints)
        : breakpoints_(std::move(breakpoints)) {}

    [[nodiscard]] std::optional<RoadDiagnostic> validate() const noexcept;
    [[nodiscard]] RoadSurfaceWidth evaluate(Station station) const noexcept;
    [[nodiscard]] const std::vector<RoadWidthBreakpoint>& breakpoints() const noexcept {
        return breakpoints_;
    }

private:
    std::vector<RoadWidthBreakpoint> breakpoints_;
};

[[nodiscard]] std::expected<RoadWidthProfile, RoadDiagnostic> buildRoadWidthProfile(
    std::vector<RoadWidthBreakpoint> breakpoints);

} // namespace infraforge::domain::road
