#include <doctest/doctest.h>

#include "infraforge/domain/world/ChunkCacheMetadata.hpp"
#include "infraforge/domain/world/ChunkResidency.hpp"
#include "infraforge/domain/world/Invalidation.hpp"
#include "infraforge/domain/world/SpatialIndex.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace world = infraforge::domain::world;
namespace geo = infraforge::domain::geo;

namespace {

// The production grid path: the physical 1 km default edge converted
// through the project's resolved canonical linear unit (metres here).
world::ChunkGrid kilometreGrid() {
    return world::ChunkGrid::fromMetreEdge(geo::ResolvedUnit{"metre", 1.0});
}

world::InvalidationMask geometryInvalidation() {
    return world::InvalidationMask::of(world::InvalidationClass::Geometry);
}

// Deterministic LCG so the synthetic 100 km world is identical on every
// platform and run (std:: distributions are implementation-defined).
class DeterministicRng {
public:
    explicit DeterministicRng(std::uint64_t seed)
        : state_(seed) {}

    [[nodiscard]] double inRange(double low, double high) {
        state_ = state_ * 6364136223846793005ULL + 1442695040888963407ULL;
        const auto unit = static_cast<double>(state_ >> 11) / 9007199254740992.0;
        return low + (high - low) * unit;
    }

private:
    std::uint64_t state_;
};

struct WorldEntity {
    world::EntityId id{};
    world::SpatialBounds bounds;
};

// A synthetic ~100 km x 100 km infrastructure project: 100 work sites
// scattered over +/-50 km in every quadrant, each with a compact cluster of
// 20 small assets. Large empty regions sit between the sites, exactly like
// a real corridor network.
class SyntheticLargeWorld {
public:
    static constexpr double kHalfExtent = 50000.0;
    static constexpr std::size_t kClusters = 100;
    static constexpr std::size_t kEntitiesPerCluster = 20;
    static constexpr std::size_t kEntityCount = kClusters * kEntitiesPerCluster;

    SyntheticLargeWorld()
        : rng_(0x496e667261466f72ULL) // "InfraFor"
    {
        entities_.reserve(kEntityCount);
        for (std::size_t cluster = 0; cluster < kClusters; ++cluster) {
            const auto centerX = rng_.inRange(-kHalfExtent + 500.0, kHalfExtent - 500.0);
            const auto centerY = rng_.inRange(-kHalfExtent + 500.0, kHalfExtent - 500.0);
            clusterCenters_.emplace_back(centerX, centerY);
            for (std::size_t item = 0; item < kEntitiesPerCluster; ++item) {
                const auto x = centerX + rng_.inRange(-60.0, 60.0);
                const auto y = centerY + rng_.inRange(-60.0, 60.0);
                entities_.push_back(WorldEntity{
                    world::EntityId{0xC0FFEEULL, entities_.size()},
                    world::SpatialBounds::ofEdges(x - 3.0, y - 3.0, x + 3.0, y + 3.0)});
            }
        }
    }

    [[nodiscard]] const std::vector<WorldEntity>& entities() const noexcept { return entities_; }
    [[nodiscard]] const std::vector<std::pair<double, double>>& clusterCenters() const noexcept {
        return clusterCenters_;
    }

private:
    DeterministicRng rng_;
    std::vector<WorldEntity> entities_;
    std::vector<std::pair<double, double>> clusterCenters_;
};

// Independent ground truth for the whole index, derived only from the grid
// math and the canonical entity bounds: cell -> entity ids.
using ExpectedCellMap = std::map<world::ChunkCoord, std::set<world::EntityId>>;

[[nodiscard]] ExpectedCellMap buildExpectedCells(
    const world::ChunkGrid& grid, const std::vector<WorldEntity>& entities) {
    ExpectedCellMap expected;
    for (const auto& entity : entities) {
        for (const auto chunk : grid.chunksIntersecting(entity.bounds)) {
            expected[chunk].insert(entity.id);
        }
    }
    return expected;
}

[[nodiscard]] std::vector<world::EntityId> bruteForceIntersecting(
    const std::vector<WorldEntity>& entities, const world::SpatialBounds query) {
    std::vector<world::EntityId> expected;
    for (const auto& entity : entities) {
        if (entity.bounds.intersects(query)) {
            expected.push_back(entity.id);
        }
    }
    std::sort(expected.begin(), expected.end());
    return expected;
}

} // namespace

TEST_SUITE("large-world spatial index acceptance") {
    TEST_CASE("a synthetic 100 km world indexes sparsely and query-plans exactly") {
        const SyntheticLargeWorld synthetic;
        const world::ChunkGrid grid{kilometreGrid()}; // default 1 km physical cells
        world::SpatialIndex index{grid};

        for (const auto& entity : synthetic.entities()) {
            static_cast<void>(index.insert(entity.id, entity.bounds, geometryInvalidation()));
        }

        // 100 km of extent at 1 km cells would be 100 x 100 = 10,000 dense
        // cells. The index must hold only the occupied subset.
        const auto denseCellCount = static_cast<std::size_t>(
            (2.0 * SyntheticLargeWorld::kHalfExtent / grid.chunkSize())
            * (2.0 * SyntheticLargeWorld::kHalfExtent / grid.chunkSize()));
        CHECK(denseCellCount == 10000);
        CHECK(index.entityCount() == SyntheticLargeWorld::kEntityCount);
        CHECK(index.occupiedChunkCount() > 0);
        CHECK(index.occupiedChunkCount() * 5 < denseCellCount);

        // The occupied subset matches independent ground truth exactly —
        // nothing was dropped, and no cell was invented.
        const auto expected = buildExpectedCells(grid, synthetic.entities());
        CHECK(index.occupiedChunkCount() == expected.size());
        for (const auto& [chunk, ids] : expected) {
            const auto indexed = index.entitiesInChunk(chunk);
            CHECK(std::vector<world::EntityId>(indexed.begin(), indexed.end())
                == std::vector<world::EntityId>(ids.begin(), ids.end()));
        }

        // Both hemispheres of the world carry content and both query
        // correctly.
        bool sawNegative = false;
        bool sawPositive = false;
        for (const auto& [chunk, ids] : expected) {
            sawNegative = sawNegative || chunk.x < 0 || chunk.y < 0;
            sawPositive = sawPositive || chunk.x > 0 || chunk.y > 0;
        }
        REQUIRE(sawNegative);
        REQUIRE(sawPositive);

        // Query plans across the full extent: near every work site the
        // index returns exactly the brute-force answer, and distant corner
        // probes do too.
        for (const auto& [centerX, centerY] : synthetic.clusterCenters()) {
            const auto query = world::SpatialBounds::ofEdges(
                centerX - 100.0, centerY - 100.0, centerX + 100.0, centerY + 100.0);
            CHECK(index.entitiesIntersecting(query)
                == bruteForceIntersecting(synthetic.entities(), query));
        }
        for (const auto& probe : {std::pair{-49000.0, 49000.0},
                 std::pair{49000.0, -49000.0}, std::pair{-49000.0, -49000.0}}) {
            const auto query = world::SpatialBounds::ofEdges(
                probe.first - 500.0, probe.second - 500.0,
                probe.first + 500.0, probe.second + 500.0);
            CHECK(index.entitiesIntersecting(query)
                == bruteForceIntersecting(synthetic.entities(), query));
        }

        // Renderer residency is a bounded working set, never the world.
        world::ChunkResidencyTracker tracker;
        for (const auto& occupied : expected) {
            tracker.transition(occupied.first, world::ChunkResidencyState::Loading);
            tracker.transition(occupied.first, world::ChunkResidencyState::Resident);
        }
        CHECK(tracker.trackedChunkCount() == expected.size());
        CHECK(tracker.trackedChunkCount() * 5 < denseCellCount);
    }

    TEST_CASE("a one-entity local edit invalidates only its spatial neighbourhood") {
        const SyntheticLargeWorld synthetic;
        const world::ChunkGrid grid{kilometreGrid()};
        world::SpatialIndex index{grid};
        for (const auto& entity : synthetic.entities()) {
            static_cast<void>(index.insert(entity.id, entity.bounds, geometryInvalidation()));
        }
        const auto expected = buildExpectedCells(grid, synthetic.entities());
        const auto occupiedBefore = index.occupiedChunkCount();
        const auto revisionBefore = index.revision();

        // Move one asset from its work site to a far corner of the world.
        const auto& moved = synthetic.entities().front();
        const auto movedFrom = index.boundsOf(moved.id);
        REQUIRE(movedFrom.has_value());

        // Content generations of three occupied cells far away from BOTH
        // the entity's origin and its destination, captured the way a
        // chunk generator would record them before the edit.
        const auto fromCenterX = (movedFrom->minEasting + movedFrom->maxEasting) / 2.0;
        const auto fromCenterY = (movedFrom->minNorthing + movedFrom->maxNorthing) / 2.0;
        const auto toCenterX = -48000.0 + 3.0;
        const auto toCenterY = 48000.0;
        std::vector<std::pair<world::ChunkCoord, world::ChunkCacheMetadata>> distantCaches;
        for (const auto& occupied : expected) {
            if (distantCaches.size() == 3) {
                break;
            }
            const auto footprint = grid.chunkBounds(occupied.first);
            const auto centerX = (footprint.minEasting + footprint.maxEasting) / 2.0;
            const auto centerY = (footprint.minNorthing + footprint.maxNorthing) / 2.0;
            const auto distanceFrom = std::hypot(centerX - fromCenterX, centerY - fromCenterY);
            const auto distanceTo = std::hypot(centerX - toCenterX, centerY - toCenterY);
            if (distanceFrom > 40000.0 && distanceTo > 40000.0) {
                distantCaches.emplace_back(occupied.first,
                    world::ChunkCacheMetadata{
                        world::currentChunkCacheSchemaVersion, 1,
                        index.lastAffectingRevision(occupied.first)});
            }
        }
        REQUIRE(distantCaches.size() == 3);

        const auto mutation = index.update(moved.id,
            world::SpatialBounds::ofEdges(
                -48000.0, 48000.0 - 3.0, -48000.0 + 6.0, 48000.0 + 3.0),
            geometryInvalidation());

        // The dirty set is the old cells united with the new cells — and
        // nothing else. A 6 m asset can touch at most 2 cells per state.
        CHECK(mutation.revision == revisionBefore + 1);
        CHECK(mutation.dirtyChunks.size() <= 4);
        CHECK(mutation.dirtyChunks.size() * 1000 < 10000);

        // The distant chunks' content generations are untouched: the
        // global revision moved, but their caches stay current.
        for (const auto& [cell, cache] : distantCaches) {
            CHECK(index.lastAffectingRevision(cell) == cache.sourceRevision);
            CHECK(world::isCurrent(cache,
                world::ChunkCacheExpectation{
                    world::currentChunkCacheSchemaVersion, 1,
                    index.lastAffectingRevision(cell)}));
        }
        // ...while every cell of the edit carries a new generation.
        for (const auto chunk : mutation.dirtyChunks) {
            CHECK(index.lastAffectingRevision(chunk) == mutation.revision);
        }

        // The union is exactly the ground-truth cell diff.
        std::set<world::ChunkCoord> expectedDirty;
        for (const auto chunk : grid.chunksIntersecting(*movedFrom)) {
            expectedDirty.insert(chunk);
        }
        for (const auto chunk : grid.chunksIntersecting(index.boundsOf(moved.id).value())) {
            expectedDirty.insert(chunk);
        }
        CHECK(std::vector<world::ChunkCoord>(
                  expectedDirty.begin(), expectedDirty.end())
            == mutation.dirtyChunks);

        // Only the moved entity's world region changed; the rest of the
        // occupancy matches ground truth.
        CHECK(index.entityCount() == SyntheticLargeWorld::kEntityCount);
        auto expectedAfter = expected;
        for (const auto chunk : grid.chunksIntersecting(*movedFrom)) {
            const auto cell = expectedAfter.find(chunk);
            if (cell != expectedAfter.end()) {
                cell->second.erase(moved.id);
                if (cell->second.empty()) {
                    expectedAfter.erase(cell);
                }
            }
        }
        const auto movedTo = index.boundsOf(moved.id).value();
        for (const auto chunk : grid.chunksIntersecting(movedTo)) {
            expectedAfter[chunk].insert(moved.id);
        }
        CHECK(index.occupiedChunkCount() == expectedAfter.size());
        CHECK(index.occupiedChunkCount() >= occupiedBefore - 1);
        CHECK(index.occupiedChunkCount() <= occupiedBefore + 2);
        for (const auto& [chunk, ids] : expectedAfter) {
            const auto indexed = index.entitiesInChunk(chunk);
            CHECK(std::vector<world::EntityId>(indexed.begin(), indexed.end())
                == std::vector<world::EntityId>(ids.begin(), ids.end()));
        }

        // Streaming consequence through the one canonical dirty mechanism:
        // only resident cells inside the dirty set go stale; the other
        // ~occupied working-set cells are untouched by the local edit.
        world::ChunkResidencyTracker tracker;
        for (const auto& occupied : expected) {
            tracker.transition(occupied.first, world::ChunkResidencyState::Loading);
            tracker.transition(occupied.first, world::ChunkResidencyState::Resident);
        }
        world::ChunkDirtySet dirty;
        dirty.absorb(mutation);
        std::size_t staleCount = 0;
        for (const auto& [chunk, classes] : dirty.chunks()) {
            if (tracker.stateOf(chunk) == world::ChunkResidencyState::Resident) {
                tracker.transition(chunk, world::ChunkResidencyState::Stale);
                ++staleCount;
            } else {
                // A cell the working set never loaded simply stays
                // unloaded; it is not invalidated into existence.
                CHECK(tracker.stateOf(chunk) == world::ChunkResidencyState::Unloaded);
            }
        }
        CHECK(staleCount >= 1);
        CHECK(staleCount <= dirty.size());
        CHECK(tracker.trackedChunkCount() == expected.size());
        CHECK(staleCount * 1000 < 10000);
        CHECK(dirty.classesFor(mutation.dirtyChunks.front()).value()
            == world::InvalidationMask::of(world::InvalidationClass::Geometry));
    }
}
