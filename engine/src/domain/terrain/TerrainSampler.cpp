#include "infraforge/domain/terrain/TerrainSampler.hpp"

#include "infraforge/domain/geo/GeoTransformService.hpp"
#include "infraforge/domain/terrain/TerrainTypes.hpp"

#include <algorithm>
#include <cmath>

namespace infraforge::domain::terrain {
namespace {

// True when the value is the quiet NaN the source reader substitutes for
// NoData cells.
[[nodiscard]] bool isNodata(double value) noexcept {
    return std::isnan(value);
}

} // namespace

TerrainSampler::TerrainSampler(
    const ports::TerrainSourceReader& reader, std::filesystem::path rasterFile,
    RasterGeometry geometry, geo::ProjectGeoreference project,
    geo::SourceSpatialReference source, const geo::GeoTransformService& transforms)
    : reader_(reader),
      rasterFile_(std::move(rasterFile)),
      geometry_(geometry),
      project_(std::move(project)),
      source_(std::move(source)),
      transforms_(transforms),
      linearUnitToMetre_(project_.linearUnit.toMetre) {
    if (linearUnitToMetre_ <= 0.0 || !std::isfinite(linearUnitToMetre_)) {
        throw TerrainError(TerrainErrorCode::InvalidArgument,
            "project linear unit must resolve to a positive metre factor");
    }
}

geo::ProjectGlobalPosition TerrainSampler::sourceToProjectGlobal(double sourceX, double sourceY) const {
    return transforms_.sourceToProjectGlobal(
        project_, source_, geo::GeoCoordinate{.x = sourceX, .y = sourceY, .z = 0.0});
}

geo::GeoCoordinate TerrainSampler::projectToSource(double easting, double northing) const {
    return transforms_.projectGlobalToSource(
        project_, source_, geo::ProjectGlobalPosition{.easting = easting, .northing = northing, .height = 0.0});
}

double TerrainSampler::canonicalHeight(double sourceZ) const {
    // Source elevation unit -> metres -> project canonical linear unit.
    return sourceZ * geometry_.elevationUnitToMetre / linearUnitToMetre_;
}

TerrainSample TerrainSampler::sample(double easting, double northing) const {
    if (!std::isfinite(easting) || !std::isfinite(northing)) {
        throw TerrainError(TerrainErrorCode::InvalidArgument, "sample coordinates must be finite");
    }

    // Canonical horizontal position -> source coordinates through the real
    // geospatial transform (double precision end to end).
    const geo::GeoCoordinate source = projectToSource(easting, northing);

    // Closed source-space coverage rectangle of the north-up raster.
    const double west = geometry_.originX;
    const double east = geometry_.originX + static_cast<double>(geometry_.width) * geometry_.cellSizeX;
    const double south = geometry_.originY - static_cast<double>(geometry_.height) * geometry_.cellSizeY;
    const double north = geometry_.originY;
    if (source.x < west || source.x > east || source.y < south || source.y > north) {
        return {.status = TerrainSampleStatus::OutsideCoverage, .height = 0.0};
    }

    // Cell-center fractional coordinates: column centers sit at
    // originX + (i + 0.5) * cellSizeX; row centers at
    // originY - (j + 0.5) * cellSizeY (north-up: y decreases with rows).
    const double columnF = (source.x - (geometry_.originX + 0.5 * geometry_.cellSizeX)) / geometry_.cellSizeX;
    const double rowF = ((geometry_.originY - 0.5 * geometry_.cellSizeY) - source.y) / geometry_.cellSizeY;

    const std::int64_t column = static_cast<std::int64_t>(std::floor(columnF));
    const std::int64_t row = static_cast<std::int64_t>(std::floor(rowF));
    const double du = columnF - static_cast<double>(column);
    const double dv = rowF - static_cast<double>(row);

    // Edge clamping: at the border the stencil folds onto the outer cell
    // centers, so the duplicate weight cancels and the edge value is used.
    const std::int64_t c0 = std::clamp(column, std::int64_t{0}, geometry_.width - 1);
    const std::int64_t c1 = std::clamp(column + 1, std::int64_t{0}, geometry_.width - 1);
    const std::int64_t r0 = std::clamp(row, std::int64_t{0}, geometry_.height - 1);
    const std::int64_t r1 = std::clamp(row + 1, std::int64_t{0}, geometry_.height - 1);

    const ports::TerrainElevationBlock block =
        reader_.readBlock(rasterFile_, c0, r0, c1 - c0 + 1, r1 - r0 + 1);
    const auto at = [&](std::int64_t x, std::int64_t y) -> double {
        const std::size_t index = static_cast<std::size_t>(y - block.offsetY)
                * static_cast<std::size_t>(block.width)
            + static_cast<std::size_t>(x - block.offsetX);
        return block.elevations.at(index);
    };

    const double h00 = at(c0, r0);
    const double h10 = at(c1, r0);
    const double h01 = at(c0, r1);
    const double h11 = at(c1, r1);
    if (isNodata(h00) || isNodata(h10) || isNodata(h01) || isNodata(h11)) {
        return {.status = TerrainSampleStatus::NoData, .height = 0.0};
    }

    const double w00 = (1.0 - du) * (1.0 - dv);
    const double w10 = du * (1.0 - dv);
    const double w01 = (1.0 - du) * dv;
    const double w11 = du * dv;
    const double sourceHeight = w00 * h00 + w10 * h10 + w01 * h01 + w11 * h11;
    if (!std::isfinite(sourceHeight)) {
        // Non-finite stored samples are corrupt content, not data.
        throw TerrainError(TerrainErrorCode::CorruptSource,
            "raster produced a non-finite elevation at the requested position");
    }
    return {.status = TerrainSampleStatus::Height, .height = canonicalHeight(sourceHeight)};
}

} // namespace infraforge::domain::terrain
