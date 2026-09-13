#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

namespace infraforge::domain::world {

// Failure taxonomy of the world-partition domain. This is in-memory domain
// logic: these codes report contract violations loudly (bad input, illegal
// lifecycle use) instead of clamping, skipping, or silently falling back.
enum class WorldPartitionErrorCode : std::uint8_t {
    // Chunk size is non-finite or not positive.
    InvalidChunkSize,
    // The resolved canonical linear unit cannot anchor chunk math
    // (non-finite or non-positive metres-per-unit factor).
    InvalidLinearUnit,
    // A non-empty bounds carries a non-finite edge.
    InvalidBounds,
    // A coordinate cannot be mapped to a chunk index: non-finite, beyond
    // the exactly representable chunk-index range, or a bounds spanning
    // more chunk cells than can be enumerated.
    CoordinateOutOfRange,
    // The reserved all-zero entity identifier was used in a mutation.
    NullEntityId,
    // A mutation referenced an entity the index does not track.
    UnknownEntity,
    // A mutation registered an entity identifier that is already tracked.
    DuplicateEntity,
    // A chunk residency transition violated the documented lifecycle.
    IllegalResidencyTransition,
};

class WorldPartitionError : public std::runtime_error {
public:
    WorldPartitionError(WorldPartitionErrorCode code, std::string message)
        : std::runtime_error(std::move(message)),
          code_(code) {}

    [[nodiscard]] WorldPartitionErrorCode code() const noexcept { return code_; }

private:
    WorldPartitionErrorCode code_;
};

} // namespace infraforge::domain::world
