#pragma once

#include "infraforge/domain/geo/GeoreferenceConfig.hpp"
#include "infraforge/domain/geo/ProjectGeoreference.hpp"
#include "infraforge/domain/geo/RenderLocalFrame.hpp"

#include <memory>
#include <string_view>

namespace infraforge::domain::geo {

// The single canonical geospatial transformation service (ADR-0007). It
// resolves and validates project georeference configurations and performs
// Source <-> Project-global <-> Render-local conversions on behalf of every
// spatial domain: terrain, roads, rails, assets, importers, exporters,
// simulation queries, and renderer coordinate conversion.
//
// Backed by the PROJ geospatial library. PROJ objects are confined to this
// service and never leak into domain signatures. A service instance is
// bound to one thread (the application executor); other components — such
// as a renderer on its own thread — create their own instance or consume
// the header-only RenderLocalFrame boundary.
class GeoTransformService {
public:
    // Creates the service and locates the geospatial runtime database
    // (proj.db). Throws GeoError(LibraryUnavailable) when it cannot be
    // found — the engine never substitutes a built-in CRS table.
    GeoTransformService();
    ~GeoTransformService();

    GeoTransformService(const GeoTransformService&) = delete;
    GeoTransformService& operator=(const GeoTransformService&) = delete;

    // Resolves and validates a persisted configuration into the canonical
    // runtime georeference. The horizontal CRS must resolve to a projected
    // or engineering CRS with linear axes, the linear unit must resolve to
    // a linear unit of measure, and a configured vertical CRS must resolve
    // to a vertical CRS. Throws GeoError otherwise; never falls back to
    // EPSG:4326, EPSG:3857, or an identity transform.
    [[nodiscard]] ProjectGeoreference resolveProjectGeoreference(
        const GeoreferenceConfig& config) const;

    // Validates the configuration and returns its canonical persisted form:
    // authority-resolvable CRS definitions become "AUTH:CODE" and the
    // linear unit becomes its canonical unit-database name. Throws GeoError
    // on any invalid or unsupported element.
    [[nodiscard]] GeoreferenceConfig canonicalizeConfig(const GeoreferenceConfig& config) const;

    // Describes any CRS definition for capability/status reporting.
    // Throws GeoError(InvalidCrs) when the definition cannot be resolved.
    [[nodiscard]] ResolvedCrs describeCrs(std::string_view definition) const;

    // Source space -> canonical project-global space. The source's
    // coordinates are transformed into the project horizontal CRS frame and
    // expressed in the canonical linear unit. The source horizontal CRS
    // must resolve to a geographic (2D or 3D), projected, or engineering
    // CRS, and a supplied source vertical CRS must resolve to a vertical
    // CRS — other roles throw GeoError(UnsupportedCrs). Height handling:
    // when both references configure differing vertical CRSs, a compound
    // transformation is attempted and UnsupportedTransform is thrown when
    // the geospatial engine cannot build it; otherwise the height value is
    // converted between the source vertical axis unit (metres when no
    // source vertical CRS is supplied) and the project's canonical linear
    // unit.
    [[nodiscard]] ProjectGlobalPosition sourceToProjectGlobal(
        const ProjectGeoreference& project,
        const SourceSpatialReference& source,
        const GeoCoordinate& coordinate) const;

    // Canonical project-global space -> source space (inverse of
    // sourceToProjectGlobal).
    [[nodiscard]] GeoCoordinate projectGlobalToSource(
        const ProjectGeoreference& project,
        const SourceSpatialReference& source,
        const ProjectGlobalPosition& position) const;

    // Canonical Project-global -> Render-local boundary anchored at the
    // project origin. Renderers may alternatively anchor a frame at an
    // explicit render origin via RenderLocalFrame::atRenderOrigin.
    [[nodiscard]] RenderLocalFrame renderLocalFrame(const ProjectGeoreference& project) const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace infraforge::domain::geo
