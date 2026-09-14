#pragma once

#include "infraforge/domain/ValidationError.hpp"
#include "infraforge/domain/world/EntityId.hpp"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace infraforge::domain::road {

// Canonical road geometry operates entirely in canonical project-global
// coordinates (ADR-0007): easting/northing in the project's canonical linear
// unit, height in the same unit. Road engineering geometry is NEVER evaluated
// in WGS84 latitude/longitude degrees (docs/05_DOMAINS/
// LINEAR_INFRASTRUCTURE_GEOMETRY.md). The renderer reduces to GPU precision at
// the render boundary only; canonical road truth is double precision and
// independent of Vulkan render meshes (ADR-0006, ADR-0009).

// Road station: signed distance along the reference alignment, in the
// project's canonical linear unit. Station 0 is the alignment start; the
// ReferenceAlignment owns continuous stationing with no hidden gaps.
using Station = double;

// Heading: tangent direction of the alignment, in radians, measured
// counterclockwise from the +easting axis. 0 = +easting, pi/2 = +northing.
using Heading = double;

// Curvature: signed reciprocal radius (1/r). Positive curvature turns left
// (counterclockwise), negative curvature turns right (clockwise). Zero is a
// straight line. Curvature is the derivative of heading with respect to
// station: kappa = d(heading)/d(s).
using Curvature = double;

// 2D point on the horizontal reference alignment in canonical project-global
// coordinates. Vertical geometry is owned separately by ElevationProfile.
struct AlignmentPoint {
    double easting{0.0};
    double northing{0.0};

    friend bool operator==(const AlignmentPoint&, const AlignmentPoint&) = default;
};

// Result of evaluating the reference alignment at one station: horizontal
// position, tangent heading, and signed curvature. All three are continuous
// functions of station within a well-formed alignment.
struct AlignmentSample {
    AlignmentPoint position{};
    Heading heading{0.0};
    Curvature curvature{0.0};

    friend bool operator==(const AlignmentSample&, const AlignmentSample&) = default;
};

// Half-open... closed station interval [start, end] of one segment within the
// alignment's continuous stationing. start == previous segment end; the first
// segment starts at station 0. end - start is the segment length.
struct StationRange {
    Station start{0.0};
    Station end{0.0};

    [[nodiscard]] constexpr double length() const noexcept { return end - start; }
    [[nodiscard]] constexpr bool contains(const Station s) const noexcept {
        return s >= start && s <= end;
    }

    friend bool operator==(const StationRange&, const StationRange&) = default;
};

// Road-domain failure taxonomy (docs/05_DOMAINS/ROAD.md). Geospatial CRS
// failures surface through the Geo domain's GeoError and are not duplicated
// here. Road geometry failures are typed and never silently repaired
// (ADR-0010).
enum class RoadErrorCode : std::uint8_t {
    // Malformed command/authoring argument (empty name, null id, ...).
    InvalidArgument,
    // A segment parameter is not finite (NaN/inf coordinate, heading, ...).
    NonFiniteParameter,
    // A segment has zero or negative usable length.
    DegenerateSegment,
    // A circular arc has zero/non-finite curvature (radius).
    InvalidCurvature,
    // Adjacent segments are not positionally continuous (G0 failure).
    PositionDiscontinuity,
    // Adjacent segments are not tangent continuous (G1 failure).
    HeadingDiscontinuity,
    // Adjacent segments break curvature continuity where required (G2).
    CurvatureDiscontinuity,
    // Stationing has a gap or overlap between adjacent segments.
    StationDiscontinuity,
    // An alignment has no segments.
    EmptyAlignment,
    // A profile (elevation/superelevation) is empty or unsorted.
    InvalidProfile,
    // A protected anchor station is outside the alignment range.
    AnchorOutOfRange,
};

[[nodiscard]] std::string_view roadErrorCodeName(RoadErrorCode code) noexcept;
[[nodiscard]] std::optional<RoadErrorCode> roadErrorCodeFromName(std::string_view name) noexcept;

class RoadError : public std::runtime_error {
public:
    RoadError(RoadErrorCode code, std::string message)
        : std::runtime_error(std::move(message)),
          code_(code) {}

    [[nodiscard]] RoadErrorCode code() const noexcept { return code_; }

private:
    RoadErrorCode code_;
};

// One active road diagnostic: a detected fact with a typed code. Diagnostics
// reference canonical entity IDs and source provenance where available; they
// are never fabricated (ADR-0010).
struct RoadDiagnostic {
    RoadErrorCode code{RoadErrorCode::InvalidArgument};
    std::string message;
};

// Kind of protected alignment anchor. Fitting/smoothing must not move any
// protected anchor; the kind records why the anchor is fixed
// (docs/05_DOMAINS/LINEAR_INFRASTRUCTURE_GEOMETRY.md "Protected anchors").
enum class AnchorKind : std::uint8_t {
    // Road junction / connectivity node.
    Junction,
    // Semantically meaningful alignment endpoint.
    Endpoint,
    // User-pinned control point.
    UserPinned,
    // Other semantic anchor (bridge/tunnel portal, cross-domain attachment).
    Semantic,
};

[[nodiscard]] std::string_view anchorKindName(AnchorKind kind) noexcept;
[[nodiscard]] std::optional<AnchorKind> anchorKindFromName(std::string_view name) noexcept;

// A protected alignment point: a station plus the canonical position that
// fitting/smoothing must preserve exactly. Protected anchors are reference
// data, not derived; future fitters consult them before moving geometry.
struct ProtectedAnchor {
    Station station{0.0};
    AlignmentPoint position{};
    AnchorKind kind{AnchorKind::Semantic};

    friend bool operator==(const ProtectedAnchor&, const ProtectedAnchor&) = default;
};

// Source provider that produced the original linework a road was derived from.
// Purely authored roads carry no source geometry; imported roads retain their
// provider and source IDs as provenance.
enum class SourceProvider : std::uint8_t {
    // OpenStreetMap way/node import (future OSM adapter, issue #9).
    Osm,
    // OpenDRIVE import (future adapter, issue #15).
    OpenDrive,
    // Authored directly in the editor (no external source linework).
    Authored,
    // Other GIS/vector source.
    Other,
};

[[nodiscard]] std::string_view sourceProviderName(SourceProvider provider) noexcept;
[[nodiscard]] std::optional<SourceProvider> sourceProviderFromName(std::string_view name) noexcept;

// Stable 128-bit road identity, minted by the road domain service. Persisted
// as canonical uuid text (the row primary key) and used as the world-index
// entity id, exactly like terrain datasets (TRD "Canonical identifiers").
using RoadId = world::EntityId;

// EntityId <-> canonical uuid text. Road ids are persisted as uuid text and
// used as EntityId values in the world index; the mapping packs the 16 uuid
// bytes big-endian into (high, low), identical to the terrain convention.
[[nodiscard]] RoadId roadIdFromUuidText(std::string_view uuidText);
[[nodiscard]] std::string uuidTextFromRoadId(RoadId id);

// Maximum characters accepted for a user-supplied road display name.
inline constexpr std::size_t kMaxRoadDisplayNameLength = 200;

// Display names are metadata, not identity, but still validated for sanity.
[[nodiscard]] std::optional<ValidationError> validateRoadDisplayName(std::string_view name);

} // namespace infraforge::domain::road
