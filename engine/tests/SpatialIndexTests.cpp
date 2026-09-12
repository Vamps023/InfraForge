#include <doctest/doctest.h>

#include "TestHelpers.hpp"
#include "infraforge/domain/world/EntityId.hpp"
#include "infraforge/domain/world/Invalidation.hpp"
#include "infraforge/domain/world/SpatialIndex.hpp"

#include <algorithm>
#include <limits>
#include <vector>

namespace world = infraforge::domain::world;

using infraforge::testhelpers::captureException;

namespace {

world::EntityId entityId(std::uint64_t low) {
    return world::EntityId{0x4f'00'00'00'00'00'00'00ULL, low};
}

} // namespace

TEST_SUITE("canonical spatial index") {
    TEST_CASE("insert registers bounds and affected chunks") {
        world::SpatialIndex index{world::ChunkGrid{}};

        const auto mutation =
            index.insert(entityId(1), world::SpatialBounds::ofEdges(100.0, 100.0, 300.0, 300.0),
                world::InvalidationMask::of(world::InvalidationClass::Geometry));

        CHECK(index.entityCount() == 1);
        CHECK(index.contains(entityId(1)));
        CHECK(index.boundsOf(entityId(1))
            == world::SpatialBounds::ofEdges(100.0, 100.0, 300.0, 300.0));
        CHECK(mutation.previousChunks.empty());
        REQUIRE(mutation.updatedChunks.size() == 1);
        CHECK(mutation.updatedChunks[0] == world::ChunkCoord{0, 0});
        CHECK(mutation.dirtyChunks == mutation.updatedChunks);
        CHECK(mutation.classes == world::InvalidationMask::of(world::InvalidationClass::Geometry));
        CHECK(mutation.revision == 1);
        CHECK(index.revision() == 1);
        CHECK(index.chunksIntersecting(world::SpatialBounds::ofPoint(150.0, 150.0))
            == std::vector<world::ChunkCoord>{{0, 0}});
    }

    TEST_CASE("movement dirties the union of old and new chunk sets") {
        world::SpatialIndex index{world::ChunkGrid{}};
        static_cast<void>(index.insert(
            entityId(7), world::SpatialBounds::ofPoint(500.0, 500.0)));

        const auto mutation = index.update(
            entityId(7), world::SpatialBounds::ofPoint(1500.0, 500.0),
            world::InvalidationMask::of(world::InvalidationClass::Geometry));

        // The entity left cell (0,0) and entered (1,0): both must be dirty
        // so content it vacated is invalidated as well.
        REQUIRE(mutation.previousChunks.size() == 1);
        REQUIRE(mutation.updatedChunks.size() == 1);
        CHECK(mutation.previousChunks[0] == world::ChunkCoord{0, 0});
        CHECK(mutation.updatedChunks[0] == world::ChunkCoord{1, 0});
        REQUIRE(mutation.dirtyChunks.size() == 2);
        CHECK(mutation.dirtyChunks[0] == world::ChunkCoord{0, 0});
        CHECK(mutation.dirtyChunks[1] == world::ChunkCoord{1, 0});
        CHECK(index.boundsOf(entityId(7)) == world::SpatialBounds::ofPoint(1500.0, 500.0));
        CHECK(index.chunksOf(entityId(7)) == std::vector<world::ChunkCoord>{{1, 0}});
        // A 50 km jump keeps the same bounded dirty-set shape.
        const auto farMove = index.update(
            entityId(7), world::SpatialBounds::ofPoint(50000.0, -50000.0));
        REQUIRE(farMove.dirtyChunks.size() == 2);
        CHECK(farMove.dirtyChunks[0] == world::ChunkCoord{1, 0});
        CHECK(farMove.dirtyChunks[1] == world::ChunkCoord{50, -50});
    }

    TEST_CASE("shrinking bounds invalidate the vacated remainder") {
        world::SpatialIndex index{world::ChunkGrid{}};
        static_cast<void>(index.insert(entityId(2),
            world::SpatialBounds::ofEdges(-1000.0, -1000.0, 1999.0, 1999.0)));
        REQUIRE(index.chunksOf(entityId(2)).size() == 9);

        const auto shrunk =
            index.update(entityId(2), world::SpatialBounds::ofEdges(-100.0, -100.0, 100.0, 100.0));

        // The 200 m core covers the four cells around the origin; the
        // vacated five cells of the former 3x3 footprint stay dirty.
        REQUIRE(shrunk.previousChunks.size() == 9);
        CHECK(shrunk.updatedChunks
            == std::vector<world::ChunkCoord>{{-1, -1}, {-1, 0}, {0, -1}, {0, 0}});
        REQUIRE(shrunk.dirtyChunks.size() == 9);
        CHECK(std::find(shrunk.dirtyChunks.begin(), shrunk.dirtyChunks.end(),
                  world::ChunkCoord{1, 1})
            != shrunk.dirtyChunks.end());
        CHECK(index.occupiedChunkCount() == 4);
        CHECK(index.entitiesInChunk(world::ChunkCoord{1, 1}).empty());
    }

    TEST_CASE("expanding bounds invalidate the newly covered cells") {
        world::SpatialIndex index{world::ChunkGrid{}};
        static_cast<void>(index.insert(
            entityId(3), world::SpatialBounds::ofPoint(0.0, 0.0)));

        const auto expanded =
            index.update(entityId(3), world::SpatialBounds::ofEdges(-1000.0, -2000.0, 500.0, 0.0));

        CHECK(expanded.updatedChunks.size() == 6);
        CHECK(expanded.dirtyChunks.size() == 6);
        CHECK(index.chunksOf(entityId(3)).size() == 6);
        CHECK(index.occupiedChunkCount() == 6);
    }

    TEST_CASE("removal releases cells and reports the vacated set") {
        world::SpatialIndex index{world::ChunkGrid{}};
        static_cast<void>(index.insert(entityId(4),
            world::SpatialBounds::ofEdges(900.0, 900.0, 1100.0, 1100.0)));
        REQUIRE(index.occupiedChunkCount() == 4);

        const auto removed = index.remove(entityId(4));

        CHECK_FALSE(index.contains(entityId(4)));
        CHECK_FALSE(index.boundsOf(entityId(4)).has_value());
        CHECK(index.entityCount() == 0);
        CHECK(index.occupiedChunkCount() == 0);
        REQUIRE(removed.previousChunks.size() == 4);
        CHECK(removed.updatedChunks.empty());
        CHECK(removed.dirtyChunks == removed.previousChunks);
        CHECK(index.entitiesInChunk(world::ChunkCoord{1, 1}).empty());
    }

    TEST_CASE("entities sharing one chunk are all reported") {
        world::SpatialIndex index{world::ChunkGrid{}};
        static_cast<void>(index.insert(entityId(10), world::SpatialBounds::ofPoint(10.0, 10.0)));
        static_cast<void>(index.insert(entityId(11), world::SpatialBounds::ofPoint(20.0, 990.0)));
        static_cast<void>(index.insert(entityId(12), world::SpatialBounds::ofPoint(30.0, 20.0)));
        static_cast<void>(index.insert(entityId(13), world::SpatialBounds::ofPoint(1200.0, 20.0)));

        const auto inOrigin = index.entitiesInChunk(world::ChunkCoord{0, 0});
        REQUIRE(inOrigin.size() == 3);
        CHECK(inOrigin[0] == entityId(10));
        CHECK(inOrigin[1] == entityId(11));
        CHECK(inOrigin[2] == entityId(12));
        CHECK(index.entitiesInChunk(world::ChunkCoord{1, 0})
            == std::vector<world::EntityId>{entityId(13)});
        CHECK(index.occupiedChunkCount() == 2);

        // Querying the shared chunk region finds exactly the three (the
        // fourth entity shares no cell and no bounds overlap).
        const auto found = index.entitiesIntersecting(
            world::SpatialBounds::ofEdges(0.0, 0.0, 1000.0, 1000.0));
        REQUIRE(found.size() == 3);
        CHECK(found[0] == entityId(10));
        CHECK(found[1] == entityId(11));
        CHECK(found[2] == entityId(12));
    }

    TEST_CASE("one entity may span many chunks without becoming them") {
        world::SpatialIndex index{world::ChunkGrid{}};

        // A long corridor road from (-2500, 500) to (2500, 500): six cells.
        const auto mutation = index.insert(
            entityId(20), world::SpatialBounds::ofEdges(-2500.0, 400.0, 2500.0, 600.0));

        REQUIRE(mutation.updatedChunks.size() == 6);
        CHECK(index.chunksOf(entityId(20)).size() == 6);
        // Chunk identity is not entity identity: exactly one entity exists,
        // registered in five distinct partition cells.
        CHECK(index.entityCount() == 1);
        for (const auto chunk : index.chunksOf(entityId(20))) {
            CHECK(index.entitiesInChunk(chunk) == std::vector<world::EntityId>{entityId(20)});
        }
        // Cell coordinates differ from the entity identity by construction.
        CHECK(mutation.updatedChunks[0] == world::ChunkCoord{-3, 0});
    }

    TEST_CASE("query intersection honours real bounds, not just cells") {
        world::SpatialIndex index{world::ChunkGrid{}};
        static_cast<void>(index.insert(
            entityId(30), world::SpatialBounds::ofEdges(100.0, 100.0, 200.0, 200.0)));
        static_cast<void>(index.insert(
            entityId(31), world::SpatialBounds::ofEdges(1800.0, 100.0, 1900.0, 200.0)));

        // A point query in cell (0,0) selects only entity 30 as a cell
        // candidate, and the real bounds check rejects it too.
        CHECK(index.entitiesIntersecting(world::SpatialBounds::ofPoint(500.0, 150.0)).empty());
        // Cell overlap is necessary but not sufficient: a query inside
        // cell (0,0) must re-check candidate bounds against the query.
        const auto inCell = index.entitiesIntersecting(
            world::SpatialBounds::ofEdges(150.0, 150.0, 250.0, 250.0));
        CHECK(inCell == std::vector<world::EntityId>{entityId(30)});
    }

    TEST_CASE("an entity can temporarily have no spatial extent") {
        world::SpatialIndex index{world::ChunkGrid{}};
        static_cast<void>(index.insert(entityId(40), world::SpatialBounds::ofPoint(100.0, 100.0)));

        const auto dormant = index.update(
            entityId(40), world::SpatialBounds::empty(),
            world::InvalidationMask::of(world::InvalidationClass::Topology));

        CHECK(index.contains(entityId(40)));
        CHECK(index.entityCount() == 1);
        CHECK(index.occupiedChunkCount() == 0);
        CHECK(index.chunksOf(entityId(40)).empty());
        CHECK(dormant.previousChunks.size() == 1);
        CHECK(dormant.updatedChunks.empty());
        CHECK(dormant.dirtyChunks.size() == 1);
        // Re-activating dirties only the re-entered cell.
        const auto revived = index.update(entityId(40), world::SpatialBounds::ofPoint(-100.0, 100.0));
        CHECK(revived.previousChunks.empty());
        CHECK(revived.updatedChunks.size() == 1);
    }

    TEST_CASE("invalid input is rejected without mutating the index") {
        world::SpatialIndex index{world::ChunkGrid{}};

        const auto nullId = captureException<world::WorldPartitionError>([&] {
            static_cast<void>(index.insert(world::EntityId{}, world::SpatialBounds::ofPoint(0.0, 0.0)));
        });
        REQUIRE(nullId.has_value());
        CHECK(nullId->code() == world::WorldPartitionErrorCode::NullEntityId);

        static_cast<void>(index.insert(entityId(50), world::SpatialBounds::ofPoint(0.0, 0.0)));

        const auto duplicate = captureException<world::WorldPartitionError>([&] {
            static_cast<void>(index.insert(entityId(50), world::SpatialBounds::ofPoint(1.0, 1.0)));
        });
        REQUIRE(duplicate.has_value());
        CHECK(duplicate->code() == world::WorldPartitionErrorCode::DuplicateEntity);

        const auto unknownUpdate = captureException<world::WorldPartitionError>([&] {
            static_cast<void>(index.update(entityId(51), world::SpatialBounds::ofPoint(1.0, 1.0)));
        });
        REQUIRE(unknownUpdate.has_value());
        CHECK(unknownUpdate->code() == world::WorldPartitionErrorCode::UnknownEntity);

        const auto unknownRemove = captureException<world::WorldPartitionError>([&] {
            static_cast<void>(index.remove(entityId(51)));
        });
        REQUIRE(unknownRemove.has_value());
        CHECK(unknownRemove->code() == world::WorldPartitionErrorCode::UnknownEntity);

        const auto nonFinite = captureException<world::WorldPartitionError>([&] {
            static_cast<void>(index.insert(
                entityId(52), world::SpatialBounds::ofEdges(0.0, 0.0,
                                  std::numeric_limits<double>::infinity(), 10.0)));
        });
        REQUIRE(nonFinite.has_value());
        CHECK(nonFinite->code() == world::WorldPartitionErrorCode::InvalidBounds);

        CHECK(index.entityCount() == 1);
        CHECK(index.revision() == 1);
    }

    TEST_CASE("chunk dirty set accumulates per-chunk classes across mutations") {
        world::ChunkDirtySet dirty;
        world::SpatialIndex index{world::ChunkGrid{}};

        const auto first = index.insert(entityId(60),
            world::SpatialBounds::ofPoint(10.0, 10.0),
            world::InvalidationMask::of(world::InvalidationClass::Geometry));
        dirty.absorb(first);

        const auto second = index.update(entityId(60),
            world::SpatialBounds::ofPoint(1010.0, 10.0),
            world::InvalidationMask::of(world::InvalidationClass::Geometry)
                | world::InvalidationMask::of(world::InvalidationClass::Material));
        dirty.absorb(second);

        // Cell (0,0) accumulated geometry from both mutations plus material
        // from the move; cell (1,0) only the move's classes.
        const auto originClasses = dirty.classesFor(world::ChunkCoord{0, 0});
        REQUIRE(originClasses.has_value());
        CHECK(originClasses->contains(world::InvalidationClass::Geometry));
        CHECK(originClasses->contains(world::InvalidationClass::Material));
        CHECK_FALSE(originClasses->contains(world::InvalidationClass::Terrain));

        const auto eastClasses = dirty.classesFor(world::ChunkCoord{1, 0});
        REQUIRE(eastClasses.has_value());
        CHECK(eastClasses->contains(world::InvalidationClass::Geometry));
        CHECK(eastClasses->contains(world::InvalidationClass::Material));

        CHECK(dirty.size() == 2);
        CHECK_FALSE(dirty.contains(world::ChunkCoord{-5, -5}));

        dirty.clear();
        CHECK(dirty.empty());
    }

    TEST_CASE("each invalidation class is distinct, named, and mask-composable") {
        using world::InvalidationClass;

        CHECK(world::invalidationClassCount() == 5);
        for (auto i = std::size_t{0}; i < world::invalidationClassCount(); ++i) {
            const auto cls = static_cast<InvalidationClass>(i);
            const auto parsed = world::invalidationClassFromName(world::invalidationClassName(cls));
            REQUIRE(parsed.has_value());
            CHECK(*parsed == cls);
        }
        CHECK_FALSE(world::invalidationClassFromName("particles").has_value());

        const auto all = world::InvalidationMask::of(InvalidationClass::Geometry)
            | world::InvalidationMask::of(InvalidationClass::Material)
            | world::InvalidationMask::of(InvalidationClass::Topology)
            | world::InvalidationMask::of(InvalidationClass::Terrain)
            | world::InvalidationMask::of(InvalidationClass::Simulation);
        for (auto i = std::size_t{0}; i < world::invalidationClassCount(); ++i) {
            CHECK(all.contains(static_cast<InvalidationClass>(i)));
        }
        CHECK_FALSE(all.contains(static_cast<InvalidationClass>(world::invalidationClassCount())));

        // Masks are independent values: combining does not bleed between
        // mutations.
        world::InvalidationMask mask;
        CHECK(mask.empty());
        mask.add(InvalidationClass::Terrain);
        CHECK(mask.contains(InvalidationClass::Terrain));
        CHECK_FALSE(mask.contains(InvalidationClass::Geometry));
        CHECK(world::InvalidationMask::fromBits(mask.bits()) == mask);
    }

    TEST_CASE("chunk identity and entity identity are independent") {
        world::SpatialIndex index{world::ChunkGrid{}};

        // Deliberately degenerate-looking: the entity id numerically equals
        // a chunk coordinate pair, yet the two namespaces never interact.
        const world::EntityId deceptive{1, 2};
        static_cast<void>(index.insert(
            deceptive, world::SpatialBounds::ofPoint(2000.0, 2000.0)));

        // Chunk (1,2) is empty; the entity lives in cell (2,2).
        CHECK(index.entitiesInChunk(world::ChunkCoord{1, 2}).empty());
        CHECK(index.entitiesInChunk(world::ChunkCoord{2, 2})
            == std::vector<world::EntityId>{deceptive});
        // Moving the entity does not change any other cell's identity, and
        // changing which cells exist never changes the entity.
        static_cast<void>(index.update(deceptive, world::SpatialBounds::ofPoint(-2000.0, 2000.0)));
        CHECK(deceptive == world::EntityId{1, 2});
        CHECK(index.boundsOf(deceptive).has_value());
        CHECK(index.occupiedChunkCount() == 1);
    }
}
