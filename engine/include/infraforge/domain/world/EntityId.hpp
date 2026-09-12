#pragma once

#include <cstddef>
#include <cstdint>

namespace infraforge::domain::world {

// Stable canonical entity identifier (TRD "Canonical identifiers"):
// 128-bit UUID-compatible value minted by the entity's owning domain
// service. A pure identity token — it carries no spatial meaning, and chunk
// membership is derived index state, never part of identity (ADR-0008).
struct EntityId {
    std::uint64_t high{0};
    std::uint64_t low{0};

    // The all-zero identifier is reserved and never minted; it exists only
    // so default construction is defined, and the spatial index rejects it.
    [[nodiscard]] bool isNull() const noexcept { return high == 0 && low == 0; }

    friend bool operator==(const EntityId&, const EntityId&) = default;
    friend auto operator<=>(const EntityId&, const EntityId&) = default;
};

struct EntityIdHash {
    [[nodiscard]] std::size_t operator()(const EntityId& id) const noexcept {
        // splitmix64 finalizer over a combining step, so (high, low) pairs
        // that differ in either half distribute independently.
        std::uint64_t mixed = id.high ^ (id.low + 0x9e3779b97f4a7c15ULL
            + (id.high << 6) + (id.high >> 2));
        mixed ^= mixed >> 30;
        mixed *= 0xbf58476d1ce4e5b9ULL;
        mixed ^= mixed >> 27;
        mixed *= 0x94d049bb133111ebULL;
        mixed ^= mixed >> 31;
        return static_cast<std::size_t>(mixed);
    }
};

} // namespace infraforge::domain::world
