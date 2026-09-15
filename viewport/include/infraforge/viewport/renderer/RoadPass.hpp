#pragma once

#include "infraforge/viewport/renderer/EditorCamera.hpp"
#include "infraforge/viewport/renderer/RoadScene.hpp"
#include "infraforge/viewport/renderer/Vulkan.hpp"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace infraforge::viewport {

// GPU half of the road pass: owns the road pipeline and one vertex+index
// buffer pair per road mesh. Update (upload/release) runs on the render
// thread only; scene swaps arrive from the control path through the
// thread-safe setScene.
//
// Precision contract: road vertices arrive as render-local float
// coordinates (the double-precision origin-relative conversion happened
// on the CPU before transport); the viewport does not touch canonical
// space.
class RoadPass final {
public:
    RoadPass() = default;
    ~RoadPass();

    RoadPass(const RoadPass&) = delete;
    RoadPass& operator=(const RoadPass&) = delete;

    void create(
        VkPhysicalDevice physical,
        VkDevice device,
        VkQueue queue,
        std::uint32_t queueFamily,
        VkRenderPass renderPass);
    void destroy();

    // Thread-safe: replaces the pending scene; adopted on the render thread.
    void setScene(const RoadScene& scene);

    // Render thread: adopt pending scene, upload/release meshes.
    void update(const EditorCamera& camera);

    // Render thread: bind the pipeline and draw all road meshes.
    void record(VkCommandBuffer command, const EditorCamera& camera) const;

    [[nodiscard]] bool created() const noexcept { return device_ != VK_NULL_HANDLE; }
    [[nodiscard]] std::size_t meshCount() const noexcept { return meshes_.size(); }

private:
    struct MeshGpu {
        VkBuffer vertexBuffer{VK_NULL_HANDLE};
        VkDeviceMemory vertexMemory{VK_NULL_HANDLE};
        VkBuffer indexBuffer{VK_NULL_HANDLE};
        VkDeviceMemory indexMemory{VK_NULL_HANDLE};
        std::uint32_t indexCount{0};
        std::uint32_t vertexCount{0};
    };

    struct MeshEntry {
        std::string roadId;
        MeshGpu gpu;
    };

    void uploadMesh(const RoadSceneMesh& mesh);
    void releaseMesh(MeshGpu& gpu);

    VkPhysicalDevice physical_{VK_NULL_HANDLE};
    VkDevice device_{VK_NULL_HANDLE};
    VkQueue queue_{VK_NULL_HANDLE};
    std::uint32_t queueFamily_{0};
    VkRenderPass renderPass_{VK_NULL_HANDLE};

    UniqueVulkan<VkPipelineLayout> pipelineLayout_;
    UniqueVulkan<VkPipeline> pipeline_;

    std::vector<MeshEntry> meshes_;
    std::mutex sceneMutex_;
    std::optional<RoadScene> pendingScene_;
};

} // namespace infraforge::viewport
