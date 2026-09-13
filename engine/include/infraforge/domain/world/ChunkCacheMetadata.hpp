#pragma once

#include <cstdint>

namespace infraforge::domain::world {

// Schema version of the chunk generated-cache metadata record itself
// (docs/02_DATA/WORLD_CHUNKS.md "Generated data"). Bumping this version
// invalidates every existing cache record: mismatched-schema content is
// never interpreted, only rebuilt.
inline constexpr std::uint32_t currentChunkCacheSchemaVersion{1};

// Versioned metadata attached to derived chunk content (terrain tiles,
// road render meshes, simulation spatial data, renderer acceleration
// structures). Purely descriptive provenance: caches are always
// reconstructable from canonical project/domain data and are never a
// storage location for canonical truth — no entity exists only because a
// chunk cache contains it.
struct ChunkCacheMetadata {
    // Cache record schema; see currentChunkCacheSchemaVersion.
    std::uint32_t schemaVersion{currentChunkCacheSchemaVersion};
    // Identity of the generator (tool/algorithm revision) that produced
    // the content. Generator changes mark old output stale even when the
    // canonical inputs are unchanged.
    std::uint64_t generatorRevision{0};
    // Canonical-state revision the content was derived from. Chunk-scoped
    // generated content records the dependency-scoped per-chunk generation
    // SpatialIndex::lastAffectingRevision(chunk, dependencyMask) captured
    // at generation time, where the mask is exactly the invalidation
    // classes this content depends on: a Material-only edit then never
    // stales a Terrain-only cache in the same chunk. The dependency-less
    // overload lastAffectingRevision(chunk) (latest across all classes)
    // is for consumers that count any invalidation class, such as
    // renderer residency staleness. Never use the global
    // SpatialIndex::revision() here: every unrelated mutation advances it,
    // which would drag the whole world back to stale on each local edit.
    std::uint64_t sourceRevision{0};

    friend bool operator==(const ChunkCacheMetadata&, const ChunkCacheMetadata&) = default;
};

// What a consumer currently requires of derived chunk content for one
// chunk cell.
struct ChunkCacheExpectation {
    std::uint32_t schemaVersion{currentChunkCacheSchemaVersion};
    std::uint64_t generatorRevision{0};
    std::uint64_t sourceRevision{0};

    friend bool operator==(const ChunkCacheExpectation&, const ChunkCacheExpectation&) = default;
};

// True only when the cached content exactly matches the current
// expectation; any schema, generator, or source drift marks the content
// stale so it is rebuilt from canonical data instead of reused.
[[nodiscard]] inline bool isCurrent(
    const ChunkCacheMetadata& cached, const ChunkCacheExpectation& expected) noexcept {
    return cached.schemaVersion == expected.schemaVersion
        && cached.generatorRevision == expected.generatorRevision
        && cached.sourceRevision == expected.sourceRevision;
}

} // namespace infraforge::domain::world
