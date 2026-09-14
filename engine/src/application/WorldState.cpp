#include "infraforge/application/WorldState.hpp"

#include "infraforge/domain/world/WorldPartitionError.hpp"

#include <stdexcept>

namespace infraforge::application {

using domain::world::IndexMutation;
using domain::world::InvalidationMask;
using domain::world::SpatialBounds;
using domain::world::ChunkCoord;
using domain::world::EntityId;

void WorldState::resetForProject(const domain::geo::ProjectGeoreference& project) {
    grid_ = domain::world::ChunkGrid::fromMetreEdge(project.linearUnit);
    index_.emplace(*grid_);
}

const domain::world::ChunkGrid& WorldState::grid() const {
    if (!grid_.has_value()) {
        throw std::logic_error("world state is not ready (no open project)");
    }
    return *grid_;
}

IndexMutation WorldState::insert(const EntityId entity, const SpatialBounds bounds, const InvalidationMask classes) {
    if (!index_.has_value()) {
        throw std::logic_error("world state is not ready (no open project)");
    }
    return index_->insert(entity, bounds, classes);
}

IndexMutation WorldState::update(
    const EntityId entity, const SpatialBounds newBounds, const InvalidationMask classes) {
    if (!index_.has_value()) {
        throw std::logic_error("world state is not ready (no open project)");
    }
    return index_->update(entity, newBounds, classes);
}

IndexMutation WorldState::remove(const EntityId entity, const InvalidationMask classes) {
    if (!index_.has_value()) {
        throw std::logic_error("world state is not ready (no open project)");
    }
    return index_->remove(entity, classes);
}

std::uint64_t WorldState::lastAffectingRevision(const ChunkCoord chunk, const InvalidationMask dependencies) const {
    if (!index_.has_value()) {
        throw std::logic_error("world state is not ready (no open project)");
    }
    return index_->lastAffectingRevision(chunk, dependencies);
}

std::optional<SpatialBounds> WorldState::boundsOf(const EntityId entity) const {
    if (!index_.has_value()) {
        throw std::logic_error("world state is not ready (no open project)");
    }
    return index_->boundsOf(entity);
}

std::vector<ChunkCoord> WorldState::chunksIntersecting(const SpatialBounds bounds) const {
    if (!index_.has_value()) {
        throw std::logic_error("world state is not ready (no open project)");
    }
    return index_->chunksIntersecting(bounds);
}

} // namespace infraforge::application
