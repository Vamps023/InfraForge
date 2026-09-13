#pragma once

#include "infraforge/domain/world/ChunkGrid.hpp"
#include "infraforge/domain/world/EntityId.hpp"
#include "infraforge/domain/world/Invalidation.hpp"
#include "infraforge/domain/world/SpatialBounds.hpp"
#include "infraforge/domain/world/WorldPartitionError.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace infraforge::domain::world {

// Spatial consequences of one accepted index mutation, computed once by the
// index — the canonical chunk-diff mechanism. Consumers (terrain, roads,
// renderer streaming) must not repeat old/new bounds math:
//
// - previousChunks: cells covered by the old bounds; none for insert.
// - updatedChunks: cells covered by the new bounds; none for remove.
// - dirtyChunks: deterministic sorted union of both — moving or shrinking
//   an entity invalidates the cells it left as well as the cells it
//   entered (docs/02_DATA/WORLD_CHUNKS.md "Dirty tracking").
// - classes: invalidation classes declared for this mutation.
// - revision: monotonically increasing global index revision after the
//   mutation. This is a whole-index counter — generated chunk content must
//   version itself with SpatialIndex::lastAffectingRevision(chunk) instead,
//   so an unrelated edit elsewhere in the world cannot mark this chunk's
//   cache stale.
struct IndexMutation {
    EntityId entity{};
    std::vector<ChunkCoord> previousChunks;
    std::vector<ChunkCoord> updatedChunks;
    std::vector<ChunkCoord> dirtyChunks;
    InvalidationMask classes{};
    std::uint64_t revision{0};

    friend bool operator==(const IndexMutation&, const IndexMutation&) = default;
};

// Accumulates per-chunk invalidation classes across mutations — the single
// canonical dirty set handed to rebuild/streaming consumers. Deterministic
// ordered iteration (ChunkCoord row-major); only touched cells appear.
class ChunkDirtySet {
public:
    // Adds the mutation's classes to each of its dirty chunks.
    void absorb(const IndexMutation& mutation);

    [[nodiscard]] bool contains(const ChunkCoord chunk) const {
        return chunks_.find(chunk) != chunks_.end();
    }

    [[nodiscard]] std::optional<InvalidationMask> classesFor(const ChunkCoord chunk) const {
        const auto found = chunks_.find(chunk);
        if (found == chunks_.end()) {
            return std::nullopt;
        }
        return found->second;
    }

    [[nodiscard]] std::size_t size() const noexcept { return chunks_.size(); }
    [[nodiscard]] bool empty() const noexcept { return chunks_.empty(); }
    [[nodiscard]] const std::map<ChunkCoord, InvalidationMask>& chunks() const noexcept {
        return chunks_;
    }

    void clear() noexcept { chunks_.clear(); }

private:
    std::map<ChunkCoord, InvalidationMask> chunks_;
};

// Canonical large-world spatial index: maps stable entity identifiers to
// canonical spatial bounds and the logical chunk cells they cover.
//
// - Sparse: storage and query cost scale with occupied/query cells and
//   result size, never with total world extent; empty world regions cost
//   nothing.
// - Single-threaded: owned by the application executor thread like all
//   canonical project state; not internally synchronized.
// - The chunk-cell inverted-list implementation is an internal detail; the
//   public surface (bounds + chunk-diff mutations + queries) is
//   expressible over any partitioned index.
class SpatialIndex {
public:
    explicit SpatialIndex(ChunkGrid grid);

    [[nodiscard]] const ChunkGrid& grid() const noexcept { return grid_; }

    // Registers an entity with canonical bounds. Empty bounds register an
    // entity that temporarily has no spatial extent (occupies no cells).
    // The invalidation classes must be stated explicitly on every
    // mutation; pass an empty InvalidationMask only when the mutation
    // intentionally requires no generated work. Throws WorldPartitionError
    // (NullEntityId) for the reserved null identifier, (DuplicateEntity)
    // when the identifier is already tracked, (InvalidBounds) for
    // non-empty bounds with non-finite edges, and (CoordinateOutOfRange)
    // when bounds cannot be mapped to chunk cells.
    IndexMutation insert(EntityId entity, SpatialBounds bounds, InvalidationMask classes);

    // Replaces the canonical bounds of a tracked entity; the mutation
    // carries the old and new cell coverage. Throws WorldPartitionError
    // (UnknownEntity) when the identifier is not tracked, plus the insert
    // input errors for the new bounds.
    IndexMutation update(EntityId entity, SpatialBounds newBounds, InvalidationMask classes);

    // Removes an entity; previousChunks carries the cells it occupied so
    // content it vacated is invalidated. Throws WorldPartitionError
    // (UnknownEntity) when the identifier is not tracked.
    IndexMutation remove(EntityId entity, InvalidationMask classes);

    [[nodiscard]] std::optional<SpatialBounds> boundsOf(EntityId entity) const;
    [[nodiscard]] bool contains(EntityId entity) const {
        return entities_.find(entity) != entities_.end();
    }
    [[nodiscard]] std::size_t entityCount() const noexcept { return entities_.size(); }

    // Distinct occupied cells — the architectural sparsity probe: this
    // counts occupied cells only, never the dense world matrix.
    [[nodiscard]] std::size_t occupiedChunkCount() const noexcept {
        return chunkEntities_.size();
    }

    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

    // Last index revision whose mutations affected this cell's content
    // (insert/update/remove of an entity covering it). Generated chunk
    // content must record this value as its cache sourceRevision at
    // generation time; a later comparison against the chunk's current
    // value marks exactly the edited cells stale while unrelated chunks
    // stay current — a distant one-entity edit can never invalidate the
    // whole world. 0 means no mutation ever affected the cell (it has no
    // derived content to invalidate).
    [[nodiscard]] std::uint64_t lastAffectingRevision(ChunkCoord chunk) const {
        const auto found = chunkRevisions_.find(chunk);
        return found == chunkRevisions_.end() ? 0 : found->second;
    }

    // Cells intersecting the closed bounds (grid delegation, deterministic
    // row-major order).
    [[nodiscard]] std::vector<ChunkCoord> chunksIntersecting(SpatialBounds bounds) const {
        return grid_.chunksIntersecting(bounds);
    }

    // Tracked entities whose canonical bounds intersect the closed query
    // bounds, sorted by EntityId. The per-cell inverted lists over-select
    // candidates; every candidate is re-checked against its real bounds.
    [[nodiscard]] std::vector<EntityId> entitiesIntersecting(SpatialBounds bounds) const;

    // Tracked entities whose bounds cover any part of the cell, sorted by
    // EntityId.
    [[nodiscard]] std::vector<EntityId> entitiesInChunk(ChunkCoord chunk) const;

    // Cells a tracked entity currently covers, sorted row-major; empty for
    // unknown entities and entities without spatial extent.
    [[nodiscard]] std::vector<ChunkCoord> chunksOf(EntityId entity) const;

private:
    struct Entry {
        SpatialBounds bounds;
        std::vector<ChunkCoord> chunks;
    };

    ChunkGrid grid_;
    std::unordered_map<EntityId, Entry, EntityIdHash> entities_;
    std::unordered_map<ChunkCoord, std::vector<EntityId>, ChunkCoordHash> chunkEntities_;
    // Sparse: one entry per cell ever touched by a mutation — never the
    // dense world matrix.
    std::unordered_map<ChunkCoord, std::uint64_t, ChunkCoordHash> chunkRevisions_;
    std::uint64_t revision_{0};
};

} // namespace infraforge::domain::world
