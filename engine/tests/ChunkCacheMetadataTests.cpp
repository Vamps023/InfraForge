#include <doctest/doctest.h>

#include "infraforge/domain/world/ChunkCacheMetadata.hpp"

namespace world = infraforge::domain::world;

TEST_SUITE("generated chunk cache metadata") {
    TEST_CASE("the metadata schema is versioned") {
        CHECK(world::currentChunkCacheSchemaVersion == 1);

        const world::ChunkCacheMetadata metadata;
        CHECK(metadata.schemaVersion == world::currentChunkCacheSchemaVersion);
    }

    TEST_CASE("metadata matching every expectation is current") {
        const world::ChunkCacheMetadata cached{
            world::currentChunkCacheSchemaVersion, 7, 42};
        const world::ChunkCacheExpectation expected{world::currentChunkCacheSchemaVersion, 7, 42};

        CHECK(world::isCurrent(cached, expected));
    }

    TEST_CASE("any drift in schema, generator, or source marks content stale") {
        const world::ChunkCacheMetadata cached{
            world::currentChunkCacheSchemaVersion, 7, 42};
        const world::ChunkCacheExpectation expected{
            world::currentChunkCacheSchemaVersion, 7, 42};

        // An older record written under a previous cache schema is never
        // interpreted — it is stale for rebuild.
        SUBCASE("older schema version") {
            CHECK_FALSE(world::isCurrent(cached, {expected.schemaVersion - 1, 7, 42}));
        }
        SUBCASE("newer schema version than the consumer supports") {
            CHECK_FALSE(world::isCurrent(cached, {expected.schemaVersion + 1, 7, 42}));
        }
        SUBCASE("generator revision moved") {
            CHECK_FALSE(world::isCurrent(cached, {expected.schemaVersion, 8, 42}));
        }
        SUBCASE("canonical source revision moved") {
            CHECK_FALSE(world::isCurrent(cached, {expected.schemaVersion, 7, 43}));
        }
    }

    TEST_CASE("metadata defaults stay compatible with default expectations") {
        const world::ChunkCacheMetadata cached;
        const world::ChunkCacheExpectation expected;

        CHECK(world::isCurrent(cached, expected));
        CHECK_FALSE(world::isCurrent(
            cached, world::ChunkCacheExpectation{expected.schemaVersion, 0, 1}));
    }
}
