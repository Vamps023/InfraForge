#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace infraforge::viewport {

// One derived terrain tile of the engine's renderer scene projection. This
// is metadata only: the viewport loads the versioned tile cache FILE the
// path points at; canonical raster data never crosses this boundary.
struct TerrainSceneTile {
    std::string datasetUuid;
    std::uint64_t datasetRevision{0};
    std::int64_t chunkX{0};
    std::int64_t chunkY{0};
    std::string path; // session-context absolute path
    double minEasting{0.0};
    double minNorthing{0.0};
    double maxEasting{0.0};
    double maxNorthing{0.0};

    friend bool operator==(const TerrainSceneTile&, const TerrainSceneTile&) = default;
};

// Scene payload of the "scene" control command, produced by the engine and
// forwarded by the desktop shell. Replacing the scene drops residency for
// tiles absent from the new manifest.
struct TerrainScene {
    double originEasting{0.0};
    double originNorthing{0.0};
    double originHeight{0.0};
    std::vector<TerrainSceneTile> tiles;
    std::uint64_t missingTiles{0};
    std::uint64_t revision{0};

    friend bool operator==(const TerrainScene&, const TerrainScene&) = default;
};

// Parses the terrain scene object of a control command. Throws
// CommandParseError on any malformed field — a broken scene is never
// partially applied.
[[nodiscard]] TerrainScene parseTerrainScene(const std::string_view json);

} // namespace infraforge::viewport
