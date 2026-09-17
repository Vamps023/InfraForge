#include "infraforge/domain/road/RoadWidthProfile.hpp"

#include <algorithm>
#include <cmath>

namespace infraforge::domain::road {

std::optional<RoadDiagnostic> RoadWidthProfile::validate() const noexcept {
    for (std::size_t i = 0; i < breakpoints_.size(); ++i) {
        const auto& point = breakpoints_[i];
        if (!std::isfinite(point.station) || !std::isfinite(point.leftWidth)
            || !std::isfinite(point.rightWidth)) {
            return RoadDiagnostic{RoadErrorCode::NonFiniteParameter,
                "road width breakpoint values must be finite"};
        }
        if (point.station < 0.0 || point.leftWidth < 0.0 || point.rightWidth < 0.0) {
            return RoadDiagnostic{RoadErrorCode::InvalidProfile,
                "road width station and side widths must be non-negative"};
        }
        if (i > 0 && point.station <= breakpoints_[i - 1].station) {
            return RoadDiagnostic{RoadErrorCode::InvalidProfile,
                "road width breakpoint stations must be strictly increasing"};
        }
    }
    return std::nullopt;
}

RoadSurfaceWidth RoadWidthProfile::evaluate(const Station station) const noexcept {
    if (breakpoints_.empty()) return {};
    if (station <= breakpoints_.front().station) {
        return {breakpoints_.front().leftWidth, breakpoints_.front().rightWidth};
    }
    if (station >= breakpoints_.back().station) {
        return {breakpoints_.back().leftWidth, breakpoints_.back().rightWidth};
    }
    const auto upper = std::upper_bound(breakpoints_.begin(), breakpoints_.end(), station,
        [](const Station value, const RoadWidthBreakpoint& point) { return value < point.station; });
    const auto& high = *upper;
    const auto& low = *(upper - 1);
    const double t = (station - low.station) / (high.station - low.station);
    return {
        low.leftWidth + t * (high.leftWidth - low.leftWidth),
        low.rightWidth + t * (high.rightWidth - low.rightWidth),
    };
}

std::expected<RoadWidthProfile, RoadDiagnostic> buildRoadWidthProfile(
    std::vector<RoadWidthBreakpoint> breakpoints) {
    RoadWidthProfile profile{std::move(breakpoints)};
    if (const auto diagnostic = profile.validate()) return std::unexpected(*diagnostic);
    return profile;
}

} // namespace infraforge::domain::road
