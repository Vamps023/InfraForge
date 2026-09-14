#pragma once

#include "infraforge/domain/road/RoadTypes.hpp"

#include <cstddef>
#include <expected>
#include <vector>

namespace infraforge::domain::road {

// Vertical and cross-slope geometry of a road, evaluated by station. Both
// profiles are independent of terrain: a road is valid in an empty project
// with no terrain datasets. Terrain may later provide elevation snapping,
// but the Road domain itself owns canonical vertical geometry
// (docs/05_DOMAINS/LINEAR_INFRASTRUCTURE_GEOMETRY.md).
//
// The representation is piecewise linear over a sorted sequence of (station,
// value) breakpoints. This is the simplest representation compatible with
// future OpenDRIVE road profile/superelevation mapping (OpenDRIVE uses
// piecewise linear <elevation>/<superelevation> records). Future work may
// extend to parabolic transitions without changing the evaluation contract.

// One breakpoint in a piecewise-linear profile.
struct ProfileBreakpoint {
    Station station{0.0};
    double value{0.0};

    friend bool operator==(const ProfileBreakpoint&, const ProfileBreakpoint&) = default;
};

// A piecewise-linear profile over station. Between two adjacent breakpoints
// the value is linearly interpolated; before the first breakpoint the first
// value is held constant; after the last breakpoint the last value is held
// constant. An empty profile evaluates to zero everywhere (no fabricated
// values).
class PiecewiseLinearProfile {
public:
    PiecewiseLinearProfile() = default;
    explicit PiecewiseLinearProfile(std::vector<ProfileBreakpoint> breakpoints)
        : breakpoints_(std::move(breakpoints)) {}

    // Validates that breakpoints are sorted by station, finite, and
    // non-degenerate (no duplicate stations). Returns nullopt when valid.
    [[nodiscard]] std::optional<RoadDiagnostic> validate() const noexcept;

    // Evaluates the profile at station s. Empty profile -> 0.0. Before the
    // first breakpoint -> first value; after the last -> last value; between
    // adjacent breakpoints -> linear interpolation. Allocation-free.
    [[nodiscard]] double evaluate(Station s) const noexcept;

    [[nodiscard]] bool isEmpty() const noexcept { return breakpoints_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return breakpoints_.size(); }
    [[nodiscard]] const std::vector<ProfileBreakpoint>& breakpoints() const noexcept { return breakpoints_; }

private:
    std::vector<ProfileBreakpoint> breakpoints_;
};

// Canonical road elevation profile: height (in canonical project units)
// as a piecewise-linear function of station. Independent of terrain.
using ElevationProfile = PiecewiseLinearProfile;

// Canonical road superelevation profile: cross-slope angle (radians) as a
// piecewise-linear function of station. Positive = right side of road is
// lower (banking into a left turn). Compatible with future OpenDRIVE
// <superelevation> records.
using SuperelevationProfile = PiecewiseLinearProfile;

// Builds a validated elevation profile. Returns the profile on success or
// a typed diagnostic on failure.
[[nodiscard]] std::expected<ElevationProfile, RoadDiagnostic> buildElevationProfile(
    std::vector<ProfileBreakpoint> breakpoints);

// Builds a validated superelevation profile.
[[nodiscard]] std::expected<SuperelevationProfile, RoadDiagnostic> buildSuperelevationProfile(
    std::vector<ProfileBreakpoint> breakpoints);

} // namespace infraforge::domain::road
