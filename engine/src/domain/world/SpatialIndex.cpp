#include "infraforge/domain/world/SpatialIndex.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace infraforge::domain::world {

namespace {

[[noreturn]] void throwPartitionError(const WorldPartitionErrorCode code, std::string message) {
    throw WorldPartitionError{code, std::move(message)};
}

void validateBounds(const SpatialBounds bounds) {
    if (!bounds.isFinite()) {
        throwPartitionError(
            WorldPartitionErrorCode::InvalidBounds, "spatial bounds must be finite");
    }
}

// Sorted lexicographic (x, then y) union of two sorted cell lists.
[[nodiscard]] std::vector<ChunkCoord> sortedChunkUnion(
    const std::vector<ChunkCoord>& left, const std::vector<ChunkCoord>& right) {
    std::vector<ChunkCoord> united;
    united.reserve(left.size() + right.size());
    std::set<ChunkCoord> unique;
    unique.insert(left.begin(), left.end());
    unique.insert(right.begin(), right.end());
    united.assign(unique.begin(), unique.end());
    return united;
}

} // namespace

void ChunkDirtySet::absorb(const IndexMutation& mutation) {
    if (mutation.classes.empty()) {
        // An empty mask is the explicit "no generated work" declaration:
        // the mutation contributes nothing to per-chunk invalidation state.
        return;
    }
    for (const auto chunk : mutation.dirtyChunks) {
        chunks_[chunk] |= mutation.classes;
    }
}

SpatialIndex::SpatialIndex(ChunkGrid grid)
    : grid_(std::move(grid)) {}

void SpatialIndex::advanceContentGenerations(
    const std::vector<ChunkCoord>& chunks, const InvalidationMask classes) {
    if (classes.empty()) {
        // Declared "no generated work": no content generation moves, so
        // no dependent cache can be staled by this mutation.
        return;
    }
    for (const auto chunk : chunks) {
        auto& generations = chunkGenerations_[chunk];
        for (auto index = std::size_t{0}; index < invalidationClassCount(); ++index) {
            const auto invalidationClass = static_cast<InvalidationClass>(index);
            if (classes.contains(invalidationClass)) {
                generations[index] = revision_;
            }
        }
    }
}

IndexMutation SpatialIndex::insert(
    const EntityId entity, const SpatialBounds bounds, const InvalidationMask classes) {
    if (entity.isNull()) {
        throwPartitionError(
            WorldPartitionErrorCode::NullEntityId, "cannot insert the reserved null entity id");
    }
    if (contains(entity)) {
        throwPartitionError(
            WorldPartitionErrorCode::DuplicateEntity, "entity is already tracked by the index");
    }
    validateBounds(bounds);

    auto chunks = grid_.chunksIntersecting(bounds);
    for (const auto chunk : chunks) {
        chunkEntities_[chunk].push_back(entity);
    }
    entities_.emplace(entity, Entry{bounds, chunks});
    ++revision_;
    advanceContentGenerations(chunks, classes);

    IndexMutation mutation;
    mutation.entity = entity;
    mutation.updatedChunks = std::move(chunks);
    mutation.dirtyChunks = mutation.updatedChunks;
    mutation.classes = classes;
    mutation.revision = revision_;
    return mutation;
}

IndexMutation SpatialIndex::update(
    const EntityId entity, const SpatialBounds newBounds, const InvalidationMask classes) {
    const auto found = entities_.find(entity);
    if (found == entities_.end()) {
        throwPartitionError(
            WorldPartitionErrorCode::UnknownEntity, "cannot update an untracked entity");
    }
    validateBounds(newBounds);

    auto previousChunks = found->second.chunks;
    auto newChunks = grid_.chunksIntersecting(newBounds);
    for (const auto chunk : previousChunks) {
        const auto cell = chunkEntities_.find(chunk);
        if (cell == chunkEntities_.end()) {
            continue;
        }
        auto& members = cell->second;
        const auto member = std::find(members.begin(), members.end(), entity);
        if (member != members.end()) {
            members.erase(member);
        }
        if (members.empty()) {
            chunkEntities_.erase(cell);
        }
    }
    for (const auto chunk : newChunks) {
        chunkEntities_[chunk].push_back(entity);
    }

    found->second.bounds = newBounds;
    found->second.chunks = newChunks;
    ++revision_;

    IndexMutation mutation;
    mutation.entity = entity;
    mutation.previousChunks = std::move(previousChunks);
    mutation.updatedChunks = std::move(newChunks);
    mutation.dirtyChunks = sortedChunkUnion(mutation.previousChunks, mutation.updatedChunks);
    mutation.classes = classes;
    mutation.revision = revision_;
    // Exactly the dirty cells — old and new coverage — get their declared
    // content generations advanced; unrelated cells keep theirs.
    advanceContentGenerations(mutation.dirtyChunks, classes);
    return mutation;
}

IndexMutation SpatialIndex::remove(const EntityId entity, const InvalidationMask classes) {
    const auto found = entities_.find(entity);
    if (found == entities_.end()) {
        throwPartitionError(
            WorldPartitionErrorCode::UnknownEntity, "cannot remove an untracked entity");
    }

    auto previousChunks = found->second.chunks;
    for (const auto chunk : previousChunks) {
        const auto cell = chunkEntities_.find(chunk);
        if (cell == chunkEntities_.end()) {
            continue;
        }
        auto& members = cell->second;
        const auto member = std::find(members.begin(), members.end(), entity);
        if (member != members.end()) {
            members.erase(member);
        }
        if (members.empty()) {
            chunkEntities_.erase(cell);
        }
    }
    entities_.erase(found);
    ++revision_;

    IndexMutation mutation;
    mutation.entity = entity;
    mutation.previousChunks = previousChunks;
    mutation.dirtyChunks = std::move(previousChunks);
    mutation.classes = classes;
    mutation.revision = revision_;
    advanceContentGenerations(mutation.dirtyChunks, classes);
    return mutation;
}

std::optional<SpatialBounds> SpatialIndex::boundsOf(const EntityId entity) const {
    const auto found = entities_.find(entity);
    if (found == entities_.end()) {
        return std::nullopt;
    }
    return found->second.bounds;
}

std::uint64_t SpatialIndex::lastAffectingRevision(
    const ChunkCoord chunk, const InvalidationMask dependencies) const {
    if (dependencies.empty()) {
        // A consumer depending on nothing is never stale.
        return 0;
    }
    const auto found = chunkGenerations_.find(chunk);
    if (found == chunkGenerations_.end()) {
        return 0;
    }
    std::uint64_t latest = 0;
    for (auto index = std::size_t{0}; index < invalidationClassCount(); ++index) {
        if (dependencies.contains(static_cast<InvalidationClass>(index))) {
            latest = std::max(latest, found->second[index]);
        }
    }
    return latest;
}

std::uint64_t SpatialIndex::lastAffectingRevision(const ChunkCoord chunk) const {
    const auto found = chunkGenerations_.find(chunk);
    if (found == chunkGenerations_.end()) {
        return 0;
    }
    return *std::max_element(found->second.begin(), found->second.end());
}

std::vector<EntityId> SpatialIndex::entitiesIntersecting(const SpatialBounds bounds) const {
    if (bounds.isEmpty()) {
        return {};
    }
    std::set<EntityId> candidates;
    for (const auto chunk : grid_.chunksIntersecting(bounds)) {
        const auto cell = chunkEntities_.find(chunk);
        if (cell == chunkEntities_.end()) {
            continue;
        }
        candidates.insert(cell->second.begin(), cell->second.end());
    }
    std::vector<EntityId> matches;
    matches.reserve(candidates.size());
    for (const auto candidate : candidates) {
        const auto& entry = entities_.at(candidate);
        if (entry.bounds.intersects(bounds)) {
            matches.push_back(candidate);
        }
    }
    // std::set iteration plus push-back already yields ascending EntityId.
    return matches;
}

std::vector<EntityId> SpatialIndex::entitiesInChunk(const ChunkCoord chunk) const {
    const auto cell = chunkEntities_.find(chunk);
    if (cell == chunkEntities_.end()) {
        return {};
    }
    std::vector<EntityId> matches{cell->second.begin(), cell->second.end()};
    std::sort(matches.begin(), matches.end());
    return matches;
}

std::vector<ChunkCoord> SpatialIndex::chunksOf(const EntityId entity) const {
    const auto found = entities_.find(entity);
    if (found == entities_.end()) {
        return {};
    }
    auto chunks = found->second.chunks;
    std::sort(chunks.begin(), chunks.end());
    return chunks;
}

} // namespace infraforge::domain::world
