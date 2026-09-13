#pragma once

#include "infraforge/domain/world/ChunkGrid.hpp"

#include <cstdint>
#include <cstddef>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace infraforge::domain::world {

// Renderer-facing residency lifecycle of derived chunk content
// (docs/02_DATA/WORLD_CHUNKS.md "Streaming"). Transient derived state owned
// by the streaming pipeline — never canonical project truth, never
// persisted as part of the project model, and never required to be resident
// for the whole world.
enum class ChunkResidencyState : std::uint8_t {
    // Absent: no derived content exists for the cell.
    Unloaded,
    // Generation/upload of derived content is in flight.
    Loading,
    // Derived content is ready for rendering.
    Resident,
    // Resident content was invalidated by a dirty-chunk change and must be
    // rebuilt before it may render again.
    Stale,
    // Eviction in progress; content is being released.
    Evicting,
};

[[nodiscard]] std::string_view chunkResidencyStateName(ChunkResidencyState state) noexcept;
[[nodiscard]] std::optional<ChunkResidencyState> chunkResidencyStateFromName(
    std::string_view name) noexcept;

// Validates and tracks residency transitions per chunk cell. The renderer
// consumes this state; the vocabulary and transition rules live here, in
// the domain, so renderer code cannot invent its own parallel residency
// model (ADR-0008). Single consumer thread; not internally synchronized.
//
// Sparse: only cells that actually transitioned are stored — querying the
// state of untouched cells (the overwhelmingly common case in a 100 km
// world) neither allocates nor iterates them.
class ChunkResidencyTracker {
public:
    // Current lifecycle state; Unloaded for never-touched cells.
    [[nodiscard]] ChunkResidencyState stateOf(ChunkCoord chunk) const noexcept;

    // Applies a lifecycle transition. The documented lifecycle is:
    //
    //   Unloaded -> Loading                    (request generation/upload)
    //   Loading  -> Resident                   (content ready)
    //   Loading  -> Unloaded                   (generation failed/cancelled;
    //                                           the failure itself is
    //                                           reported through diagnostics)
    //   Resident -> Stale                      (dirty-chunk invalidation)
    //   Resident -> Evicting                   (working-set eviction)
    //   Stale    -> Loading                    (rebuild requested)
    //   Stale    -> Evicting                   (drop instead of rebuild)
    //   Evicting -> Unloaded                   (release complete)
    //
    // Any other edge — including a state transitioning to itself — throws
    // WorldPartitionError (IllegalResidencyTransition): residency bugs are
    // pipeline defects, never silently absorbed.
    void transition(ChunkCoord chunk, ChunkResidencyState target);

    // True only for Resident cells; Stale content is not render-ready.
    [[nodiscard]] bool isResident(ChunkCoord chunk) const noexcept {
        return stateOf(chunk) == ChunkResidencyState::Resident;
    }

    [[nodiscard]] std::size_t trackedChunkCount() const noexcept { return states_.size(); }

private:
    [[nodiscard]] static bool allowed(
        ChunkResidencyState from, ChunkResidencyState to) noexcept;

    std::unordered_map<ChunkCoord, ChunkResidencyState, ChunkCoordHash> states_;
};

} // namespace infraforge::domain::world
