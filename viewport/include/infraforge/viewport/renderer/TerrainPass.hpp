#pragma once

#include "infraforge/domain/geo/RenderLocalFrame.hpp"
#include "infraforge/domain/terrain/TerrainTileFile.hpp"
#include "infraforge/viewport/renderer/GridCamera.hpp"
#include "infraforge/viewport/renderer/TerrainScene.hpp"
#include "infraforge/viewport/renderer/TerrainTileCache.hpp"
#include "infraforge/viewport/renderer/Vulkan.hpp"

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace infraforge::viewport {

// GPU half of the terrain pass: owns the terrain pipeline, the streaming
// policy (TerrainTileCache), the decoded CPU tile payloads, and one vertex+
// index buffer pair per resident tile/LOD. Update (load/evict) runs on the
// render thread only; scene swaps arrive from the control path through the
// thread-safe setScene.
//
// Precision contract: tile grid parameters are doubles in canonical space;
// the origin-relative conversion runs in double precision through the
// shared RenderLocalFrame and only the result is stored as float — the
// double→GPU boundary sits exactly here.
class TerrainPass final {
public:
    TerrainPass() = default;
    ~TerrainPass() = default;

    TerrainPass(const TerrainPass&) = delete;
    TerrainPass& operator=(const TerrainPass&) = delete;

    void create(
        const VkPhysicalDevice physical,
        const VkDevice device,
        const VkQueue queue,
        const std::uint32_t queueFamily,
        const VkRenderPass renderPass);
    void destroy();

    // Thread-safe: replaces the pending scene; adopted on the render thread.
    void setScene(const TerrainScene& scene);

    // Render thread: adopt pending scene, advance residency, execute the
    // bounded load/release lists (file decode + buffer upload/release).
    void update(const GridCamera& camera);

    // Render thread: bind the pipeline and draw resident tiles.
    void record(const VkCommandBuffer command, const GridCamera& camera) const;

    [[nodiscard]] bool created() const noexcept { return device_ != VK_NULL_HANDLE; }
    [[nodiscard]] std::size_t residentCount() const noexcept { return cache_.residentTiles().size(); }

private:
    struct TileGpu {
        VkBuffer vertexBuffer{VK_NULL_HANDLE};
        VkDeviceMemory vertexMemory{VK_NULL_HANDLE};
        VkBuffer indexBuffer{VK_NULL_HANDLE};
        VkDeviceMemory indexMemory{VK_NULL_HANDLE};
        std::uint32_t indexCount{0};
    };

    // CPU payload of a loaded tile (decoded file kept so LOD rebuilds do
    // not re-read the file). Bounded by the residency budget.
    struct TilePayload {
        std::string datasetUuid;
        std::uint64_t datasetRevision{0};
        std::uint32_t lod{0};
        std::vector<float> vertices; // pos(3) + normal(3)
        std::vector<std::uint32_t> indices;
        std::optional<TileGpu> gpu;
    };

    [[nodiscard]] TilePayload buildPayload(
        const TerrainSceneTile& tile, const std::string& filePath, std::uint32_t lod) const;
    [[nodiscard]] TileGpu uploadPayload(const TilePayload& payload) const;
    void destroyGpu(TileGpu& gpu) const;
    void logTileFailure(const TerrainSceneTile& tile, std::string_view detail) const;

    VkPhysicalDevice physical_{VK_NULL_HANDLE};
    VkDevice device_{VK_NULL_HANDLE};
    VkQueue queue_{VK_NULL_HANDLE};
    std::uint32_t queueFamily_{0};
    VkRenderPass renderPass_{VK_NULL_HANDLE};

    UniqueVulkan<VkPipelineLayout> pipelineLayout_;
    UniqueVulkan<VkPipeline> pipeline_;

    TerrainTileCache cache_;
    // Keyed by tile id; only resident tiles hold payloads.
    std::map<TerrainTileId, TilePayload> payloads_;
    // Double-precision render-local frame anchored at the scene origin.
    std::optional<domain::geo::RenderLocalFrame> renderFrame_;

    std::mutex sceneMutex_;
    std::optional<TerrainScene> pendingScene_;
};

} // namespace infraforge::viewport
