#include "infraforge/application/TerrainTileGenerator.hpp"

#include "infraforge/domain/geo/GeoTransformService.hpp"
#include "infraforge/domain/terrain/TerrainSampler.hpp"
#include "infraforge/domain/terrain/TerrainTypes.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace infraforge::application {
namespace {

using domain::terrain::TerrainError;
using domain::terrain::TerrainErrorCode;
using domain::world::ChunkCoord;

// Raster cells read beyond the transformed tile footprint before sampling;
// covers sub-cell source drift of the canonical grid edges. Vertices that
// still fall outside the pre-read window fall back to a dedicated 2x2 read
// so curvature of non-affine transforms can never produce wrong heights.
constexpr std::int64_t kSourceWindowMarginCells = 2;

struct BlockSampler {
    const ports::TerrainSourceReader& reader;
    const std::filesystem::path& rasterFile;
    domain::terrain::TerrainSampler::RasterGeometry geometry;

    // One pre-read source window in raster cell coordinates.
    ports::TerrainElevationBlock block;

    // Bilinear sample over the block using the canonical cell-center
    // semantics of TerrainSampler. Returns false when the position lies
    // outside the block's bilinear-safe area (a fallback read is needed);
    // writes NaN for NoData cells.
    [[nodiscard]] bool sample(double sourceX, double sourceY, double& outHeight) const {
        const double columnF =
            (sourceX - (geometry.originX + 0.5 * geometry.cellSizeX)) / geometry.cellSizeX;
        const double rowF =
            ((geometry.originY - 0.5 * geometry.cellSizeY) - sourceY) / geometry.cellSizeY;
        const std::int64_t column = static_cast<std::int64_t>(std::floor(columnF));
        const std::int64_t row = static_cast<std::int64_t>(std::floor(rowF));

        const std::int64_t bx = block.offsetX;
        const std::int64_t by = block.offsetY;
        const std::int64_t bw = block.width;
        const std::int64_t bh = block.height;
        if (column < bx || column > bx + bw - 2 || row < by || row > by + bh - 2) {
            return false;
        }
        const double du = columnF - static_cast<double>(column);
        const double dv = rowF - static_cast<double>(row);
        const auto at = [&](std::int64_t x, std::int64_t y) {
            return block.elevations[static_cast<std::size_t>(y - by) * static_cast<std::size_t>(bw)
                + static_cast<std::size_t>(x - bx)];
        };
        const double h00 = at(column, row);
        const double h10 = at(column + 1, row);
        const double h01 = at(column, row + 1);
        const double h11 = at(column + 1, row + 1);
        if (std::isnan(h00) || std::isnan(h10) || std::isnan(h01) || std::isnan(h11)) {
            outHeight = std::numeric_limits<double>::quiet_NaN();
            return true;
        }
        outHeight = (1.0 - du) * (1.0 - dv) * h00 + du * (1.0 - dv) * h10
            + (1.0 - du) * dv * h01 + du * dv * h11;
        return true;
    }
};

[[nodiscard]] domain::terrain::TerrainSampler::RasterGeometry rasterGeometryOf(
    const domain::terrain::TerrainDataset& dataset) {
    domain::terrain::TerrainSampler::RasterGeometry geometry;
    geometry.width = dataset.rasterWidth;
    geometry.height = dataset.rasterHeight;
    geometry.originX = dataset.originX;
    geometry.originY = dataset.originY;
    geometry.cellSizeX = dataset.cellSizeX;
    geometry.cellSizeY = dataset.cellSizeY;
    geometry.elevationUnitToMetre = dataset.elevationUnitToMetre;
    return geometry;
}

} // namespace

std::filesystem::path TerrainTileGenerator::tilePath(
    const std::filesystem::path& projectDirectory, const std::string& datasetUuid,
    const std::int64_t chunkX, const std::int64_t chunkY) {
    return projectDirectory / "cache" / "terrain" / datasetUuid
        / ("tile_" + std::to_string(chunkX) + "_" + std::to_string(chunkY)
            + std::string{domain::terrain::kTerrainTileFileExtension});
}

TerrainTileGenerator::Summary TerrainTileGenerator::generateTiles(const Input& input,
    const ports::TerrainSourceReader& reader, const std::vector<ChunkCoord>& chunks,
    const std::function<void()>& checkpoint, const Progress& progress) {
    if (chunks.empty()) {
        return {};
    }
    if (input.dataset.bounds.isEmpty() || !input.dataset.bounds.isFinite()) {
        throw TerrainError(TerrainErrorCode::InvalidCoverage, "terrain dataset coverage is invalid");
    }

    // Worker-confined transform service: PROJ objects are thread-bound, so
    // tile generation resolves coordinates through its own instance.
    const domain::geo::GeoTransformService transforms;

    // Sampling helper over the stored project-owned raster.
    const std::filesystem::path rasterFile =
        input.projectDirectory / std::filesystem::path{input.dataset.storagePath};
    BlockSampler sampler{
        .reader = reader,
        .rasterFile = rasterFile,
        .geometry = rasterGeometryOf(input.dataset),
        .block = {},
    };

    // Reference sampler for the rare fallback reads uses the same reader;
    // both share identical cell-center semantics.
    domain::terrain::TerrainSampler referenceSampler(
        reader, rasterFile, rasterGeometryOf(input.dataset), input.project,
        domain::geo::SourceSpatialReference{.horizontalCrs = input.sourceCrs, .verticalCrs = ""},
        transforms);

    Summary summary;
    const std::uint64_t total = chunks.size();
    std::uint64_t done = 0;

    for (const ChunkCoord chunk : chunks) {
        checkpoint();
        const domain::world::SpatialBounds tileRect =
            input.grid.chunkBounds(chunk).intersectedWith(input.dataset.bounds);
        if (tileRect.isEmpty()) {
            ++summary.tilesSkipped;
            ++done;
            if (progress) {
                progress(done, total);
            }
            continue;
        }

        domain::terrain::TerrainTileFile tile;
        tile.datasetUuid = domain::terrain::uuidTextFromEntityId(input.dataset.id);
        tile.datasetRevision = input.dataset.revision;
        tile.chunkX = chunk.x;
        tile.chunkY = chunk.y;
        tile.lods.resize(domain::terrain::kTerrainTileLodCount);

        // Source-space bounding box of the whole tile footprint, computed
        // once per tile: canonical rect corners + edge midpoints + center
        // through the real transform, padded by the cell margin. ONE block
        // read covers every LOD level of the tile; strongly non-affine
        // transforms whose vertices still fall outside use the exact
        // per-point fallback sampler below. (Per-vertex re-reads would open
        // the raster once per sample — the block-read rule exists exactly
        // for this.)
        std::optional<ports::TerrainElevationBlock> window;
        double windowWest = 0.0;
        double windowEast = 0.0;
        double windowSouth = 0.0;
        double windowNorth = 0.0;
        {
            double minSourceX = std::numeric_limits<double>::infinity();
            double maxSourceX = -std::numeric_limits<double>::infinity();
            double minSourceY = std::numeric_limits<double>::infinity();
            double maxSourceY = -std::numeric_limits<double>::infinity();
            const auto expandTo = [&](const double easting, const double northing) {
                const domain::geo::GeoCoordinate source = referenceSampler.projectToSource(easting, northing);
                minSourceX = std::min(minSourceX, source.x);
                maxSourceX = std::max(maxSourceX, source.x);
                minSourceY = std::min(minSourceY, source.y);
                maxSourceY = std::max(maxSourceY, source.y);
            };
            const double midE = (tileRect.minEasting + tileRect.maxEasting) * 0.5;
            const double midN = (tileRect.minNorthing + tileRect.maxNorthing) * 0.5;
            expandTo(tileRect.minEasting, tileRect.minNorthing);
            expandTo(tileRect.minEasting, tileRect.maxNorthing);
            expandTo(tileRect.maxEasting, tileRect.minNorthing);
            expandTo(tileRect.maxEasting, tileRect.maxNorthing);
            expandTo(midE, tileRect.minNorthing);
            expandTo(midE, tileRect.maxNorthing);
            expandTo(tileRect.minEasting, midN);
            expandTo(tileRect.maxEasting, midN);
            expandTo(midE, midN);

            const double padX = static_cast<double>(kSourceWindowMarginCells) * input.dataset.cellSizeX;
            const double padY = static_cast<double>(kSourceWindowMarginCells) * input.dataset.cellSizeY;
            windowWest = minSourceX - padX;
            windowEast = maxSourceX + padX;
            windowSouth = minSourceY - padY;
            windowNorth = maxSourceY + padY;

            const std::int64_t offsetX = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::floor(
                (windowWest - input.dataset.originX) / input.dataset.cellSizeX)));
            const std::int64_t offsetY = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::floor(
                (input.dataset.originY - windowNorth) / input.dataset.cellSizeY)));
            const std::int64_t limitX = std::min<std::int64_t>(input.dataset.rasterWidth,
                static_cast<std::int64_t>(std::ceil(
                    (windowEast - input.dataset.originX) / input.dataset.cellSizeX)));
            const std::int64_t limitY = std::min<std::int64_t>(input.dataset.rasterHeight,
                static_cast<std::int64_t>(std::ceil(
                    (input.dataset.originY - windowSouth) / input.dataset.cellSizeY)));
            if (limitX - offsetX < 2 || limitY - offsetY < 2) {
                // The tile footprint does not overlap the raster footprint
                // in source space; leave the window empty — every vertex
                // resolves through the exact fallback sampler.
                window.reset();
            } else {
                window = reader.readBlock(rasterFile, offsetX, offsetY, limitX - offsetX, limitY - offsetY);
                // Recompute the bilinear-safe area the block covers (cell
                // centers of the first through second-to-last cells).
                windowWest = input.dataset.originX
                    + (static_cast<double>(window->offsetX) + 0.5) * input.dataset.cellSizeX;
                windowEast = input.dataset.originX
                    + (static_cast<double>(window->offsetX + window->width) - 1.5) * input.dataset.cellSizeX;
                windowNorth = input.dataset.originY
                    - (static_cast<double>(window->offsetY) + 0.5) * input.dataset.cellSizeY;
                windowSouth = input.dataset.originY
                    - (static_cast<double>(window->offsetY + window->height) - 1.5) * input.dataset.cellSizeY;
                sampler.block = std::move(*window);
            }
        }

        for (std::uint32_t level = 0; level < domain::terrain::kTerrainTileLodCount; ++level) {
            const std::uint32_t dim = domain::terrain::terrainTileLodDim(level);
            domain::terrain::TerrainTileLodGrid& lod = tile.lods[level];
            lod.dim = dim;
            lod.originEasting = tileRect.minEasting;
            lod.originNorthing = tileRect.maxNorthing;
            lod.cellEasting = (tileRect.maxEasting - tileRect.minEasting) / static_cast<double>(dim - 1);
            lod.cellNorthing = (tileRect.maxNorthing - tileRect.minNorthing) / static_cast<double>(dim - 1);
            lod.heights.assign(static_cast<std::size_t>(dim) * dim, std::numeric_limits<double>::quiet_NaN());
            lod.minZ = 0.0;
            lod.maxZ = 0.0;

            bool anyFinite = false;
            double minZ = std::numeric_limits<double>::infinity();
            double maxZ = -std::numeric_limits<double>::infinity();

            for (std::uint32_t row = 0; row < dim; ++row) {
                const double northing =
                    lod.originNorthing - static_cast<double>(row) * lod.cellNorthing;
                for (std::uint32_t column = 0; column < dim; ++column) {
                    const double easting =
                        lod.originEasting + static_cast<double>(column) * lod.cellEasting;

                    // Canonical grid position -> source coordinates through
                    // the real geospatial transform (double precision).
                    const domain::geo::GeoCoordinate source = referenceSampler.projectToSource(easting, northing);

                    double sourceHeight = std::numeric_limits<double>::quiet_NaN();
                    const bool insideWindow = window.has_value()
                        && source.x >= windowWest && source.x <= windowEast
                        && source.y >= windowSouth && source.y <= windowNorth;
                    if (insideWindow && sampler.sample(source.x, source.y, sourceHeight)) {
                        // Fast path: served from the tile's pre-read window.
                    } else if (insideWindow) {
                        // Inside the window footprint but touching NoData
                        // cells on its padding edge: exact 2x2 read.
                        const domain::terrain::TerrainSample exact = referenceSampler.sample(easting, northing);
                        if (exact.status == domain::terrain::TerrainSampleStatus::Height) {
                            sourceHeight = exact.height / (input.dataset.elevationUnitToMetre
                                / input.project.linearUnit.toMetre);
                        }
                    } else {
                        // Vertex fell outside the pre-read window (strongly
                        // non-affine transform or empty window): exact read.
                        const domain::terrain::TerrainSample exact = referenceSampler.sample(easting, northing);
                        if (exact.status == domain::terrain::TerrainSampleStatus::Height) {
                            sourceHeight = exact.height / (input.dataset.elevationUnitToMetre
                                / input.project.linearUnit.toMetre);
                        }
                    }
                    if (std::isnan(sourceHeight)) {
                        continue; // NoData stays NaN; renderer drops it
                    }
                    const double canonical = sourceHeight * input.dataset.elevationUnitToMetre
                        / input.project.linearUnit.toMetre;
                    lod.heights[static_cast<std::size_t>(row) * dim + column] = canonical;
                    anyFinite = true;
                    minZ = std::min(minZ, canonical);
                    maxZ = std::max(maxZ, canonical);
                }
            }
            if (anyFinite) {
                lod.minZ = minZ;
                lod.maxZ = maxZ;
            }
        }

        domain::terrain::encodeTerrainTileToFile(
            tile, tilePath(input.projectDirectory, tile.datasetUuid, chunk.x, chunk.y).string());
        ++summary.tilesGenerated;
        ++done;
        if (progress) {
            progress(done, total);
        }
    }
    return summary;
}

} // namespace infraforge::application
