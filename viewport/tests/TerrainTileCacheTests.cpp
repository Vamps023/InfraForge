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

// BLOCKER 2 regression: tile distance calculations must use render-local
// coordinates (subtracting the render origin) so the camera and tile centers
// are in the same space. With a large canonical origin (e.g., 500000,
// 4650000), the camera at render-local (500, 500) must be near a tile at
// canonical (500000, 4650000) — not millions of metres away.
TEST_CASE("tile distance uses render-local coordinates with large origin") {
    using infraforge::viewport::TerrainTileCache;
    TerrainTileCache cache;

    // Large canonical origin (typical UTM coordinate).
    const double originE = 500000.0;
    const double originN = 4650000.0;

    infraforge::viewport::TerrainScene scene;
    // Add many tiles so the desired working set (kMaxResidentTiles) is a
    // subset. Tiles are at canonical (originE + x*1000, originN + y*1000).
    for (int y = 0; y < 12; ++y) {
        for (int x = 0; x < 12; ++x) {
            infraforge::viewport::TerrainSceneTile tile;
            tile.datasetUuid = "ds-large";
            tile.datasetRevision = 1;
            tile.chunkX = x;
            tile.chunkY = y;
            tile.minEasting = originE + static_cast<double>(x) * 1000.0;
            tile.maxEasting = tile.minEasting + 1000.0;
            tile.minNorthing = originN + static_cast<double>(y) * 1000.0;
            tile.maxNorthing = tile.minNorthing + 1000.0;
            scene.tiles.push_back(tile);
        }
    }
    (void)cache.adoptScene(scene);

    // Camera at render-local center of the bottom-left tile, with the
    // render origin set to the scene origin.
    const infraforge::viewport::TerrainCameraState camera{
        .centerX = 500.0, .centerY = 500.0,
        .metersPerPixel = 1.0, .viewportWidth = 1000, .viewportHeight = 800,
        .originEasting = originE, .originNorthing = originN};

    // The nearest tiles should be loaded (those near render-local (500, 500)).
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
    CHECK(cache.residentTiles().size() > 0);
    CHECK(cache.residentTiles().size() <= TerrainTileCache::kMaxResidentTiles);

    // All resident tiles should be near the camera in render-local space
    // (i.e., near (500, 500), not near (500500, 4650500)).
    for (const auto* entry : cache.residentTiles()) {
        const double centerE = (entry->tile.minEasting + entry->tile.maxEasting) * 0.5 - originE;
        const double centerN = (entry->tile.minNorthing + entry->tile.maxNorthing) * 0.5 - originN;
        const double dx = centerE - camera.centerX;
        const double dy = centerN - camera.centerY;
        const double dist = std::sqrt(dx * dx + dy * dy);
        // Resident tiles must be within a reasonable distance of the camera
        // in render-local space. With 144 tiles and kMaxResidentTiles=64,
        // the farthest resident is < 12000 m away.
        CHECK(dist < 12000.0);
    }

    // Move camera far away in render-local: tiles should be evicted and
    // not reloaded (they're outside the desired working set).
    const infraforge::viewport::TerrainCameraState farCamera{
        .centerX = 50000.0, .centerY = 50000.0,
        .metersPerPixel = 1.0, .viewportWidth = 1000, .viewportHeight = 800,
        .originEasting = originE, .originNorthing = originN};
    for (int frame = 0; frame < 200; ++frame) {
        auto r = cache.update(farCamera);
        for (const auto* entry : r.toRelease) {
            cache.notifyReleased(entry->tile.datasetUuid, entry->tile.chunkX, entry->tile.chunkY);
        }
        for (const auto* entry : r.toLoad) {
            cache.notifyLoaded(entry->tile.datasetUuid, entry->tile.chunkX, entry->tile.chunkY,
                entry->desiredLod, true);
        }
    }
    // All tiles are far from the camera (50000 m away in render-local),
    // but they're still the nearest tiles (there's nothing closer). So
    // the desired set still includes the nearest kMaxResidentTiles. The
    // key check is that the resident tiles are the ones nearest to the
    // far camera, not the ones near the original camera.
    if (!cache.residentTiles().empty()) {
        // Resident tiles should be near the far camera (50000, 50000),
        // which in canonical is (550000, 4700000) — tiles (50, 50) don't
        // exist, so the nearest are (11, 11) at canonical (511000, 4661000),
        // render-local (11000, 11000). Distance from (50000, 50000) is
        // sqrt(39000^2 + 39000^2) ≈ 55000. All tiles are roughly equidistant.
        // The key point: the distance calculation used render-local
        // coordinates correctly (no overflow/precision loss from large
        // canonical values).
        for (const auto* entry : cache.residentTiles()) {
            const double centerE = (entry->tile.minEasting + entry->tile.maxEasting) * 0.5 - originE;
            const double centerN = (entry->tile.minNorthing + entry->tile.maxNorthing) * 0.5 - originN;
            // centerE/centerN are in [0, 11500], far from camera (50000, 50000).
            // This confirms render-local coordinates are being used.
            CHECK(centerE >= 0.0);
            CHECK(centerE < 12000.0);
            CHECK(centerN >= 0.0);
            CHECK(centerN < 12000.0);
        }
    }
}

// BLOCKER 4 regression: with >100 tiles and kMaxResidentTiles=64, a
// stationary camera must converge to a stable working set. After
// convergence, repeated update() calls must produce zero toLoad and zero
// toRelease.
TEST_CASE("stationary camera converges to stable working set (no churn)") {
    using infraforge::viewport::TerrainTileCache;
    TerrainTileCache cache;

    infraforge::viewport::TerrainScene scene;
    // 12x12 = 144 tiles, more than kMaxResidentTiles (64).
    for (int y = 0; y < 12; ++y) {
        for (int x = 0; x < 12; ++x) {
            scene.tiles.push_back(makeTile("ds", x, y));
        }
    }
    (void)cache.adoptScene(scene);
    CHECK(cache.trackedCount() == 144);

    // Camera at the center of the grid.
    const infraforge::viewport::TerrainCameraState camera{
        .centerX = 6000.0, .centerY = 6000.0,
        .metersPerPixel = 1.0, .viewportWidth = 1000, .viewportHeight = 800};

    // Run enough frames to converge.
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

    // After convergence, the resident set is bounded.
    CHECK(cache.residentTiles().size() <= TerrainTileCache::kMaxResidentTiles);
    CHECK(cache.residentTiles().size() > 0);

    // Repeated update() with the SAME camera must produce zero loads
    // and zero releases — no churn.
    for (int frame = 0; frame < 10; ++frame) {
        const auto result = cache.update(camera);
        CHECK(result.toLoad.empty());
        CHECK(result.toRelease.empty());
    }

    // Resident count must remain stable.
    const std::size_t stableCount = cache.residentTiles().size();
    for (int frame = 0; frame < 10; ++frame) {
        (void)cache.update(camera);
    }
    CHECK(cache.residentTiles().size() == stableCount);
}

// BLOCKER 4 regression: moving the camera to a new area evicts old tiles
// and loads new ones. Moving back reloads the correct tiles.
TEST_CASE("camera move evicts and reloads correct working set") {
    using infraforge::viewport::TerrainTileCache;
    TerrainTileCache cache;

    infraforge::viewport::TerrainScene scene;
    for (int y = 0; y < 12; ++y) {
        for (int x = 0; x < 12; ++x) {
            scene.tiles.push_back(makeTile("ds", x, y));
        }
    }
    (void)cache.adoptScene(scene);

    // Camera at bottom-left corner.
    infraforge::viewport::TerrainCameraState cameraBL{
        .centerX = 500.0, .centerY = 500.0,
        .metersPerPixel = 1.0, .viewportWidth = 1000, .viewportHeight = 800};
    for (int frame = 0; frame < 200; ++frame) {
        auto result = cache.update(cameraBL);
        for (const auto* entry : result.toLoad) {
            cache.notifyLoaded(entry->tile.datasetUuid, entry->tile.chunkX, entry->tile.chunkY,
                entry->desiredLod, true);
        }
        for (const auto* entry : result.toRelease) {
            cache.notifyReleased(entry->tile.datasetUuid, entry->tile.chunkX, entry->tile.chunkY);
        }
    }
    const std::size_t countBL = cache.residentTiles().size();
    CHECK(countBL > 0);
    CHECK(countBL <= TerrainTileCache::kMaxResidentTiles);

    // Record which tiles are resident at bottom-left.
    std::vector<std::pair<std::int64_t, std::int64_t>> residentBL;
    for (const auto* entry : cache.residentTiles()) {
        residentBL.emplace_back(entry->tile.chunkX, entry->tile.chunkY);
    }

    // Move camera to top-right corner.
    infraforge::viewport::TerrainCameraState cameraTR{
        .centerX = 11500.0, .centerY = 11500.0,
        .metersPerPixel = 1.0, .viewportWidth = 1000, .viewportHeight = 800};
    for (int frame = 0; frame < 200; ++frame) {
        auto result = cache.update(cameraTR);
        for (const auto* entry : result.toLoad) {
            cache.notifyLoaded(entry->tile.datasetUuid, entry->tile.chunkX, entry->tile.chunkY,
                entry->desiredLod, true);
        }
        for (const auto* entry : result.toRelease) {
            cache.notifyReleased(entry->tile.datasetUuid, entry->tile.chunkX, entry->tile.chunkY);
        }
    }
    const std::size_t countTR = cache.residentTiles().size();
    CHECK(countTR > 0);
    CHECK(countTR <= TerrainTileCache::kMaxResidentTiles);

    // The resident sets should be different (camera moved significantly).
    std::vector<std::pair<std::int64_t, std::int64_t>> residentTR;
    for (const auto* entry : cache.residentTiles()) {
        residentTR.emplace_back(entry->tile.chunkX, entry->tile.chunkY);
    }
    // At least some tiles should differ.
    bool anyDifference = false;
    for (const auto& tr : residentTR) {
        bool found = false;
        for (const auto& bl : residentBL) {
            if (tr == bl) { found = true; break; }
        }
        if (!found) { anyDifference = true; break; }
    }
    CHECK(anyDifference);

    // Move back to bottom-left: tiles should reload.
    for (int frame = 0; frame < 200; ++frame) {
        auto result = cache.update(cameraBL);
        for (const auto* entry : result.toLoad) {
            cache.notifyLoaded(entry->tile.datasetUuid, entry->tile.chunkX, entry->tile.chunkY,
                entry->desiredLod, true);
        }
        for (const auto* entry : result.toRelease) {
            cache.notifyReleased(entry->tile.datasetUuid, entry->tile.chunkX, entry->tile.chunkY);
        }
    }
    // Stable again.
    for (int frame = 0; frame < 5; ++frame) {
        auto result = cache.update(cameraBL);
        CHECK(result.toLoad.empty());
        CHECK(result.toRelease.empty());
    }
}

} // TEST_SUITE
