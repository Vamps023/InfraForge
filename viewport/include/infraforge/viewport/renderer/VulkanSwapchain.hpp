#pragma once

#include "infraforge/viewport/renderer/Vulkan.hpp"

#include <cstddef>
#include <vector>

namespace infraforge::viewport {

// RAII swapchain with per-image views and framebuffers. Recreation is a
// normal operation driven by the swapchain state machine; the render pass is
// owned here because framebuffers depend on it for this viewport's single
// grid pass.
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

    // Destroys framebuffers/views/swapchain (in that order) for recreation
    // or shutdown. Safe to call when nothing is created.
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

    VkPhysicalDevice physical_{VK_NULL_HANDLE};
    VkDevice device_{VK_NULL_HANDLE};
    VkSurfaceKHR surface_{VK_NULL_HANDLE};
    std::uint32_t graphicsFamily_{0};
    VkRenderPass renderPass_{VK_NULL_HANDLE};

    UniqueVulkan<VkSwapchainKHR> swapchain_;
    UniqueVulkan<VkRenderPass> ownedRenderPass_;
    std::vector<VkImage> images_;
    std::vector<UniqueVulkan<VkImageView>> imageViews_;
    std::vector<VkFramebuffer> framebuffers_;
    VkFormat format_{VK_FORMAT_B8G8R8A8_UNORM};
    VkExtent2D extent_{};
};

} // namespace infraforge::viewport
