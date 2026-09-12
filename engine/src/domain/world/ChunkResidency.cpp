#include "infraforge/domain/world/ChunkResidency.hpp"

#include "infraforge/domain/world/WorldPartitionError.hpp"

#include <stdexcept>
#include <string>
#include <tuple>

namespace infraforge::domain::world {

std::string_view chunkResidencyStateName(const ChunkResidencyState state) noexcept {
    switch (state) {
    case ChunkResidencyState::Unloaded:
        return "unloaded";
    case ChunkResidencyState::Loading:
        return "loading";
    case ChunkResidencyState::Resident:
        return "resident";
    case ChunkResidencyState::Stale:
        return "stale";
    case ChunkResidencyState::Evicting:
        return "evicting";
    }
    return "";
}

std::optional<ChunkResidencyState> chunkResidencyStateFromName(const std::string_view name) noexcept {
    static constexpr ChunkResidencyState kStates[]{
        ChunkResidencyState::Unloaded,
        ChunkResidencyState::Loading,
        ChunkResidencyState::Resident,
        ChunkResidencyState::Stale,
        ChunkResidencyState::Evicting,
    };
    for (const auto state : kStates) {
        if (chunkResidencyStateName(state) == name) {
            return state;
        }
    }
    return std::nullopt;
}

ChunkResidencyState ChunkResidencyTracker::stateOf(const ChunkCoord chunk) const noexcept {
    const auto found = states_.find(chunk);
    return found == states_.end() ? ChunkResidencyState::Unloaded : found->second;
}

void ChunkResidencyTracker::transition(const ChunkCoord chunk, const ChunkResidencyState target) {
    const auto from = stateOf(chunk);
    if (!allowed(from, target)) {
        throw WorldPartitionError{
            WorldPartitionErrorCode::IllegalResidencyTransition,
            "illegal chunk residency transition " + std::string{chunkResidencyStateName(from)}
                + " -> " + std::string{chunkResidencyStateName(target)}};
    }
    states_[chunk] = target;
    // A cell back at Unloaded carries no further derived state; dropping it
    // keeps the tracker sparse and makes "untouched" and "released" cells
    // indistinguishable by design.
    if (target == ChunkResidencyState::Unloaded) {
        states_.erase(chunk);
    }
}

bool ChunkResidencyTracker::allowed(
    const ChunkResidencyState from, const ChunkResidencyState to) noexcept {
    switch (from) {
    case ChunkResidencyState::Unloaded:
        return to == ChunkResidencyState::Loading;
    case ChunkResidencyState::Loading:
        return to == ChunkResidencyState::Resident || to == ChunkResidencyState::Unloaded;
    case ChunkResidencyState::Resident:
        return to == ChunkResidencyState::Stale || to == ChunkResidencyState::Evicting;
    case ChunkResidencyState::Stale:
        return to == ChunkResidencyState::Loading || to == ChunkResidencyState::Evicting;
    case ChunkResidencyState::Evicting:
        return to == ChunkResidencyState::Unloaded;
    }
    return false;
}

} // namespace infraforge::domain::world
