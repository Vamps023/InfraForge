#pragma once

#include "infraforge/domain/geo/ProjectGeoreference.hpp"
#include "infraforge/domain/world/ChunkGrid.hpp"
#include "infraforge/domain/world/SpatialIndex.hpp"
#include "infraforge/domain/world/SpatialBounds.hpp"

#include <optional>
#include <vector>

namespace infraforge::application {

// In-memory large-world partition state of the open project session
// (ADR-0008): the logical chunk grid built from the project's canonical
// linear unit plus the canonical spatial index. This is the single shared
// partition surface for the spatial domains — terrain registers its
// datasets here, roads and later domains register theirs; nobody maintains
// a second dirty-region mechanism.
//
// The index is rebuilt from persisted canonical records on project open
// (it is derived index state, not persisted itself). Owned by the
// application executor thread like all canonical state; not internally
// synchronized.
class WorldState {
public:
    // Rebuilds the partition for a (re)opened project. The grid uses the
    // default physical 1 km chunk edge converted through the project's
    // canonical linear unit, so metre and non-metre projects get physically
    // identical cells (docs/02_DATA/WORLD_CHUNKS.md).
    void resetForProject(const domain::geo::ProjectGeoreference& project);

    [[nodiscard]] bool isReady() const noexcept { return index_.has_value(); }
    [[nodiscard]] const domain::world::ChunkGrid& grid() const;

    // Registers an entity with canonical bounds under an explicit
    // invalidation-class mask and returns the canonical chunk diff. The
    // caller applies the returned mutation to its own dirty set or
    // generation bookkeeping — this class never triggers rebuilds itself.
    [[nodiscard]] domain::world::IndexMutation insert(
        domain::world::EntityId entity, domain::world::SpatialBounds bounds,
        domain::world::InvalidationMask classes);
    [[nodiscard]] domain::world::IndexMutation update(
        domain::world::EntityId entity, domain::world::SpatialBounds newBounds,
        domain::world::InvalidationMask classes);
    [[nodiscard]] domain::world::IndexMutation remove(
        domain::world::EntityId entity, domain::world::InvalidationMask classes);

    // Dependency-scoped per-chunk content generation (the cache freshness
    // source for chunk-scoped derived content such as terrain tiles).
    [[nodiscard]] std::uint64_t lastAffectingRevision(
        domain::world::ChunkCoord chunk, domain::world::InvalidationMask dependencies) const;

    [[nodiscard]] std::optional<domain::world::SpatialBounds> boundsOf(
        domain::world::EntityId entity) const;
    [[nodiscard]] std::vector<domain::world::ChunkCoord> chunksIntersecting(
        domain::world::SpatialBounds bounds) const;

private:
    std::optional<domain::world::ChunkGrid> grid_;
    std::optional<domain::world::SpatialIndex> index_;
};

} // namespace infraforge::application
