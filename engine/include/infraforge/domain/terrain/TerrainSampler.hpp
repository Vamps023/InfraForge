#pragma once

#include "infraforge/domain/geo/GeoTypes.hpp"
#include "infraforge/domain/geo/ProjectGeoreference.hpp"
#include "infraforge/domain/terrain/TerrainTypes.hpp"
#include "infraforge/ports/TerrainSource.hpp"

#include <cstdint>
#include <filesystem>

namespace infraforge::domain::geo {
class GeoTransformService;
}

namespace infraforge::domain::terrain {

// Canonical double-precision terrain sampling (docs/05_DOMAINS/TERRAIN.md).
// Sampling takes canonical project-global coordinates and resolves heights
// through the real geospatial transform — never through a stored mesh, a
// float shortcut, or terrain-local coordinate math (ADR-0007).
//
// Semantics (documented contract):
// - Coverage: the closed source raster footprint, transformed to canonical
//   space via the Geo service. Points outside it sample to
//   TerrainSampleStatus::OutsideCoverage.
// - Horizontal mapping: each sample is converted to source coordinates with
//   GeoTransformService::projectGlobalToSource, then mapped to cell-center
//   fractional coordinates (GDAL north-up geotransform convention).
// - Interpolation: bilinear over the four surrounding cell centers, with
//   edge clamping at the raster border (exact-cell samples reproduce the
//   stored value).
// - NoData: if any cell of the bilinear stencil is NoData, the sample is
//   TerrainSampleStatus::NoData — no substitution, no nearest-valid search.
// - Height: source elevation units convert to the project's canonical
//   linear unit exactly as the Geo service converts positions
//   (h_project = z_source * elevationUnitToMetre / linearUnit.toMetre).
//
// Threading: a sampler references a GeoTransformService, which is bound to
// one thread. Create the sampler on the thread that samples (application
// executor for queries; a worker for tile generation, with its own
// transform service instance).
class TerrainSampler {
public:
    // Raster geometry of the dataset being sampled (the canonical record's
    // source-space fields). NoData handling is not part of the geometry —
    // the reader port already surfaces NoData cells as quiet NaN.
    struct RasterGeometry {
        std::int64_t width{0};
        std::int64_t height{0};
        double originX{0.0};
        double originY{0.0};
        double cellSizeX{0.0};
        double cellSizeY{0.0};
        double elevationUnitToMetre{1.0};
    };

    TerrainSampler(const ports::TerrainSourceReader& reader, std::filesystem::path rasterFile,
        RasterGeometry geometry, geo::ProjectGeoreference project,
        geo::SourceSpatialReference source, const geo::GeoTransformService& transforms);

    [[nodiscard]] TerrainSample sample(double easting, double northing) const;

    // Converts a source elevation value into the canonical linear unit
    // (shared by sampling and tile generation so both apply one rule).
    [[nodiscard]] double canonicalHeight(double sourceZ) const;

    // Canonical-space horizontal coverage of the raster (closed bounds in
    // project-global coordinates, through the real transform of the four
    // source-space corners).
    [[nodiscard]] geo::ProjectGlobalPosition sourceToProjectGlobal(double sourceX, double sourceY) const;
    [[nodiscard]] geo::GeoCoordinate projectToSource(double easting, double northing) const;

private:
    // reader_/transforms_ are service references owned elsewhere for the
    // sampler's whole lifetime; the georeference and source reference are
    // COPIED — callers construct samplers with temporaries (e.g. a
    // SourceSpatialReference built at the call site), so reference members
    // would dangle before the first sample().
    const ports::TerrainSourceReader& reader_;
    std::filesystem::path rasterFile_;
    RasterGeometry geometry_;
    geo::ProjectGeoreference project_;
    geo::SourceSpatialReference source_;
    const geo::GeoTransformService& transforms_;
    double linearUnitToMetre_;
};

} // namespace infraforge::domain::terrain
