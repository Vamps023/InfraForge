#pragma once

#include "infraforge/viewport/renderer/Vulkan.hpp"

#include <cstdint>

#include <cstddef>
#include <vector>

namespace infraforge::viewport {

// RAII swapchain with per-image views, per-image depth attachments, and
// framebuffers over (color, depth). Recreation is a normal operation driven
// by the swapchain state machine; the render pass is owned here because
// framebuffers depend on it for this viewport's terrain + grid passes.
class VulkanSwapchain {
public:
    VulkanSwapchain() = default;
    ~VulkanSwapchain() = default;

    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

    void create(
        VkPhysicalDevice physical,
        VkDevice device,
        VkSurfaceKHR surface,
        std::uint32_t graphicsFamily,
        VkRenderPass renderPass,
        std::uint32_t width,
        std::uint32_t height);

    // Destroys framebuffers/depth images/views/swapchain (in that order) for
    // recreation or shutdown. Safe to call when nothing is created.
    void destroy();

    [[nodiscard]] VkSwapchainKHR get() const noexcept { return swapchain_.get(); }
    [[nodiscard]] VkRenderPass renderPass() const noexcept { return renderPass_; }
    [[nodiscard]] const std::vector<VkFramebuffer>& framebuffers() const noexcept { return framebuffers_; }
    [[nodiscard]] VkFormat format() const noexcept { return format_; }
    [[nodiscard]] VkExtent2D extent() const noexcept { return extent_; }
    [[nodiscard]] std::size_t imageCount() const noexcept { return framebuffers_.size(); }

private:
    void createRenderPass();
    void createImageViewsAndFramebuffers();
    [[nodiscard]] VkFormat selectDepthFormat() const;

    VkPhysicalDevice physical_{VK_NULL_HANDLE};
    VkDevice device_{VK_NULL_HANDLE};
    VkSurfaceKHR surface_{VK_NULL_HANDLE};
    std::uint32_t graphicsFamily_{0};
    VkRenderPass renderPass_{VK_NULL_HANDLE};

    UniqueVulkan<VkSwapchainKHR> swapchain_;
    UniqueVulkan<VkRenderPass> ownedRenderPass_;
    std::vector<VkImage> images_;
    std::vector<UniqueVulkan<VkImageView>> imageViews_;
    // Per swapchain image: depth image + memory + view (terrain pass needs
    // depth testing; the grid pass benefits from it against terrain).
    std::vector<VkImage> depthImages_;
    std::vector<UniqueVulkan<VkDeviceMemory>> depthMemories_;
    std::vector<UniqueVulkan<VkImageView>> depthViews_;
    std::vector<VkFramebuffer> framebuffers_;
    // The (format, colorspace) pair the surface actually reported; halves
    // must never be combined across supported pairs.
    VkFormat format_{VK_FORMAT_B8G8R8A8_UNORM};
    VkColorSpaceKHR colorSpace_{VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    VkFormat depthFormat_{VK_FORMAT_D32_SFLOAT};
    VkExtent2D extent_{};
};

} // namespace infraforge::viewport
