#pragma once

#include "infraforge/domain/ValidationError.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace infraforge::domain::geo {

// Axis convention of canonical project-global space. Easting/Northing/Up is
// the only convention: +X points east, +Y points north, +Z points up along
// the vertical reference (right-handed ENU frame).
enum class AxisConvention : std::uint8_t {
    EastingNorthingUp,
};

// The single canonical georeference configuration owned by the Geo domain
// (ADR-0007). Persisted once per project; every spatial conversion must go
// through the shared Geo transform service — no consumer may keep an
// independent CRS/origin model.
//
// Field semantics:
// - horizontalCrs: definition resolvable by the geospatial engine
//   ("EPSG:32633", OGC URN, WKT2, PROJJSON). A definition is canonicalized
//   to "AUTH:CODE" only when canonically equivalent to the authority
//   entry; bound CRSs and custom definitions are preserved verbatim.
// - linearUnit: canonical unit of project-global coordinates (e.g.
//   "metre"), resolved against the geospatial unit database.
// - origin: project origin expressed in the horizontal CRS coordinate
//   frame, in the canonical linear unit.
// - verticalCrs: optional vertical CRS/datum identifier; empty when
//   unknown. Must resolve to a vertical CRS when present — unresolvable or
//   wrong-role definitions are rejected, never stored as metadata.
struct GeoreferenceConfig {
    std::string horizontalCrs;
    std::string linearUnit;
    AxisConvention axisConvention{AxisConvention::EastingNorthingUp};
    double originEasting{0.0};
    double originNorthing{0.0};
    double originHeight{0.0};
    std::string verticalCrs;

    friend bool operator==(const GeoreferenceConfig&, const GeoreferenceConfig&) = default;
};

[[nodiscard]] std::string_view axisConventionName(AxisConvention convention) noexcept;
[[nodiscard]] std::optional<AxisConvention> axisConventionFromName(std::string_view name) noexcept;

// Structural validation only: presence, length, and finiteness. Whether the
// CRS/unit is resolvable and supported is decided by GeoTransformService.
[[nodiscard]] std::optional<ValidationError> validateGeoreference(const GeoreferenceConfig& georeference);

} // namespace infraforge::domain::geo
