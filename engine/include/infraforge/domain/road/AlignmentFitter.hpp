#pragma once

#include "infraforge/domain/road/RoadSource.hpp"
#include "infraforge/domain/road/ReferenceAlignment.hpp"
#include "infraforge/domain/road/RoadTypes.hpp"

#include <vector>

namespace infraforge::domain::road {

// Deterministic alignment fitter: converts a conditioned source polyline
// (already transformed into canonical project coordinates) plus protected
// anchors into a canonical ReferenceAlignment composed of Line, CircularArc,
// and Clothoid segments.
//
// The fitter is domain-neutral so Road and future Rail/importers can reuse
// the mathematical fitting services. It operates entirely in canonical
// project coordinates — never in WGS84 degrees.
//
// Properties (docs/05_DOMAINS/LINEAR_INFRASTRUCTURE_GEOMETRY.md):
//   - Deterministic: identical inputs + constraints + configuration produce
//     identical segment parameters. No randomness, no platform-dependent
//     heuristic instability.
//   - Topology safe: protected anchors remain exact.
//   - Canonical-unit operation: all fitting in project coordinates.
//   - Does not invent engineering data: no design speed, minimum radius,
//     maximum curvature, transition length, or superelevation. Absent
//     constraints remain absent (std::optional).
//   - Source deviation: verifies the fitted alignment stays within the
//     configured source-deviation tolerance. Returns a typed diagnostic
//     if impossible — never silently exceeds the tolerance.
//
// Segment support:
//   - Line
//   - Line -> Arc
//   - Arc -> Line
//   - Line -> Clothoid -> Arc
//   - Arc -> Clothoid -> Line
//   - Clothoid -> Arc -> Clothoid
//   - Multi-segment S curves
//
// The fitter never uses a polyline with many tiny segments as a shortcut.
// Polylines and tessellated vertices are never canonical alignment truth.

// Configuration for the fitter. All values in canonical project units.
struct AlignmentFitConfig {
    // Tolerance for position deviation between the fitted alignment and the
    // source polyline. The fitter rejects a fit that exceeds this.
    double positionTolerance{kDefaultPositionTolerance};
    // Heading tolerance for continuity validation (radians).
    double headingTolerance{kDefaultHeadingTolerance};
    // Curvature tolerance for continuity validation (1/canonical units).
    double curvatureTolerance{kDefaultCurvatureTolerance};
    // Optional maximum curvature constraint (1/radius). When absent, the
    // fitter does not invent a minimum radius or design speed.
    std::optional<double> maxCurvature;
    // Angular threshold (radians) for detecting heading changes that indicate
    // a curve vs. a straight section. Consecutive segments with heading
    // difference below this are treated as collinear.
    double straightHeadingThreshold{1e-4};
    // Minimum number of source points in a curved section to attempt an arc
    // fit. Fewer points are treated as a transition or line.
    std::size_t minArcPoints{3};
};

// Fits a conditioned source polyline into a canonical ReferenceAlignment.
// Returns the fitted alignment on success, or typed diagnostics on failure.
// The fitter never silently repairs invalid geometry or moves a protected
// anchor.
[[nodiscard]] AlignmentFitResult fitAlignment(
    const AlignmentFitInput& input,
    const AlignmentFitConfig& config = {}) noexcept;

// Validates that the fitted alignment stays within the configured source-
// deviation tolerance of the original polyline. Returns diagnostics for
// any source vertex whose distance from the alignment exceeds the tolerance.
[[nodiscard]] std::vector<RoadDiagnostic> validateSourceDeviation(
    const ReferenceAlignment& alignment,
    const std::vector<ConditionedVertex>& sourcePolyline,
    double positionTolerance) noexcept;

} // namespace infraforge::domain::road
