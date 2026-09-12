#pragma once

#include "infraforge/viewport/renderer/Vulkan.hpp"

#include <string>
#include <vector>

namespace infraforge::viewport {

struct VulkanInstanceInfo {
    std::string apiVersionText;   // "1.3.280" style, from the loader/physical device
    bool validationEnabled{false};
};

// Vulkan 1.3 instance with platform surface extensions and optional
// development validation (KHONOS validation layer + debug messenger).
// Capability shortfalls fail with actionable messages; nothing is silently
// downgraded.
class VulkanInstance {
public:
    VulkanInstance() = default;
    ~VulkanInstance() = default;

    VulkanInstance(const VulkanInstance&) = delete;
    VulkanInstance& operator=(const VulkanInstance&) = delete;

    // Requires a Vulkan 1.3-capable loader. When validation is requested the
    // KHONOS layer must be present; otherwise creation fails explicitly.
    void create(bool validationEnabled);

    [[nodiscard]] VkInstance get() const noexcept { return instance_.get(); }
    [[nodiscard]] const VulkanInstanceInfo& info() const noexcept { return info_; }

private:
    UniqueVulkan<VkInstance> instance_;
    UniqueVulkan<VkDebugUtilsMessengerEXT> messenger_;
    VulkanInstanceInfo info_;
};

} // namespace infraforge::viewport
