#pragma once

#include "infraforge/domain/geo/GeoTypes.hpp"
#include "infraforge/domain/geo/ProjectGeoreference.hpp"
#include "infraforge/domain/terrain/TerrainDataset.hpp"
#include "infraforge/domain/terrain/TerrainTileFile.hpp"
#include "infraforge/domain/world/ChunkGrid.hpp"
#include "infraforge/domain/world/SpatialBounds.hpp"
#include "infraforge/ports/TerrainSource.hpp"

#include <functional>

namespace infraforge::domain::geo {
class GeoTransformService;
}

namespace infraforge::application {

// Derived terrain tile generation: evaluates chunk-aligned, LOD-pyramided
// height grids for one dataset and writes self-describing tile cache files
// (domain::terrain::TerrainTileFile). Runs on a worker thread with
// immutable inputs (dataset snapshot, chunk grid value, project
// georeference value); all canonical-space sampling goes through a
// worker-confined GeoTransformService — never through local coordinate
// math (ADR-0007).
class TerrainTileGenerator {
public:
    struct Input {
        std::filesystem::path projectDirectory; // session context for cache paths
        domain::terrain::TerrainDataset dataset; // immutable snapshot
        domain::world::ChunkGrid grid;           // immutable value
        domain::geo::ProjectGeoreference project;
        std::string sourceCrs; // resolved source CRS definition text
    };

    // Called after each processed tile with (tilesDone, tilesTotal).
    using Progress = std::function<void(std::uint64_t, std::uint64_t)>;

    struct Summary {
        std::uint64_t tilesGenerated{0};
        std::uint64_t tilesSkipped{0}; // empty intersection with the dataset
    };

    // Generates tiles for every chunk of `chunks` that intersects the
    // dataset coverage. `chunks` is captured immutable input; the
    // cancellation checkpoint is checked per tile. Returns the generation
    // summary. Throws domain::terrain::TerrainError on raster/transform
    // failures and JobCancelled-equivalent exceptions through `checkpoint`.
    static Summary generateTiles(const Input& input,
        const ports::TerrainSourceReader& reader, const std::vector<domain::world::ChunkCoord>& chunks,
        const std::function<void()>& checkpoint, const Progress& progress);

    // Deterministic cache path of one tile relative to the project root.
    [[nodiscard]] static std::filesystem::path tilePath(
        const std::filesystem::path& projectDirectory, const std::string& datasetUuid,
        std::int64_t chunkX, std::int64_t chunkY);
};

} // namespace infraforge::application
