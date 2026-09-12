#pragma once

#include "infraforge/domain/world/SpatialBounds.hpp"

#include <cstdint>
#include <cstddef>
#include <functional>
#include <vector>

namespace infraforge::domain::world {

// Logical chunk cell coordinate: the floor-divided position of a square
// cell of the world partition grid (docs/02_DATA/WORLD_CHUNKS.md,
// ADR-0008). Chunk identity is a pure spatial partition index over
// project-global space — it is never an entity identity, and entity ↔ chunk
// membership is derived index state, not ownership.
struct ChunkCoord {
    std::int64_t x{0};
    std::int64_t y{0};

    friend bool operator==(const ChunkCoord&, const ChunkCoord&) = default;
    friend auto operator<=>(const ChunkCoord&, const ChunkCoord&) = default;
};

struct ChunkCoordHash {
    [[nodiscard]] std::size_t operator()(const ChunkCoord& chunk) const noexcept {
        std::uint64_t mixed = static_cast<std::uint64_t>(chunk.x) * 0x9e3779b97f4a7c15ULL
            ^ (static_cast<std::uint64_t>(chunk.y) + 0x9e3779b97f4a7c15ULL
                + (static_cast<std::uint64_t>(chunk.y) << 6)
                + (static_cast<std::uint64_t>(chunk.y) >> 2));
        mixed ^= mixed >> 30;
        mixed *= 0xbf58476d1ce4e5b9ULL;
        mixed ^= mixed >> 27;
        mixed *= 0x94d049bb133111ebULL;
        mixed ^= mixed >> 31;
        return static_cast<std::size_t>(mixed);
    }
};

// Default logical chunk edge in the canonical project-global linear unit
// (1 km for the canonical metre unit). The single definition point — call
// sites reference this constant or a configured ChunkGridConfig value and
// never hard-code the size.
inline constexpr double defaultChunkSize = 1000.0;

// Validated-at-construction configuration of the logical chunk grid.
struct ChunkGridConfig {
    // Positive finite cell edge length in the canonical linear unit of
    // project-global space.
    double chunkSize{defaultChunkSize};

    friend bool operator==(const ChunkGridConfig&, const ChunkGridConfig&) = default;
};

// Pure chunking math over canonical project-global coordinates: floor
// division to cell coordinates, conservative closed-bounds cell
// enumeration, and cell footprints.
//
// Boundary semantics (deterministic, tested):
// - Cell k covers the half-open footprint [k*size, (k+1)*size) on an axis;
//   a position exactly on a cell edge belongs to the higher cell (floor
//   division, never truncation toward zero — negative coordinates floor).
// - A bounds lying exactly on a cell edge touches both adjacent cells:
//   chunksIntersecting enumerates every cell whose footprint intersects
//   the closed bounds, so invalidation is conservative (never misses
//   content) while a position maps to exactly one cell.
//
// Immutable value type; safe to share across threads by const-ness.
class ChunkGrid {
public:
    // Largest chunk index the partition computes exactly: binary doubles
    // represent every integer up to 2^53, so beyond this neighbouring
    // world coordinates could not be distinguished anyway.
    static constexpr double maxChunkIndex = 9007199254740992.0; // 2^53

    // Upper bound on cells enumerated for one bounds query; a bounds
    // spanning more cells than this is rejected instead of hanging the
    // caller (an entity covering more than 1000 x 1000 cells of the
    // configured grid is a modeling error, not a supported shape).
    static constexpr std::size_t maxEnumeratedChunks = 1000000;

    ChunkGrid() = default;
    // Throws WorldPartitionError (InvalidChunkSize) for non-finite or
    // non-positive sizes.
    explicit ChunkGrid(ChunkGridConfig config);

    [[nodiscard]] const ChunkGridConfig& config() const noexcept { return config_; }
    [[nodiscard]] double chunkSize() const noexcept { return config_.chunkSize; }

    // Floor-divided cell of a canonical horizontal position. Throws
    // WorldPartitionError (CoordinateOutOfRange) for non-finite input or
    // coordinates beyond the exactly representable index range.
    [[nodiscard]] ChunkCoord chunkAt(double easting, double northing) const;
    [[nodiscard]] ChunkCoord chunkAt(const geo::ProjectGlobalPosition& position) const;

    // Every cell whose footprint intersects the closed bounds, in
    // deterministic lexicographic order (ascending x, then ascending y).
    // Empty bounds select no cells. Throws WorldPartitionError
    // (CoordinateOutOfRange) for non-representable coordinates or bounds
    // spanning more than maxEnumeratedChunks cells.
    [[nodiscard]] std::vector<ChunkCoord> chunksIntersecting(SpatialBounds bounds) const;

    // Closed footprint of one cell: [x*size, (x+1)*size] x
    // [y*size, (y+1)*size] — note the closed upper edge so that
    // chunkBounds(cell).contains(...) matches chunkAt boundary semantics
    // for the shared edge position.
    [[nodiscard]] SpatialBounds chunkBounds(ChunkCoord chunk) const;

private:
    [[nodiscard]] std::int64_t axisIndex(double value) const;

    ChunkGridConfig config_{};
};

} // namespace infraforge::domain::world
