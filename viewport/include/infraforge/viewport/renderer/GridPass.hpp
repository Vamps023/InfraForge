#pragma once

#include "infraforge/viewport/renderer/GridCamera.hpp"
#include "infraforge/viewport/renderer/Vulkan.hpp"

namespace infraforge::viewport {

// Renders the world-grid debug pass: one line-list draw over a cleared
// swapchain image. The pipeline, vertex buffer, and shader modules are owned
// RAII; the pass consumes the camera projection per frame via push constants.
class GridPass {
public:
    GridPass() = default;
    ~GridPass() = default;

    GridPass(const GridPass&) = delete;
    GridPass& operator=(const GridPass&) = delete;

    void create(
        VkPhysicalDevice physical,
        VkDevice device,
        VkQueue queue,
        std::uint32_t queueFamily,
        VkRenderPass renderPass);
    void destroy();

    // Records the clear + grid draw for `framebuffer` into `commandBuffer`.
    void recordFrame(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer, const GridCamera& camera) const;

private:
    void createVertexBuffer();
    void createPipeline();

    VkPhysicalDevice physical_{VK_NULL_HANDLE};
    VkDevice device_{VK_NULL_HANDLE};
    VkQueue queue_{VK_NULL_HANDLE};
    std::uint32_t queueFamily_{0};
    VkRenderPass renderPass_{VK_NULL_HANDLE};

    UniqueVulkan<VkShaderModule> vertexShader_;
    UniqueVulkan<VkShaderModule> fragmentShader_;
    UniqueVulkan<VkPipelineLayout> pipelineLayout_;
    UniqueVulkan<VkPipeline> pipeline_;
    UniqueVulkan<VkBuffer> vertexBuffer_;
    UniqueVulkan<VkDeviceMemory> vertexMemory_;
    VkDeviceSize vertexCount_{0};
};

} // namespace infraforge::viewport
