#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace infraforge::domain::terrain {

// Derived terrain tile file format ("docs/05_DOMAINS/TERRAIN.md", tile
// cache). One file holds the LOD pyramid for one world-chunk tile of one
// dataset: a canonical-space regular vertex grid per LOD level with
// double-precision elevations in the project linear unit.
//
// This is derived, rebuildable cache content (ADR-0005) — never canonical
// project state. Every file is self-describing (schema version, generator
// revision, dataset identity + revision, chunk identity) so stale results
// are rejected instead of interpreted. Header-only and dependency-free so
// the engine writes tiles and the viewport process reads them with the
// exact same codec.
//
// All integers are little-endian two's complement; all floats are IEEE-754
// little-endian. The static endian check below refuses to compile on a
// big-endian target rather than silently writing swapped bytes.

inline constexpr char kTerrainTileMagic[4] = {'I', 'F', 'G', 'T'};
inline constexpr std::uint32_t kTerrainTileSchemaVersion = 1;
inline constexpr std::uint32_t kTerrainTileGeneratorRevision = 1;
inline constexpr std::string_view kTerrainTileFileExtension = ".iforgetile";

// LOD pyramid: level 0 evaluates 129x129 vertices per tile and each
// following level halves the resolution (129, 65, 33, 17, 9). Deterministic
// so renderer LOD decisions and cache contents stay reproducible.
inline constexpr std::uint32_t kTerrainTileLodBaseDim = 129;
inline constexpr std::uint32_t kTerrainTileLodCount = 5;

[[nodiscard]] inline constexpr std::uint32_t terrainTileLodDim(const std::uint32_t level) noexcept {
    return ((kTerrainTileLodBaseDim - 1u) >> level) + 1u;
}

struct TerrainTileLodGrid {
    std::uint32_t dim{0}; // vertices per side
    // Canonical-space grid: vertex (row r, column c) sits at
    // (originEasting + c*cellEasting, originNorthing - r*cellNorthing);
    // rows run north -> south, cell sizes are positive magnitudes, all
    // values are exact doubles in the project linear unit.
    double originEasting{0.0};
    double originNorthing{0.0};
    double cellEasting{0.0};
    double cellNorthing{0.0};
    double minZ{0.0};
    double maxZ{0.0};
    // dim*dim elevations, row-major, absolute canonical elevation (project
    // linear unit). NoData positions carry quiet NaN; the renderer draws
    // them as dropped vertices, never as interpolated heights.
    std::vector<double> heights;
};

struct TerrainTileFile {
    std::uint32_t schemaVersion{kTerrainTileSchemaVersion};
    std::uint32_t generatorRevision{kTerrainTileGeneratorRevision};
    // Provenance: the dataset this tile was derived from.
    std::string datasetUuid; // 36-char canonical text
    std::uint64_t datasetRevision{0};
    std::int64_t chunkX{0};
    std::int64_t chunkY{0};
    // LODs strictly descending in resolution, level i has dim
    // terrainTileLodDim(i).
    std::vector<TerrainTileLodGrid> lods;
};

class TerrainTileFormatError : public std::runtime_error {
public:
    explicit TerrainTileFormatError(std::string message)
        : std::runtime_error(std::move(message)) {}
};

namespace detail {

inline void putU32(std::string& out, const std::uint32_t value) {
    std::array<char, 4> bytes{};
    std::memcpy(bytes.data(), &value, 4);
    out.append(bytes.data(), 4);
}

inline void putU64(std::string& out, const std::uint64_t value) {
    std::array<char, 8> bytes{};
    std::memcpy(bytes.data(), &value, 8);
    out.append(bytes.data(), 8);
}

inline void putI64(std::string& out, const std::int64_t value) {
    putU64(out, static_cast<std::uint64_t>(value));
}

inline void putF64(std::string& out, const double value) {
    std::array<char, 8> bytes{};
    std::memcpy(bytes.data(), &value, 8);
    out.append(bytes.data(), 8);
}

inline void putF32(std::string& out, const float value) {
    std::array<char, 4> bytes{};
    std::memcpy(bytes.data(), &value, 4);
    out.append(bytes.data(), 4);
}

struct Reader {
    std::span<const std::byte> bytes;
    std::size_t offset{0};

    void take(void* destination, const std::size_t count) {
        if (offset + count > bytes.size()) {
            throw TerrainTileFormatError("terrain tile data is truncated");
        }
        std::memcpy(destination, bytes.data() + offset, count);
        offset += count;
    }

    [[nodiscard]] std::uint32_t readU32() {
        std::uint32_t value = 0;
        take(&value, 4);
        return value;
    }

    [[nodiscard]] std::uint64_t readU64() {
        std::uint64_t value = 0;
        take(&value, 8);
        return value;
    }

    [[nodiscard]] std::int64_t readI64() { return static_cast<std::int64_t>(readU64()); }

    [[nodiscard]] double readF64() {
        double value = 0.0;
        take(&value, 8);
        return value;
    }
};

} // namespace detail

// Serializes one tile. Throws TerrainTileFormatError when the structure is
// inconsistent (never writes a silently "fixed" file).
[[nodiscard]] inline std::string encodeTerrainTile(const TerrainTileFile& tile) {
    static_assert(std::endian::native == std::endian::little,
        "terrain tile files are written in native little-endian layout");
    if (tile.datasetUuid.size() != 36) {
        throw TerrainTileFormatError("terrain tile dataset uuid must be canonical 36-char text");
    }
    if (tile.lods.size() != kTerrainTileLodCount) {
        throw TerrainTileFormatError("terrain tile must carry the full LOD pyramid");
    }
    for (std::uint32_t level = 0; level < tile.lods.size(); ++level) {
        const TerrainTileLodGrid& lod = tile.lods[level];
        if (lod.dim != terrainTileLodDim(level)) {
            throw TerrainTileFormatError("terrain tile LOD dim does not match its level");
        }
        if (lod.heights.size() != static_cast<std::size_t>(lod.dim) * lod.dim) {
            throw TerrainTileFormatError("terrain tile LOD height count does not match its dim");
        }
    }

    std::string out;
    out.reserve(64 + tile.lods.size() * (40 + tile.lods[0].heights.size() * 8));
    out.append(kTerrainTileMagic, 4);
    detail::putU32(out, tile.schemaVersion);
    detail::putU32(out, tile.generatorRevision);
    out.append(tile.datasetUuid);
    detail::putU64(out, tile.datasetRevision);
    detail::putI64(out, tile.chunkX);
    detail::putI64(out, tile.chunkY);
    detail::putU32(out, static_cast<std::uint32_t>(tile.lods.size()));
    detail::putU32(out, 0); // reserved

    for (const TerrainTileLodGrid& lod : tile.lods) {
        detail::putU32(out, lod.dim);
        detail::putF64(out, lod.originEasting);
        detail::putF64(out, lod.originNorthing);
        detail::putF64(out, lod.cellEasting);
        detail::putF64(out, lod.cellNorthing);
        detail::putF64(out, lod.minZ);
        detail::putF64(out, lod.maxZ);
        for (const double height : lod.heights) {
            detail::putF64(out, height);
        }
    }
    return out;
}

// Parses one tile and validates its self-description. Throws
// TerrainTileFormatError on any mismatch — a stale or corrupt tile is
// reported, never interpreted leniently.
[[nodiscard]] inline TerrainTileFile decodeTerrainTile(const std::span<const std::byte> bytes) {
    static_assert(std::endian::native == std::endian::little,
        "terrain tile files are read in native little-endian layout");
    if (bytes.size() < 4 + 4 + 4 + 36 + 8 + 8 + 8 + 4 + 4) {
        throw TerrainTileFormatError("terrain tile data is truncated");
    }
    if (std::memcmp(bytes.data(), kTerrainTileMagic, 4) != 0) {
        throw TerrainTileFormatError("terrain tile magic mismatch");
    }

    detail::Reader reader{bytes, 4};
    TerrainTileFile tile;
    tile.schemaVersion = reader.readU32();
    tile.generatorRevision = reader.readU32();
    if (tile.schemaVersion != kTerrainTileSchemaVersion) {
        throw TerrainTileFormatError("terrain tile schema version " + std::to_string(tile.schemaVersion)
            + " is not supported (expected " + std::to_string(kTerrainTileSchemaVersion) + ")");
    }
    std::array<char, 36> uuid{};
    reader.take(uuid.data(), 36);
    tile.datasetUuid.assign(uuid.data(), uuid.size());
    tile.datasetRevision = reader.readU64();
    tile.chunkX = reader.readI64();
    tile.chunkY = reader.readI64();
    const std::uint32_t lodCount = reader.readU32();
    (void)reader.readU32(); // reserved
    if (lodCount != kTerrainTileLodCount) {
        throw TerrainTileFormatError("terrain tile LOD count " + std::to_string(lodCount)
            + " is not supported (expected " + std::to_string(kTerrainTileLodCount) + ")");
    }

    tile.lods.resize(lodCount);
    for (std::uint32_t level = 0; level < lodCount; ++level) {
        TerrainTileLodGrid& lod = tile.lods[level];
        lod.dim = reader.readU32();
        lod.originEasting = reader.readF64();
        lod.originNorthing = reader.readF64();
        lod.cellEasting = reader.readF64();
        lod.cellNorthing = reader.readF64();
        lod.minZ = reader.readF64();
        lod.maxZ = reader.readF64();
        if (lod.dim != terrainTileLodDim(level)) {
            throw TerrainTileFormatError("terrain tile LOD dim does not match its level");
        }
        if (!(lod.cellEasting > 0.0) || !(lod.cellNorthing > 0.0)) {
            throw TerrainTileFormatError("terrain tile LOD cell size must be positive");
        }
        lod.heights.resize(static_cast<std::size_t>(lod.dim) * lod.dim);
        reader.take(lod.heights.data(), lod.heights.size() * 8);
    }
    return tile;
}

[[nodiscard]] inline TerrainTileFile decodeTerrainTileFromFile(const std::string& path) {
    std::ifstream input(std::filesystem::path(path), std::ios::binary);
    if (!input) {
        throw TerrainTileFormatError("terrain tile file cannot be opened: " + path);
    }
    std::vector<char> chars((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (input.bad()) {
        throw TerrainTileFormatError("terrain tile file read failed: " + path);
    }
    const auto* bytes = reinterpret_cast<const std::byte*>(chars.data());
    return decodeTerrainTile(std::span<const std::byte>(bytes, chars.size()));
}

inline void encodeTerrainTileToFile(const TerrainTileFile& tile, const std::string& path) {
    std::filesystem::path target(path);
    if (std::error_code ec; !target.is_absolute()) {
        target = std::filesystem::absolute(target, ec);
    }
    if (std::error_code ec; target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path(), ec);
    }
    // Atomic publication: write a temp sibling, then rename over the final
    // name so a reader never observes a half-written tile.
    const std::string tempPath = path + ".part";
    {
        std::ofstream output(std::filesystem::path(tempPath), std::ios::binary | std::ios::trunc);
        if (!output) {
            throw TerrainTileFormatError("terrain tile file cannot be created: " + tempPath);
        }
        const std::string encoded = encodeTerrainTile(tile);
        output.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
        output.flush();
        if (!output) {
            output.close();
            std::error_code ec;
            std::filesystem::remove(std::filesystem::path(tempPath), ec);
            throw TerrainTileFormatError("terrain tile file write failed: " + tempPath);
        }
    }
    std::error_code renameError;
    std::filesystem::rename(std::filesystem::path(tempPath), std::filesystem::path(path), renameError);
    if (renameError) {
        // Windows rename cannot overwrite; replace semantics via remove+rename.
        std::error_code removeError;
        std::filesystem::remove(std::filesystem::path(path), removeError);
        std::filesystem::rename(std::filesystem::path(tempPath), std::filesystem::path(path), renameError);
        if (renameError) {
            std::error_code cleanupError;
            std::filesystem::remove(std::filesystem::path(tempPath), cleanupError);
            throw TerrainTileFormatError("terrain tile file publish failed: " + path);
        }
    }
}

} // namespace infraforge::domain::terrain
