#include "infraforge/domain/road/VerticalProfiles.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace infraforge::domain::road {

std::optional<RoadDiagnostic> PiecewiseLinearProfile::validate() const noexcept {
    if (breakpoints_.empty()) {
        return std::nullopt; // empty profile is valid (evaluates to 0)
    }
    for (const auto& bp : breakpoints_) {
        if (!std::isfinite(bp.station) || !std::isfinite(bp.value)) {
            return RoadDiagnostic{RoadErrorCode::NonFiniteParameter,
                "profile breakpoint is not finite"};
        }
    }
    for (std::size_t i = 1; i < breakpoints_.size(); ++i) {
        if (breakpoints_[i].station <= breakpoints_[i - 1].station) {
            return RoadDiagnostic{RoadErrorCode::InvalidProfile,
                "profile breakpoints must be strictly increasing in station"};
        }
    }
    return std::nullopt;
}

double PiecewiseLinearProfile::evaluate(const Station s) const noexcept {
    if (breakpoints_.empty()) {
        return 0.0;
    }
    // Non-finite station policy: NaN returns 0.0 (the empty-profile default)
    // rather than reaching std::upper_bound with an invalid ordering.
    // +inf clamps to the last breakpoint value; -inf to the first.
    if (!std::isfinite(s)) {
        if (s == std::numeric_limits<double>::infinity()) {
            return breakpoints_.back().value;
        }
        if (s == -std::numeric_limits<double>::infinity()) {
            return breakpoints_.front().value;
        }
        return 0.0;
    }
    if (s <= breakpoints_.front().station) {
        return breakpoints_.front().value;
    }
    if (s >= breakpoints_.back().station) {
        return breakpoints_.back().value;
    }
    // Binary search for the interval containing s.
    auto it = std::upper_bound(breakpoints_.begin(), breakpoints_.end(), s,
        [](const Station value, const ProfileBreakpoint& bp) { return value < bp.station; });
    // it points to the first breakpoint with station > s; interpolate [it-1, it).
    const auto& hi = *it;
    const auto& lo = *(it - 1);
    const double span = hi.station - lo.station;
    if (span <= 0.0) {
        return lo.value;
    }
    const double t = (s - lo.station) / span;
    return lo.value + t * (hi.value - lo.value);
}

std::expected<ElevationProfile, RoadDiagnostic> buildElevationProfile(
    std::vector<ProfileBreakpoint> breakpoints) {
    ElevationProfile profile(std::move(breakpoints));
    if (auto d = profile.validate()) {
        return std::unexpected(*d);
    }
    return profile;
}

std::expected<SuperelevationProfile, RoadDiagnostic> buildSuperelevationProfile(
    std::vector<ProfileBreakpoint> breakpoints) {
    SuperelevationProfile profile(std::move(breakpoints));
    if (auto d = profile.validate()) {
        return std::unexpected(*d);
    }
    return profile;
}

} // namespace infraforge::domain::road
