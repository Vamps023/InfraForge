#pragma once

#include "infraforge/domain/geo/GeoreferenceConfig.hpp"
#include "infraforge/domain/geo/GeoTypes.hpp"

#include <cstdint>
#include <string>

namespace infraforge::domain::geo {

// Resolved object kind reported for a CRS definition. Only Projected and
// Engineering kinds may anchor a project georeference.
enum class CrsKind : std::uint8_t {
    Projected,
    Engineering,
    Geographic,
    Geocentric,
    Vertical,
    Compound,
    Other,
};

[[nodiscard]] std::string_view crsKindName(CrsKind kind) noexcept;

// Resolved metadata about a CRS definition accepted by the geospatial
// engine. Produced by GeoTransformService; never authored by consumers.
struct ResolvedCrs {
    // The CRS definition as configured (identifier, URN, WKT2, PROJJSON);
    // used to build transformations.
    std::string definition;
    // Canonical "AUTH:CODE" identifier when the definition resolves to a
    // database authority entry; empty otherwise.
    std::string identifier;
    // True only when the original definition object is canonically
    // equivalent to the authority CRS named by `identifier` — the only
    // case where canonicalization may substitute the identifier for the
    // definition. A bound CRS or a custom definition that merely carries
    // an ID element must keep its original form.
    bool authoritativeIdentifier{false};
    std::string name;
    std::string authority;
    std::string code;
    CrsKind kind{CrsKind::Other};
    // Conversion factor from the CRS horizontal axis unit to metres; zero
    // for angular (degree-based) axes.
    double axisUnitToMetre{0.0};
};

// Resolved canonical linear unit of project-global coordinates.
struct ResolvedUnit {
    std::string name;
    // Conversion factor to metres.
    double toMetre{1.0};
};

// Configured vertical reference. A configured vertical CRS must resolve to
// a real vertical CRS — unresolvable or wrong-role definitions are
// rejected when the georeference is created, set, or opened.
struct VerticalReference {
    // The vertical CRS definition as configured; used to build compound
    // transformations.
    std::string definition;
    // Canonical identifier when resolvable; the configured definition
    // otherwise. Empty when no vertical reference is configured.
    std::string identifier;
    std::string name;
    bool present{false};
    // True only when the configured definition is canonically equivalent
    // to `identifier`; see ResolvedCrs::authoritativeIdentifier.
    bool authoritativeIdentifier{false};
    // True when the definition resolves to a vertical CRS that can
    // participate in compound transformations. Grid-dependent datum
    // transformations may still fail per-command when unavailable.
    bool transformSupported{false};
    // Conversion factor from the vertical axis unit to metres.
    double axisUnitToMetre{0.0};
};

// Canonical resolved project georeference: the validated runtime form of
// the persisted GeoreferenceConfig produced by GeoTransformService.
struct ProjectGeoreference {
    ResolvedCrs horizontalCrs;
    ResolvedUnit linearUnit;
    AxisConvention axisConvention{AxisConvention::EastingNorthingUp};
    // Project origin in project-global space (canonical linear unit).
    ProjectGlobalPosition origin{};
    VerticalReference vertical;
};

} // namespace infraforge::domain::geo
