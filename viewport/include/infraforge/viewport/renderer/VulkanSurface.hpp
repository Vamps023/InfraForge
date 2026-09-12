#pragma once

#include "infraforge/viewport/renderer/Vulkan.hpp"

#include <cstdint>

namespace infraforge::viewport {

// Owns the VkSurfaceKHR bound to the native child surface. Platform-specific
// creation lives in the per-platform translation units; on platforms without
// an implementation creation fails explicitly (Windows is the first runtime
// acceptance platform).
class VulkanSurface {
public:
    VulkanSurface() = default;
    ~VulkanSurface() = default;

    VulkanSurface(const VulkanSurface&) = delete;
    VulkanSurface& operator=(const VulkanSurface&) = delete;

    void create(VkInstance instance, std::uint64_t nativeWindowHandle);

    [[nodiscard]] VkSurfaceKHR get() const noexcept { return surface_.get(); }

private:
    UniqueVulkan<VkSurfaceKHR> surface_;
};

} // namespace infraforge::viewport
