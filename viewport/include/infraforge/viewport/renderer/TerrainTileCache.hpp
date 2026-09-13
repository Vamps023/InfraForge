#pragma once

#include "infraforge/viewport/renderer/TerrainScene.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace infraforge::viewport {

// Identity of one tracked tile: dataset + world chunk.
using TerrainTileId = std::pair<std::string, std::pair<std::int64_t, std::int64_t>>;

// Camera snapshot the streaming policy decisions are based on: an ortho
// top-down view over render-local coordinates.
struct TerrainCameraState {
    double centerX{0.0}; // render-local
    double centerY{0.0};
    double metersPerPixel{1.0};
    double viewportWidth{0.0};
    double viewportHeight{0.0};
    // Render origin in canonical space; tile bounds (which are canonical)
    // are converted to render-local by subtracting this origin before
    // distance/residency calculations. This keeps the camera and tile
    // distance math in the same render-local space as the GPU vertices.
    double originEasting{0.0};
    double originNorthing{0.0};
};

// GPU-independent terrain tile streaming policy (docs/04_RENDERER/
// VULKAN_ARCHITECTURE.md "Terrain pass"). It owns the residency state
// machine over the world-partition vocabulary (unloaded → loading →
// resident, stale on scene/revision drift, evicting → unloaded), the
// deterministic LOD decision, and the bounded working set. File loading
// and GPU uploads are executed by the pass that owns this policy; this
// type only decides WHAT must be resident and at which LOD.
class TerrainTileCache {
public:
    // Residency states mirror domain::world::ChunkResidencyState names; the
    // renderer tracks the same lifecycle locally because the viewport
    // process owns its own derived-state lifetime.
    enum class Residency : std::uint8_t {
        Unloaded,
        Loading,
        Resident,
        // LOD replacement: the tile is drawable at loadedLod (old LOD) while
        // a new LOD load is in flight at desiredLod. This avoids unload
        // flicker — the old GPU payload stays drawable until the new LOD is
        // successfully prepared, then atomically replaces it.
        LodReplacing,
        Stale,
        Evicting,
    };

    [[nodiscard]] static std::string_view residencyName(Residency residency) noexcept;

    // One tracked tile: manifest entry plus derived streaming state.
    struct Entry {
        TerrainSceneTile tile;
        Residency residency{Residency::Unloaded};
        std::uint32_t loadedLod{0};     // LOD of resident/queued content
        std::uint32_t desiredLod{0};    // LOD the current camera metric wants
        std::uint64_t failedLoads{0};
        bool releaseQueued{false};      // GPU release handed to the pass once
        // LOD replacement in-flight flag: when LodReplacing and true, a load
        // at desiredLod is already queued and the tile should not be
        // re-selected until notifyLoaded resolves it.
        bool lodLoadInProgress{false};
    };

    // Upper bound of resident tiles — GPU residency scales with the working
    // set, never with total project extent (PRD performance budget).
    static constexpr std::size_t kMaxResidentTiles = 64;
    // Tile loads (file decode + upload) allowed per update; keeps a frame
    // from stalling when the working set changes abruptly.
    static constexpr std::size_t kMaxLoadsPerUpdate = 2;

    // Replaces the scene manifest. New tiles are tracked as unloaded, tiles
    // absent from the new scene are evicted (released, then dropped), and
    // present tiles whose dataset revision moved become stale. Returns the
    // number of tiles marked stale by the switch.
    std::size_t adoptScene(const TerrainScene& scene);

    // Advances the streaming decisions for one frame. Returns the tiles the
    // pass must load now (at most kMaxLoadsPerUpdate, at their desired LOD)
    // and the tiles whose GPU buffers must be released.
    struct UpdateResult {
        std::vector<const Entry*> toLoad;
        std::vector<const Entry*> toRelease;
    };
    [[nodiscard]] UpdateResult update(const TerrainCameraState& camera);

    // The pass reports a completed load; content that no longer matches the
    // desired LOD (or a stale scene) is re-evaluated on the next update.
    void notifyLoaded(const std::string& datasetUuid, std::int64_t chunkX, std::int64_t chunkY,
        std::uint32_t lod, bool success);

    // The pass confirms the GPU buffer of an evicted tile was destroyed.
    // Tiles still in the manifest return to Unloaded (reload on demand);
    // tiles removed with their old scene are dropped from the cache.
    void notifyReleased(const std::string& datasetUuid, std::int64_t chunkX, std::int64_t chunkY);

    [[nodiscard]] std::vector<const Entry*> residentTiles() const;
    [[nodiscard]] std::size_t trackedCount() const noexcept { return entries_.size(); }
    [[nodiscard]] const TerrainScene& scene() const noexcept { return scene_; }

    // Deterministic LOD rule: the coarsest level that still resolves at
    // roughly one height sample per screen pixel. level =
    // clamp(floor(log2(mpp / level0Spacing)) + 1, 0, kLodCount - 1);
    // zoomed-in views stay at level 0.
    static constexpr std::uint32_t kLodCount = 5;
    static constexpr double kLevel0Samples = 128.0; // 129 vertices per side
    [[nodiscard]] static std::uint32_t lodForSpacing(
        double metersPerPixel, double level0Spacing);

private:
    [[nodiscard]] std::optional<std::size_t> indexOf(
        const std::string& datasetUuid, std::int64_t chunkX, std::int64_t chunkY) const;
    [[nodiscard]] double tileCenterDistanceSquared(const Entry& entry, const TerrainCameraState& camera) const;
    [[nodiscard]] bool presentInScene(const Entry& entry) const;
    void rebuildIndex();

    TerrainScene scene_;
    std::map<TerrainTileId, std::size_t> index_;
    std::vector<Entry> entries_;
};

} // namespace infraforge::viewport
