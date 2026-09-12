#include "infraforge/viewport/renderer/VulkanDevice.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace infraforge::viewport {
namespace {

std::string formatApiVersion(const std::uint32_t version) {
    return std::to_string(VK_API_VERSION_MAJOR(version)) + "."
        + std::to_string(VK_API_VERSION_MINOR(version)) + "."
        + std::to_string(VK_API_VERSION_PATCH(version));
}

std::string deviceTypeName(const VkPhysicalDeviceType type) {
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "discrete";
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated";
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return "software";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "virtual";
    default: return "other";
    }
}

std::vector<VkPhysicalDevice> enumeratePhysicalDevices(VkInstance instance) {
    std::uint32_t count = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &count, nullptr), "physical device probe");
    if (count == 0) {
        throw RendererError(
            "no Vulkan physical devices were found; install or enable a Vulkan 1.3-capable GPU driver",
            VK_ERROR_INITIALIZATION_FAILED);
    }
    std::vector<VkPhysicalDevice> devices(count);
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "physical device enumeration");
    return devices;
}

struct DeviceCandidate {
    VkPhysicalDevice device;
    int score;
    VulkanDeviceInfo info;
    std::uint32_t queueFamily;
};

std::optional<DeviceCandidate> evaluateCandidate(VkPhysicalDevice device, VkSurfaceKHR surface) {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device, &properties);
    if (properties.apiVersion < VK_API_VERSION_1_3) {
        return std::nullopt;
    }

    std::uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, families.data());

    std::optional<std::uint32_t> combinedFamily;
    for (std::uint32_t family = 0; family < familyCount; ++family) {
        if ((families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) {
            continue;
        }
        VkBool32 presentSupported = VK_FALSE;
        VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(device, family, surface, &presentSupported),
            "surface support probe");
        if (presentSupported == VK_TRUE) {
            combinedFamily = family;
            break;
        }
    }
    if (!combinedFamily.has_value()) {
        return std::nullopt;
    }

    VkPhysicalDeviceFeatures2 features{};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    vkGetPhysicalDeviceFeatures2(device, &features);

    VulkanDeviceInfo info;
    info.deviceName = properties.deviceName;
    info.apiVersionText = formatApiVersion(properties.apiVersion);
    info.deviceTypeText = deviceTypeName(properties.deviceType);

    int score = 1;
    switch (properties.deviceType) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: score = 1000; break;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: score = 100; break;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: score = 50; break;
    default: score = 1; break;
    }

    return DeviceCandidate{
        .device = device,
        .score = score,
        .info = std::move(info),
        .queueFamily = *combinedFamily,
    };
}

} // namespace

void VulkanDevice::create(const VkInstance instance, const VkSurfaceKHR surface) {
    auto candidates = enumeratePhysicalDevices(instance);
    std::vector<DeviceCandidate> usable;
    for (const VkPhysicalDevice device : candidates) {
        auto candidate = evaluateCandidate(device, surface);
        if (candidate.has_value()) {
            usable.push_back(std::move(*candidate));
        }
    }
    if (usable.empty()) {
        throw RendererError(
            "no physical device satisfies the Vulkan 1.3 baseline with graphics+presentation support",
            VK_ERROR_INITIALIZATION_FAILED);
    }
    std::ranges::sort(usable, [](const DeviceCandidate& a, const DeviceCandidate& b) {
        return a.score > b.score;
    });

    const DeviceCandidate& chosen = usable.front();
    physical_ = chosen.device;
    info_ = chosen.info;
    queueFamily_ = chosen.queueFamily;

    const float queuePriority = 1.0F;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = queueFamily_;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &queuePriority;

    const char* swapchainExtension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkPhysicalDeviceFeatures2 features{};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.pNext = &features;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = 1;
    deviceInfo.ppEnabledExtensionNames = &swapchainExtension;

    VkDevice rawDevice = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDevice(physical_, &deviceInfo, nullptr, &rawDevice), "logical device creation");
    device_ = UniqueVulkan<VkDevice>{
        rawDevice,
        [](VkDevice device) { vkDestroyDevice(device, nullptr); }};

    vkGetDeviceQueue(device_.get(), queueFamily_, 0, &queue_);
    if (queue_ == VK_NULL_HANDLE) {
        throw RendererError("device queue retrieval returned a null queue", VK_ERROR_INITIALIZATION_FAILED);
    }
}

} // namespace infraforge::viewport
