#include <doctest/doctest.h>

#include "TestHelpers.hpp"
#include "infraforge/domain/world/ChunkResidency.hpp"
#include "infraforge/domain/world/WorldPartitionError.hpp"

#include <cstdint>

namespace world = infraforge::domain::world;

using infraforge::testhelpers::captureException;

TEST_SUITE("chunk residency lifecycle") {
    TEST_CASE("state names round-trip") {
        for (const auto state :
            {world::ChunkResidencyState::Unloaded, world::ChunkResidencyState::Loading,
                world::ChunkResidencyState::Resident, world::ChunkResidencyState::Stale,
                world::ChunkResidencyState::Evicting}) {
            const auto parsed =
                world::chunkResidencyStateFromName(world::chunkResidencyStateName(state));
            REQUIRE(parsed.has_value());
            CHECK(*parsed == state);
        }
        CHECK_FALSE(world::chunkResidencyStateFromName("partially-resident").has_value());
    }

    TEST_CASE("untouched chunks are unloaded without being tracked") {
        const world::ChunkResidencyTracker tracker;

        CHECK(tracker.stateOf(world::ChunkCoord{12, -34}) == world::ChunkResidencyState::Unloaded);
        CHECK_FALSE(tracker.isResident(world::ChunkCoord{12, -34}));
        CHECK(tracker.trackedChunkCount() == 0);
    }

    TEST_CASE("the documented happy path is load, reside, invalidate, rebuild, evict") {
        world::ChunkResidencyTracker tracker;
        const world::ChunkCoord cell{3, -2};

        tracker.transition(cell, world::ChunkResidencyState::Loading);
        CHECK(tracker.stateOf(cell) == world::ChunkResidencyState::Loading);

        tracker.transition(cell, world::ChunkResidencyState::Resident);
        CHECK(tracker.isResident(cell));

        // A dirty-chunk invalidation marks the cell stale; stale content is
        // not render-ready.
        tracker.transition(cell, world::ChunkResidencyState::Stale);
        CHECK(tracker.stateOf(cell) == world::ChunkResidencyState::Stale);
        CHECK_FALSE(tracker.isResident(cell));

        tracker.transition(cell, world::ChunkResidencyState::Loading);
        tracker.transition(cell, world::ChunkResidencyState::Resident);
        CHECK(tracker.isResident(cell));

        tracker.transition(cell, world::ChunkResidencyState::Evicting);
        CHECK(tracker.stateOf(cell) == world::ChunkResidencyState::Evicting);
        tracker.transition(cell, world::ChunkResidencyState::Unloaded);
        CHECK(tracker.stateOf(cell) == world::ChunkResidencyState::Unloaded);
        // Released cells return to the sparse untouched state.
        CHECK(tracker.trackedChunkCount() == 0);
    }

    TEST_CASE("failed loads return to unloaded and stale chunks may be dropped") {
        world::ChunkResidencyTracker tracker;
        const world::ChunkCoord cell{0, 0};

        tracker.transition(cell, world::ChunkResidencyState::Loading);
        tracker.transition(cell, world::ChunkResidencyState::Unloaded);
        CHECK(tracker.stateOf(cell) == world::ChunkResidencyState::Unloaded);

        tracker.transition(cell, world::ChunkResidencyState::Loading);
        tracker.transition(cell, world::ChunkResidencyState::Resident);
        tracker.transition(cell, world::ChunkResidencyState::Stale);
        tracker.transition(cell, world::ChunkResidencyState::Evicting);
        tracker.transition(cell, world::ChunkResidencyState::Unloaded);
        CHECK(tracker.trackedChunkCount() == 0);
    }

    TEST_CASE("illegal transitions fail loudly instead of being absorbed") {
        world::ChunkResidencyTracker tracker;
        const world::ChunkCoord cell{1, 1};

        // Nothing may render, evict, or go stale before loading.
        const auto unloadToResident = captureException<world::WorldPartitionError>([&] {
            tracker.transition(cell, world::ChunkResidencyState::Resident);
        });
        REQUIRE(unloadToResident.has_value());
        CHECK(unloadToResident->code()
            == world::WorldPartitionErrorCode::IllegalResidencyTransition);

        tracker.transition(cell, world::ChunkResidencyState::Loading);
        const auto loadingToEvicting = captureException<world::WorldPartitionError>([&] {
            tracker.transition(cell, world::ChunkResidencyState::Evicting);
        });
        REQUIRE(loadingToEvicting.has_value());

        tracker.transition(cell, world::ChunkResidencyState::Resident);
        const auto residentToLoading = captureException<world::WorldPartitionError>([&] {
            tracker.transition(cell, world::ChunkResidencyState::Loading);
        });
        REQUIRE(residentToLoading.has_value());

        // Redundant same-state transitions are misuse too.
        const auto residentToResident = captureException<world::WorldPartitionError>([&] {
            tracker.transition(cell, world::ChunkResidencyState::Resident);
        });
        REQUIRE(residentToResident.has_value());

        // The rejected transitions changed nothing.
        CHECK(tracker.stateOf(cell) == world::ChunkResidencyState::Resident);
    }

    TEST_CASE("tracking stays sparse across a large remote world") {
        world::ChunkResidencyTracker tracker;

        // Cells scattered across a +/-50 km world at 1 km cells; only the
        // touched cells exist in the tracker.
        for (std::int64_t i = 0; i < 10; ++i) {
            const world::ChunkCoord cell{i * 997 - 5000, -i * 991 + 5000};
            tracker.transition(cell, world::ChunkResidencyState::Loading);
            tracker.transition(cell, world::ChunkResidencyState::Resident);
        }
        CHECK(tracker.trackedChunkCount() == 10);
        CHECK(tracker.isResident(world::ChunkCoord{-5000, 5000}));
        CHECK(tracker.stateOf(world::ChunkCoord{0, 0}) == world::ChunkResidencyState::Unloaded);
    }
}
