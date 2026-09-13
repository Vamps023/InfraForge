#pragma once

#include "infraforge/domain/geo/ProjectGeoreference.hpp"
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

// Default logical chunk edge in **physical metres** (1 km, per
// docs/02_DATA/WORLD_CHUNKS.md). This is the single definition point of the
// physical default — call sites never hard-code the size. Project-global
// coordinates are expressed in the project's canonical linear unit
// (ADR-0007), so the metre edge must be converted through the resolved
// linear unit (see ChunkGrid::fromMetreEdge); dividing canonical
// coordinates by 1000 directly would silently produce ~305 m cells for a
// US-survey-foot project.
inline constexpr double defaultChunkEdgeMetres = 1000.0;

// Edge length of one chunk cell expressed in the canonical linear unit of
// project-global space. There is deliberately no default value: a grid edge
// in project units is meaningless without knowing the project's unit, so
// callers construct a grid through ChunkGrid::fromMetreEdge (the production
// path) or state the project-unit edge explicitly.
struct ChunkGridConfig {
    double chunkSize;

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
    // Largest chunk index the partition computes exactly and whose cell
    // footprint stays representable: binary doubles represent every integer
    // up to 2^53, and at exactly 2^53 the footprint's upper edge (k + 1)
    // would round back down to k and collapse the cell to zero width — so
    // the symmetric supported range is ±(2^53 - 1).
    static constexpr double maxChunkIndex = 9007199254740991.0; // 2^53 - 1

    // Upper bound on cells enumerated for one bounds query; a bounds
    // spanning more cells than this is rejected instead of hanging the
    // caller (an entity covering more than 1000 x 1000 cells of the
    // configured grid is a modeling error, not a supported shape).
    static constexpr std::size_t maxEnumeratedChunks = 1000000;

    // Production construction path: converts a physically specified edge
    // (metres) into the canonical project-global unit through the resolved
    // linear unit of the project georeference. A 1 km edge yields cells
    // that are physically 1 km regardless of whether the project works in
    // metres, US survey feet, or any other linear unit. Throws
    // WorldPartitionError (InvalidLinearUnit) for a non-finite/non-positive
    // unit factor and (InvalidChunkSize) for a non-finite/non-positive edge
    // or a project-unit size that is not representable.
    [[nodiscard]] static ChunkGrid fromMetreEdge(
        const geo::ResolvedUnit& linearUnit, double edgeMetres = defaultChunkEdgeMetres);

    // Explicit project-unit construction. Throws WorldPartitionError
    // (InvalidChunkSize) for non-finite or non-positive sizes.
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
