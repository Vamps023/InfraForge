#include "infraforge/viewport/renderer/TerrainTileCache.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace infraforge::viewport {
namespace {

[[nodiscard]] TerrainTileId keyOf(
    const std::string& datasetUuid, const std::int64_t chunkX, const std::int64_t chunkY) {
    return {datasetUuid, {chunkX, chunkY}};
}

} // namespace

std::string_view TerrainTileCache::residencyName(const Residency residency) noexcept {
    switch (residency) {
    case Residency::Unloaded:
        return "unloaded";
    case Residency::Loading:
        return "loading";
    case Residency::Resident:
        return "resident";
    case Residency::LodReplacing:
        return "lod_replacing";
    case Residency::Stale:
        return "stale";
    case Residency::Evicting:
        return "evicting";
    }
    return "unknown";
}

std::size_t TerrainTileCache::adoptScene(const TerrainScene& scene) {
    scene_ = scene;

    std::size_t staled = 0;
    for (Entry& entry : entries_) {
        const auto tile = std::find_if(scene.tiles.begin(), scene.tiles.end(),
            [&](const TerrainSceneTile& candidate) {
                return candidate.datasetUuid == entry.tile.datasetUuid
                    && candidate.chunkX == entry.tile.chunkX
                    && candidate.chunkY == entry.tile.chunkY;
            });
        if (tile == scene.tiles.end()) {
            // Removed with the scene: release, then drop on notifyReleased.
            entry.residency = Residency::Evicting;
            entry.releaseQueued = false;
            continue;
        }
        const bool revisionMoved = tile->datasetRevision != entry.tile.datasetRevision;
        entry.tile = *tile;
        if (revisionMoved
            && (entry.residency == Residency::Resident
                || entry.residency == Residency::LodReplacing
                || entry.residency == Residency::Loading)) {
            // Resident content derived from a superseded revision must not be
            // shown again; the tile reloads from the regenerated file.
            entry.residency = Residency::Stale;
            entry.lodLoadInProgress = false;
            ++staled;
        }
    }

    for (const TerrainSceneTile& tile : scene.tiles) {
        if (!indexOf(tile.datasetUuid, tile.chunkX, tile.chunkY).has_value()) {
            Entry entry;
            entry.tile = tile;
            entries_.push_back(std::move(entry));
        }
    }
    rebuildIndex();
    return staled;
}

std::optional<std::size_t> TerrainTileCache::indexOf(
    const std::string& datasetUuid, const std::int64_t chunkX, const std::int64_t chunkY) const {
    const auto found = index_.find(keyOf(datasetUuid, chunkX, chunkY));
    if (found == index_.end()) {
        return std::nullopt;
    }
    return found->second;
}

bool TerrainTileCache::presentInScene(const Entry& entry) const {
    return std::any_of(scene_.tiles.begin(), scene_.tiles.end(),
        [&](const TerrainSceneTile& candidate) {
            return candidate.datasetUuid == entry.tile.datasetUuid
                && candidate.chunkX == entry.tile.chunkX
                && candidate.chunkY == entry.tile.chunkY;
        });
}

void TerrainTileCache::rebuildIndex() {
    index_.clear();
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const Entry& entry = entries_[i];
        index_.emplace(keyOf(entry.tile.datasetUuid, entry.tile.chunkX, entry.tile.chunkY), i);
    }
}

double TerrainTileCache::tileCenterDistanceSquared(const Entry& entry, const TerrainCameraState& camera) const {
    // Convert tile canonical bounds to render-local before computing
    // distance, so the camera (render-local) and tile centers are in the
    // same coordinate space. BLOCKER 12: terrain vertices use
    // Render Y = -north, so the tile center Y must be negated to match.
    const double centerE = (entry.tile.minEasting + entry.tile.maxEasting) * 0.5 - camera.originEasting;
    const double centerN = (entry.tile.minNorthing + entry.tile.maxNorthing) * 0.5 - camera.originNorthing;
    const double renderY = -centerN;
    const double dx = centerE - camera.centerX;
    const double dy = renderY - camera.centerY;
    return dx * dx + dy * dy;
}

std::uint32_t TerrainTileCache::lodForSpacing(const double metersPerPixel, const double level0Spacing) {
    if (!(metersPerPixel > 0.0) || !(level0Spacing > 0.0)) {
        return 0;
    }
    // One height sample per screen pixel is the reference density; the view
    // can go up to 2x coarser at level 0, then drops one LOD per factor-2
    // zoom-out, deterministically from the camera metric alone.
    const double ratio = metersPerPixel / level0Spacing;
    if (ratio < 2.0) {
        return 0;
    }
    const double level = std::floor(std::log2(ratio));
    const double clamped = std::clamp(level, 0.0, static_cast<double>(kLodCount - 1));
    return static_cast<std::uint32_t>(clamped);
}

TerrainTileCache::UpdateResult TerrainTileCache::update(const TerrainCameraState& camera) {
    UpdateResult result;

    const auto queueRelease = [&result](Entry& entry) {
        if (entry.residency == Residency::Evicting && !entry.releaseQueued) {
            entry.releaseQueued = true;
            result.toRelease.push_back(&entry);
        }
    };

    // 1. Tiles already evicted (scene removal or budget pressure) whose GPU
    // release is still outstanding.
    for (Entry& entry : entries_) {
        queueRelease(entry);
    }

    // 2. Desired LOD per tile under the current camera metric.
    for (Entry& entry : entries_) {
        if (entry.residency == Residency::Evicting) {
            continue;
        }
        const double extent = entry.tile.maxEasting - entry.tile.minEasting;
        entry.desiredLod = lodForSpacing(camera.metersPerPixel, extent / kLevel0Samples);
        // BLOCKER 6: a Resident tile at the wrong LOD needs an LOD replacement.
        // Transition to LodReplacing so it becomes a load candidate while
        // remaining drawable at its old LOD (no flicker).
        if (entry.residency == Residency::Resident && entry.loadedLod != entry.desiredLod) {
            entry.residency = Residency::LodReplacing;
            entry.lodLoadInProgress = false;
        }
    }

    // BLOCKER 4: Compute the desired working set — the kMaxResidentTiles
    // nearest tiles to the camera. Only tiles in the desired set are
    // eligible for loading. Tiles outside the desired set that are currently
    // resident are evicted. This prevents churn: a budget-evicted tile that
    // returns to Unloaded is NOT in the desired set, so it won't be reloaded
    // on the next update while the camera is stationary.
    //
    // The desired set is purely distance-based: the nearest kMaxResidentTiles
    // tiles regardless of current residency. GPU-holding tiles outside the
    // desired set are evicted; non-GPU-holding tiles inside the desired set
    // are load candidates.
    std::vector<std::size_t> allByDistance;
    allByDistance.reserve(entries_.size());
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].residency != Residency::Evicting) {
            allByDistance.push_back(i);
        }
    }
    std::sort(allByDistance.begin(), allByDistance.end(), [&](std::size_t a, std::size_t b) {
        return tileCenterDistanceSquared(entries_[a], camera)
            < tileCenterDistanceSquared(entries_[b], camera);
    });

    // The desired working set is the nearest kMaxResidentTiles tiles.
    const std::size_t desiredCapacity = std::min(allByDistance.size(), kMaxResidentTiles);
    std::vector<bool> inDesiredSet(entries_.size(), false);
    for (std::size_t i = 0; i < desiredCapacity; ++i) {
        inDesiredSet[allByDistance[i]] = true;
    }

    // 3. Evict resident tiles that are no longer in the desired working set.
    // This prevents stale residents from consuming GPU memory when the
    // camera has moved away.
    // BLOCKER 13: Evict any GPU-holding tile outside the desired set,
    // including LodReplacing tiles whose loadedLod != desiredLod. A tile
    // that becomes undesired during LOD replacement must not keep an old
    // GPU payload alive.
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        Entry& entry = entries_[i];
        if (!inDesiredSet[i]
            && (entry.residency == Residency::Resident
                || entry.residency == Residency::LodReplacing)) {
            // If a LOD replacement load is in progress, mark it stale so
            // the completion does not resurrect an undesired tile.
            if (entry.lodLoadInProgress) {
                entry.lodLoadInProgress = false;
            }
            entry.residency = Residency::Evicting;
            queueRelease(entry);
        }
    }

    // 4. Loads: stale tiles first (freshest content), then unloaded tiles
    // and LOD-replacing tiles nearest the camera center — but ONLY tiles in
    // the desired working set. Bounded per update so a working-set change
    // cannot stall the frame.
    std::vector<std::size_t> candidates;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (!inDesiredSet[i]) {
            continue;
        }
        const Entry& entry = entries_[i];
        if (entry.residency == Residency::Unloaded || entry.residency == Residency::Stale) {
            candidates.push_back(i);
        } else if (entry.residency == Residency::LodReplacing && !entry.lodLoadInProgress) {
            candidates.push_back(i);
        }
    }
    std::sort(candidates.begin(), candidates.end(), [&](std::size_t a, std::size_t b) {
        const auto priority = [](const Entry& e) {
            if (e.residency == Residency::Stale) return 0;
            if (e.residency == Residency::LodReplacing) return 1;
            return 2; // Unloaded
        };
        const int pa = priority(entries_[a]);
        const int pb = priority(entries_[b]);
        if (pa != pb) {
            return pa < pb;
        }
        return tileCenterDistanceSquared(entries_[a], camera)
            < tileCenterDistanceSquared(entries_[b], camera);
    });
    const std::size_t loadingNow = std::min(candidates.size(), kMaxLoadsPerUpdate);
    for (std::size_t i = 0; i < loadingNow; ++i) {
        Entry& entry = entries_[candidates[i]];
        if (entry.residency == Residency::LodReplacing) {
            // LOD replacement: keep the old payload drawable at loadedLod;
            // the pass loads at desiredLod and notifyLoaded atomically
            // replaces the LOD on success.
            entry.lodLoadInProgress = true;
        } else {
            // Initial load or stale reload: transition to Loading.
            entry.residency = Residency::Loading;
            entry.loadedLod = entry.desiredLod;
        }
        result.toLoad.push_back(&entry);
    }

    // 5. Budget pressure: when the resident set would exceed the bound, the
    // farthest up-to-date tiles are evicted first. In-flight loads are never
    // evicted — their completion is version-checked instead. LodReplacing
    // tiles count toward the resident budget (they hold GPU payloads).
    // This is a safety net; the desired-set eviction in step 3 should handle
    // most cases, but in-flight loads that complete can temporarily exceed
    // the budget.
    // BLOCKER 13: Count all GPU-holding tiles, including LodReplacing tiles
    // with mismatched LODs (they still hold an old GPU payload).
    std::size_t usefulResident = 0;
    for (const Entry& entry : entries_) {
        if (entry.residency == Residency::Resident
            || entry.residency == Residency::LodReplacing) {
            ++usefulResident;
        }
    }
    if (usefulResident > kMaxResidentTiles) {
        std::vector<std::size_t> evictable;
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            const Entry& entry = entries_[i];
            if (entry.residency == Residency::Resident
                || entry.residency == Residency::LodReplacing) {
                evictable.push_back(i);
            }
        }
        std::sort(evictable.begin(), evictable.end(), [&](std::size_t a, std::size_t b) {
            return tileCenterDistanceSquared(entries_[a], camera)
                > tileCenterDistanceSquared(entries_[b], camera);
        });
        std::size_t excess = usefulResident - kMaxResidentTiles;
        for (const std::size_t index : evictable) {
            if (excess == 0) {
                break;
            }
            Entry& entry = entries_[index];
            entry.residency = Residency::Evicting;
            queueRelease(entry);
            --excess;
        }
    }

    return result;
}

void TerrainTileCache::notifyLoaded(
    const std::string& datasetUuid, const std::int64_t chunkX, const std::int64_t chunkY,
    const std::uint32_t lod, const bool success) {
    const auto index = indexOf(datasetUuid, chunkX, chunkY);
    if (!index.has_value()) {
        throw std::out_of_range("notifyLoaded for an untracked terrain tile");
    }
    Entry& entry = entries_[*index];
    // BLOCKER 13: A stale load completion must not resurrect an undesired
    // or evicted tile. If the tile was evicted while the load was in
    // flight, drop the result silently.
    if (entry.residency == Residency::Evicting
        || entry.residency == Residency::Unloaded
        || entry.residency == Residency::Stale) {
        entry.lodLoadInProgress = false;
        return;
    }
    if (entry.residency == Residency::LodReplacing) {
        // LOD replacement completion: on success, atomically replace the LOD;
        // on failure, retain the old resident payload (no flicker, no drop).
        entry.lodLoadInProgress = false;
        if (success) {
            entry.loadedLod = lod;
            entry.failedLoads = 0;
            entry.residency = Residency::Resident;
        } else {
            ++entry.failedLoads;
            // Keep old loadedLod and Resident state; the next update() will
            // re-queue LOD replacement if the camera still wants a different LOD.
            entry.residency = Residency::Resident;
        }
        return;
    }
    if (success) {
        entry.loadedLod = lod;
        entry.failedLoads = 0;
        // A scene switch during the load may have staled the tile; content
        // becomes resident only while the load is still current.
        if (entry.residency == Residency::Loading) {
            entry.residency = Residency::Resident;
        }
    } else {
        ++entry.failedLoads;
        entry.residency = Residency::Unloaded;
    }
}

void TerrainTileCache::notifyReleased(
    const std::string& datasetUuid, const std::int64_t chunkX, const std::int64_t chunkY) {
    const auto index = indexOf(datasetUuid, chunkX, chunkY);
    if (!index.has_value()) {
        return; // already dropped
    }
    Entry& entry = entries_[*index];
    if (entry.residency != Residency::Evicting) {
        return;
    }
    entry.lodLoadInProgress = false;
    if (presentInScene(entry)) {
        // Budget eviction of a manifest tile: may reload on demand.
        entry.residency = Residency::Unloaded;
        entry.releaseQueued = false;
        return;
    }
    // Removed with its scene: drop the tracked entry entirely.
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(*index));
    rebuildIndex();
}

std::vector<const TerrainTileCache::Entry*> TerrainTileCache::residentTiles() const {
    std::vector<const Entry*> resident;
    for (const Entry& entry : entries_) {
        // LodReplacing tiles are drawable at their old LOD while the new LOD
        // load is in flight.
        if (entry.residency == Residency::Resident || entry.residency == Residency::LodReplacing) {
            resident.push_back(&entry);
        }
    }
    return resident;
}

} // namespace infraforge::viewport
