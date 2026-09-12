#pragma once

#include "infraforge/viewport/renderer/Vulkan.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace infraforge::viewport {

struct VulkanDeviceInfo {
    std::string deviceName;
    std::string apiVersionText;
    std::string deviceTypeText;
};

// Physical-device selection plus the logical device and the single queue
// family used for graphics and presentation. Selection prefers discrete
// hardware and rejects devices that cannot meet the Vulkan 1.3 baseline.
class VulkanDevice {
public:
    VulkanDevice() = default;
    ~VulkanDevice() = default;

    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;

    void create(VkInstance instance, VkSurfaceKHR surface);

    [[nodiscard]] VkDevice get() const noexcept { return device_.get(); }
    [[nodiscard]] VkPhysicalDevice physical() const noexcept { return physical_; }
    [[nodiscard]] VkQueue queue() const noexcept { return queue_; }
    [[nodiscard]] std::uint32_t queueFamily() const noexcept { return queueFamily_; }
    [[nodiscard]] const VulkanDeviceInfo& info() const noexcept { return info_; }

private:
    VkPhysicalDevice physical_{VK_NULL_HANDLE};
    UniqueVulkan<VkDevice> device_;
    VkQueue queue_{VK_NULL_HANDLE};
    std::uint32_t queueFamily_{0};
    VulkanDeviceInfo info_;
};

} // namespace infraforge::viewport
