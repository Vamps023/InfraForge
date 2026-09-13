#include <doctest/doctest.h>

#include "infraforge/viewport/renderer/TerrainTileCache.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

TEST_SUITE("terrain tile streaming policy") {

namespace {
infraforge::viewport::TerrainSceneTile makeTile(
    const std::string& uuid, std::int64_t x, std::int64_t y, std::uint64_t revision = 1) {
    infraforge::viewport::TerrainSceneTile tile;
    tile.datasetUuid = uuid;
    tile.datasetRevision = revision;
    tile.chunkX = x;
    tile.chunkY = y;
    tile.path = "C:/cache/" + uuid + "_" + std::to_string(x) + "_" + std::to_string(y) + ".iforgetile";
    // 1000 m tile around (x * 1000, y * 1000).
    tile.minEasting = static_cast<double>(x) * 1000.0;
    tile.maxEasting = tile.minEasting + 1000.0;
    tile.minNorthing = static_cast<double>(y) * 1000.0;
    tile.maxNorthing = tile.minNorthing + 1000.0;
    return tile;
}
} // namespace

TEST_CASE("LOD decision is deterministic from the camera metric") {
    using infraforge::viewport::TerrainTileCache;
    // 1000 m tile: level-0 spacing is 1000/128 ≈ 7.8 m.
    const double level0Spacing = 1000.0 / TerrainTileCache::kLevel0Samples;
    CHECK(TerrainTileCache::lodForSpacing(1.0, level0Spacing) == 0);
    CHECK(TerrainTileCache::lodForSpacing(level0Spacing, level0Spacing) == 0);
    CHECK(TerrainTileCache::lodForSpacing(level0Spacing * 1.9, level0Spacing) == 0);
    CHECK(TerrainTileCache::lodForSpacing(level0Spacing * 2.1, level0Spacing) == 1);
    CHECK(TerrainTileCache::lodForSpacing(level0Spacing * 3.9, level0Spacing) == 1);
    CHECK(TerrainTileCache::lodForSpacing(level0Spacing * 4.1, level0Spacing) == 2);
    // Far zoom-out clamps at the last level.
    CHECK(TerrainTileCache::lodForSpacing(level0Spacing * 1000.0, level0Spacing)
        == TerrainTileCache::kLodCount - 1);
    CHECK(TerrainTileCache::lodForSpacing(0.0, level0Spacing) == 0);
}

TEST_CASE("scene adoption adds, stales, and evicts tiles by revision and presence") {
    using infraforge::viewport::TerrainTileCache;
    TerrainTileCache cache;

    infraforge::viewport::TerrainScene scene;
    scene.tiles.push_back(makeTile("ds-a", 0, 0));
    scene.tiles.push_back(makeTile("ds-a", 1, 0));
    std::size_t staled = cache.adoptScene(scene);
    CHECK(staled == 0);
    CHECK(cache.trackedCount() == 2);

    // Load both tiles (camera at origin tile): every load must be confirmed
    // by the pass, mirroring the real update/notify cycle.
    const infraforge::viewport::TerrainCameraState camera{
        .centerX = 500.0, .centerY = 500.0, .metersPerPixel = 1.0, .viewportWidth = 1000, .viewportHeight = 800};
    for (int frame = 0; frame < 4; ++frame) {
        const auto result = cache.update(camera);
        for (const auto* entry : result.toLoad) {
            cache.notifyLoaded(entry->tile.datasetUuid, entry->tile.chunkX, entry->tile.chunkY,
                entry->desiredLod, true);
        }
    }
    CHECK(cache.residentTiles().size() == 2);

    // Same tiles with a moved dataset revision: resident content goes stale.
    infraforge::viewport::TerrainScene revised;
    revised.tiles.push_back(makeTile("ds-a", 0, 0, 2));
    revised.tiles.push_back(makeTile("ds-a", 1, 0, 2));
    staled = cache.adoptScene(revised);
    CHECK(staled == 2);

    // A scene without tile (1,0) evicts it; (0,0) stays resident-tracked.
    infraforge::viewport::TerrainScene reduced;
    reduced.tiles.push_back(makeTile("ds-a", 0, 0, 2));
    (void)cache.adoptScene(reduced);
    bool sawRelease = false;
    for (int frame = 0; frame < 5; ++frame) {
        const auto result = cache.update(camera);
        for (const auto* entry : result.toRelease) {
            sawRelease = true;
            cache.notifyReleased(entry->tile.datasetUuid, entry->tile.chunkX, entry->tile.chunkY);
        }
    }
    CHECK(sawRelease);
    CHECK(cache.trackedCount() == 1);
}

TEST_CASE("working set is bounded; farthest tiles are evicted first") {
    using infraforge::viewport::TerrainTileCache;
    TerrainTileCache cache;

    infraforge::viewport::TerrainScene scene;
    // More tiles than the residency budget: a 10x10 grid around the camera.
    std::vector<double> centerDistances;
    for (int y = -5; y < 5; ++y) {
        for (int x = -5; x < 5; ++x) {
            scene.tiles.push_back(makeTile("ds", x, y));
            const double dx = (static_cast<double>(x) * 1000.0 + 500.0) - 0.0;
            const double dy = (static_cast<double>(y) * 1000.0 + 500.0) - 0.0;
            centerDistances.push_back(std::sqrt(dx * dx + dy * dy));
        }
    }
    (void)cache.adoptScene(scene);
    // With distance-ordered eviction the resident set is exactly the
    // kMaxResidentTiles nearest tile centers; the farthest kept distance is
    // therefore known exactly.
    std::sort(centerDistances.begin(), centerDistances.end());
    const double farthestKept = centerDistances[TerrainTileCache::kMaxResidentTiles - 1];

    const infraforge::viewport::TerrainCameraState camera{
        .centerX = 0.0, .centerY = 0.0, .metersPerPixel = 1.0, .viewportWidth = 1000, .viewportHeight = 800};
    // Loads are bounded per update; run enough frames for everything that
    // fits to become resident.
    for (int frame = 0; frame < 200; ++frame) {
        const auto result = cache.update(camera);
        for (const auto* entry : result.toLoad) {
            cache.notifyLoaded(entry->tile.datasetUuid, entry->tile.chunkX, entry->tile.chunkY,
                entry->desiredLod, true);
        }
        for (const auto* entry : result.toRelease) {
            cache.notifyReleased(entry->tile.datasetUuid, entry->tile.chunkX, entry->tile.chunkY);
        }
    }
    // The bound plus the documented in-flight slack: loads completed since
    // the last update are resident before budget pressure is re-evaluated.
    CHECK(cache.residentTiles().size()
        <= TerrainTileCache::kMaxResidentTiles + TerrainTileCache::kMaxLoadsPerUpdate);

    // Resident tiles are exactly the nearest ones: the farthest resident
    // matches the predicted distance bound.
    double farthestResident = 0.0;
    for (const auto* entry : cache.residentTiles()) {
        const double dx = (entry->tile.minEasting + 500.0) - camera.centerX;
        const double dy = (entry->tile.minNorthing + 500.0) - camera.centerY;
        farthestResident = std::max(farthestResident, std::sqrt(dx * dx + dy * dy));
    }
    CHECK(farthestResident == doctest::Approx(farthestKept).epsilon(1e-9));

    // Loads never exceed the per-update bound.
    const auto single = cache.update(camera);
    CHECK(single.toLoad.size() <= TerrainTileCache::kMaxLoadsPerUpdate);
}

TEST_CASE("failed loads return to unloaded without becoming resident") {
    using infraforge::viewport::TerrainTileCache;
    TerrainTileCache cache;

    infraforge::viewport::TerrainScene scene;
    scene.tiles.push_back(makeTile("ds-a", 0, 0));
    (void)cache.adoptScene(scene);

    const infraforge::viewport::TerrainCameraState camera{
        .centerX = 500.0, .centerY = 500.0, .metersPerPixel = 1.0, .viewportWidth = 1000, .viewportHeight = 800};
    const auto result = cache.update(camera);
    REQUIRE(result.toLoad.size() == 1);
    cache.notifyLoaded("ds-a", 0, 0, 0, false);
    CHECK(cache.residentTiles().empty());
    // The tile is retried on a later update.
    const auto retry = cache.update(camera);
    CHECK(retry.toLoad.size() == 1);
}

// BLOCKER 6 regression: a resident tile at LOD X must reload when camera zoom
// changes the desired LOD to Y. The old payload stays drawable (LodReplacing)
// until the new LOD is loaded, then the replacement is atomic.
TEST_CASE("resident tile reloads when LOD changes (zoom out then in)") {
    using infraforge::viewport::TerrainTileCache;
    TerrainTileCache cache;

    infraforge::viewport::TerrainScene scene;
    scene.tiles.push_back(makeTile("ds-a", 0, 0));
    (void)cache.adoptScene(scene);

    const double level0Spacing = 1000.0 / TerrainTileCache::kLevel0Samples;

    // Camera close: LOD 0.
    infraforge::viewport::TerrainCameraState camera{
        .centerX = 500.0, .centerY = 500.0, .metersPerPixel = 1.0, .viewportWidth = 1000, .viewportHeight = 800};
    auto result = cache.update(camera);
    REQUIRE(result.toLoad.size() == 1);
    REQUIRE(result.toLoad[0]->desiredLod == 0);
    cache.notifyLoaded("ds-a", 0, 0, 0, true);
    CHECK(cache.residentTiles().size() == 1);
    CHECK(cache.residentTiles()[0]->loadedLod == 0);

    // Zoom out: desired LOD becomes 1. The tile should enter LodReplacing
    // and be a load candidate.
    camera.metersPerPixel = level0Spacing * 2.5;
    result = cache.update(camera);
    REQUIRE(result.toLoad.size() == 1);
    CHECK(result.toLoad[0]->desiredLod == 1);
    // The tile is still drawable (LodReplacing is drawable).
    CHECK(cache.residentTiles().size() == 1);

    // Complete the LOD replacement: the new LOD is loaded.
    cache.notifyLoaded("ds-a", 0, 0, 1, true);
    CHECK(cache.residentTiles().size() == 1);
    CHECK(cache.residentTiles()[0]->loadedLod == 1);
    CHECK(cache.residentTiles()[0]->residency == TerrainTileCache::Residency::Resident);

    // Zoom farther: desired LOD becomes 2.
    camera.metersPerPixel = level0Spacing * 4.5;
    result = cache.update(camera);
    REQUIRE(result.toLoad.size() == 1);
    CHECK(result.toLoad[0]->desiredLod == 2);
    cache.notifyLoaded("ds-a", 0, 0, 2, true);
    CHECK(cache.residentTiles()[0]->loadedLod == 2);

    // Zoom back in: desired LOD returns to 0.
    camera.metersPerPixel = 1.0;
    result = cache.update(camera);
    REQUIRE(result.toLoad.size() == 1);
    CHECK(result.toLoad[0]->desiredLod == 0);
    cache.notifyLoaded("ds-a", 0, 0, 0, true);
    CHECK(cache.residentTiles()[0]->loadedLod == 0);
}

// BLOCKER 6 regression: failed LOD replacement retains the old resident
// payload (does not corrupt cache state).
TEST_CASE("failed LOD replacement retains old resident payload") {
    using infraforge::viewport::TerrainTileCache;
    TerrainTileCache cache;

    infraforge::viewport::TerrainScene scene;
    scene.tiles.push_back(makeTile("ds-a", 0, 0));
    (void)cache.adoptScene(scene);

    const double level0Spacing = 1000.0 / TerrainTileCache::kLevel0Samples;

    // Load at LOD 0.
    infraforge::viewport::TerrainCameraState camera{
        .centerX = 500.0, .centerY = 500.0, .metersPerPixel = 1.0, .viewportWidth = 1000, .viewportHeight = 800};
    auto result = cache.update(camera);
    cache.notifyLoaded("ds-a", 0, 0, 0, true);
    CHECK(cache.residentTiles()[0]->loadedLod == 0);

    // Zoom out to desire LOD 1.
    camera.metersPerPixel = level0Spacing * 2.5;
    result = cache.update(camera);
    REQUIRE(result.toLoad.size() == 1);

    // Simulate a failed replacement load.
    cache.notifyLoaded("ds-a", 0, 0, 1, false);
    // The old LOD 0 payload should still be resident and drawable.
    CHECK(cache.residentTiles().size() == 1);
    CHECK(cache.residentTiles()[0]->loadedLod == 0);
    CHECK(cache.residentTiles()[0]->residency == TerrainTileCache::Residency::Resident);
}

// BLOCKER 6 regression: LOD replacement is bounded by max loads per update.
TEST_CASE("LOD replacement is bounded by max loads per update") {
    using infraforge::viewport::TerrainTileCache;
    TerrainTileCache cache;

    infraforge::viewport::TerrainScene scene;
    // Add many tiles so multiple need LOD replacement at once.
    for (int x = 0; x < 10; ++x) {
        for (int y = 0; y < 10; ++y) {
            scene.tiles.push_back(makeTile("ds-a", x, y));
        }
    }
    (void)cache.adoptScene(scene);

    const double level0Spacing = 1000.0 / TerrainTileCache::kLevel0Samples;

    // Load all tiles at LOD 0 (close camera).
    infraforge::viewport::TerrainCameraState camera{
        .centerX = 5000.0, .centerY = 5000.0, .metersPerPixel = 1.0, .viewportWidth = 10000, .viewportHeight = 10000};
    for (int frame = 0; frame < 200; ++frame) {
        auto result = cache.update(camera);
        for (const auto* entry : result.toLoad) {
            cache.notifyLoaded(entry->tile.datasetUuid, entry->tile.chunkX, entry->tile.chunkY,
                entry->desiredLod, true);
        }
        for (const auto* entry : result.toRelease) {
            cache.notifyReleased(entry->tile.datasetUuid, entry->tile.chunkX, entry->tile.chunkY);
        }
    }

    // Zoom out: many tiles need LOD replacement, but loads are bounded.
    camera.metersPerPixel = level0Spacing * 2.5;
    const auto result = cache.update(camera);
    CHECK(result.toLoad.size() <= TerrainTileCache::kMaxLoadsPerUpdate);
}

} // TEST_SUITE
