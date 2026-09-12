#pragma once

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace infraforge::domain::geo {

// Failure taxonomy of the canonical geospatial service. Mapped onto the
// application/protocol error codes by the application layer; geospatial
// failures are never silently replaced by a different CRS or an identity
// transform.
enum class GeoErrorCode : std::uint8_t {
    // CRS definition is malformed or cannot be resolved at all.
    InvalidCrs,
    // CRS resolves but cannot anchor the requested role (e.g. a geographic
    // or vertical CRS used as the project horizontal CRS).
    UnsupportedCrs,
    // Canonical linear unit is unresolvable or not a linear unit.
    UnsupportedUnit,
    // No transformation can be built for the requested pair (e.g. a
    // differing vertical datum whose grid support is unavailable).
    UnsupportedTransform,
    // Input or output coordinate is not finite.
    NotFinite,
    // Geospatial runtime data (proj.db) is missing.
    LibraryUnavailable,
    // Unexpected geospatial engine failure.
    LibraryFailure,
};

class GeoError : public std::runtime_error {
public:
    GeoError(GeoErrorCode code, std::string message)
        : std::runtime_error(std::move(message)),
          code_(code) {}

    [[nodiscard]] GeoErrorCode code() const noexcept { return code_; }

private:
    GeoErrorCode code_;
};

// Coordinate spaces (docs/02_DATA/GEOREFERENCE.md):
//
// - Source space: coordinates in an imported/source dataset CRS.
// - Project-global space: canonical persisted double-precision project
//   coordinates in the project horizontal CRS frame, expressed in the
//   canonical linear unit, axes easting/northing/up.
// - Render-local space: coordinates relative to a double-precision render
//   origin, same axes and unit as project-global space; the only space a
//   renderer may reduce to GPU precision, at the render boundary.

// Coordinate in a source dataset CRS in normalized GIS-friendly axis order
// (proj_normalize_for_visualization): x is longitude for a geographic
// source CRS or easting for a projected one, y is latitude or northing,
// z is the height expressed in the source vertical CRS's axis unit — or
// metres when the source carries no vertical CRS.
struct GeoCoordinate {
    double x{0.0};
    double y{0.0};
    double z{0.0};

    friend bool operator==(const GeoCoordinate&, const GeoCoordinate&) = default;
};

// Spatial reference of source coordinates: a horizontal CRS definition plus
// an optional vertical CRS identifier (empty when unknown).
struct SourceSpatialReference {
    std::string horizontalCrs;
    std::string verticalCrs;

    friend bool operator==(const SourceSpatialReference&, const SourceSpatialReference&) = default;
};

// Canonical project-global position (double precision, never float).
struct ProjectGlobalPosition {
    double easting{0.0};
    double northing{0.0};
    double height{0.0};

    friend bool operator==(const ProjectGlobalPosition&, const ProjectGlobalPosition&) = default;
};

// Render-local position relative to a render origin. Double precision on
// the canonical side; renderers may convert to float at the GPU boundary.
struct RenderLocalPosition {
    double x{0.0};
    double y{0.0};
    double z{0.0};

    friend bool operator==(const RenderLocalPosition&, const RenderLocalPosition&) = default;
};

[[nodiscard]] inline bool isFinite(const GeoCoordinate& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] inline bool isFinite(const ProjectGlobalPosition& value) noexcept {
    return std::isfinite(value.easting) && std::isfinite(value.northing) && std::isfinite(value.height);
}

[[nodiscard]] inline bool isFinite(const RenderLocalPosition& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

} // namespace infraforge::domain::geo
