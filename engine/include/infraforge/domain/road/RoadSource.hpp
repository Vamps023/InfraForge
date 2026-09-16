#pragma once

#include "infraforge/domain/road/ReferenceAlignment.hpp"
#include "infraforge/domain/road/RoadTypes.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace infraforge::domain::road {

// Source geometry and provenance, kept architecturally separate from the
// canonical ReferenceAlignment (docs/05_DOMAINS/
// LINEAR_INFRASTRUCTURE_GEOMETRY.md). Imported roads retain their original
// linework, source IDs, and provider metadata so the canonical alignment
// can be re-derived or audited. Authored roads carry no source geometry.
//
// The pipeline is:
//   SourcePolyline (in source coordinates)
//     -> Geo transform into project coordinates
//     -> Protected-anchor extraction
//     -> Line/Arc/Clothoid fit
//     -> Validation
//     -> Canonical ReferenceAlignment
//
// Source geometry is NEVER the canonical road geometry: OSM nodes are not
// the alignment, and GPU smoothing is not engineering geometry.

// One vertex of the original source linework, in source coordinates (e.g.
// WGS84 lon/lat for OSM, or OpenDRIVE s/t). Source coordinates are stored
// verbatim and never used for engineering calculations; the Geo domain
// transforms them into canonical project coordinates before fitting.
struct SourceVertex {
    double x{0.0};
    double y{0.0};
    // Optional source-elevation, if the source provides it. Absent
    // (nullopt) when the source has no elevation data; never represented
    // as NaN so it round-trips safely through SQLite NULL.
    std::optional<double> z;

    friend bool operator==(const SourceVertex&, const SourceVertex&) = default;
};

// One source identifier/tag pair (e.g. OSM way id, OpenDRIVE road id). Tags
// are retained verbatim for provenance and audit.
struct SourceTag {
    std::string key;
    std::string value;

    friend bool operator==(const SourceTag&, const SourceTag&) = default;
};

// Provenance of an imported road: who provided it, when, and with what
// source identifiers/tags. Provenance is reference data, not derived; it
// survives save/reopen unchanged.
struct RoadProvenance {
    SourceProvider provider{SourceProvider::Authored};
    // Stable source identifier (e.g. OSM way id as a string).
    std::string sourceId;
    // Free-form source tags retained verbatim.
    std::vector<SourceTag> tags;
    // ISO-8601 timestamp of the import, or empty if authored.
    std::string importedAt;

    friend bool operator==(const RoadProvenance&, const RoadProvenance&) = default;
};

// Original source linework for an imported road, in source coordinates.
// Empty for authored roads. The source polyline is retained so the
// canonical alignment can be re-derived or audited; it is never used as
// canonical geometry itself.
struct SourcePolyline {
    // Vertices in source coordinates (e.g. WGS84 lon/lat for OSM).
    std::vector<SourceVertex> vertices;
    // Optional CRS/coordinate reference identifier (e.g. "EPSG:4326").
    std::string sourceCrs;

    friend bool operator==(const SourcePolyline&, const SourcePolyline&) = default;
};

// Complete source geometry + provenance for a road. Stored alongside but
// separate from the canonical ReferenceAlignment. An authored road has an
// empty SourcePolyline and provider == Authored.
struct RoadSource {
    SourcePolyline geometry;
    RoadProvenance provenance;
    // Protected anchors that fitting/smoothing must not move. These are
    // reference data (junction locations, endpoints, user-pinned points)
    // consulted before any fitting modifies the alignment.
    std::vector<ProtectedAnchor> protectedAnchors;

    friend bool operator==(const RoadSource&, const RoadSource&) = default;
};

// ---- Alignment fitter interface ----
//
// The fitter converts a conditioned source polyline (already transformed
// into canonical project coordinates) plus protected anchors into a
// canonical ReferenceAlignment. The full OSM/OpenDRIVE importer is out of
// scope for this foundation (issues #9/#15); this interface establishes
// the contract the future shared fitter will implement so the road
// mathematical kernel and the import pipeline stay decoupled.
//
// A conditioned polyline vertex is a source vertex already transformed
// into canonical project-global coordinates.
struct ConditionedVertex {
    AlignmentPoint position{};
    // Optional station hint from the source (e.g. OpenDRIVE s), or NaN.
    double sourceStation{std::numeric_limits<double>::quiet_NaN()};

    friend bool operator==(const ConditionedVertex&, const ConditionedVertex&) = default;
};

// Input to the alignment fitter: conditioned polyline in project coordinates
// plus the protected anchors that must be preserved exactly.
struct AlignmentFitInput {
    std::vector<ConditionedVertex> polyline;
    std::vector<ProtectedAnchor> protectedAnchors;
    // Tolerance for position deviation during the fit.
    double positionTolerance{kDefaultPositionTolerance};
    // Optional maximum curvature constraint (1/radius in canonical project
    // units). When absent, the fitter must not invent a minimum radius or
    // design speed (Issue #7: do not invent engineering constraints the
    // caller has not supplied).
    std::optional<double> maxCurvature;
};

// Result of an alignment fit: either a canonical ReferenceAlignment or a
// typed diagnostic describing why the fit failed. The fitter never
// silently repairs invalid geometry or moves a protected anchor.
struct AlignmentFitResult {
    // The fitted canonical alignment, present on success.
    std::optional<ReferenceAlignment> alignment;
    // Diagnostics describing fit deviations, near-collinear cleanup, etc.
    std::vector<RoadDiagnostic> diagnostics;
    // Segment indices whose start is a protected-anchor boundary. This is
    // canonical continuity metadata and must survive persistence.
    std::set<std::size_t> anchorBoundarySegments;
};

// Validates source geometry coordinates: x and y must be finite; z must
// be absent or finite. NaN is never a valid sentinel for missing elevation
// (use std::nullopt). Returns typed diagnostics for any invalid vertex.
[[nodiscard]] std::vector<RoadDiagnostic> validateRoadSource(
    const RoadSource& source) noexcept;

} // namespace infraforge::domain::road
