#include "infraforge/viewport/renderer/VulkanInstance.hpp"

#include "infraforge/runtime/Logging.hpp"

#ifdef _WIN32
#include <Windows.h>
#include <vulkan/vulkan_win32.h>
#endif

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace infraforge::viewport {
namespace {

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    const VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*) {
    // Validation output goes to the structured log; validation findings are
    // treated as failures by the lifecycle verification process.
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        runtime::logError("viewport", "vulkan.validation_error", {{"detail", data->pMessage}});
    } else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        runtime::logWarn("viewport", "vulkan.validation_warning", {{"detail", data->pMessage}});
    } else {
        runtime::logInfo("viewport", "vulkan.validation_info", {{"detail", data->pMessage}});
    }
    return VK_FALSE;
}

std::string formatApiVersion(const std::uint32_t version) {
    return std::to_string(VK_API_VERSION_MAJOR(version)) + "."
        + std::to_string(VK_API_VERSION_MINOR(version)) + "."
        + std::to_string(VK_API_VERSION_PATCH(version));
}

PFN_vkCreateDebugUtilsMessengerEXT loadCreateMessenger(VkInstance instance) {
    return reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
}

PFN_vkDestroyDebugUtilsMessengerEXT loadDestroyMessenger(VkInstance instance) {
    return reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
}

} // namespace

std::string vulkanResultName(const VkResult result) {
    switch (result) {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_NOT_READY: return "VK_NOT_READY";
    case VK_TIMEOUT: return "VK_TIMEOUT";
    case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    default: return "VkResult(" + std::to_string(static_cast<int>(result)) + ")";
    }
}

void VulkanInstance::create(const bool validationEnabled) {
    // The baseline is Vulkan 1.3 (docs/04_RENDERER/VULKAN_ARCHITECTURE.md).
    std::uint32_t loaderVersion = 0;
    VK_CHECK(vkEnumerateInstanceVersion(&loaderVersion), "Vulkan loader version probe");
    if (loaderVersion < VK_API_VERSION_1_3) {
        throw RendererError(
            "Vulkan loader reports API version " + formatApiVersion(loaderVersion)
                + " but the renderer requires a Vulkan 1.3 baseline",
            VK_ERROR_INITIALIZATION_FAILED);
    }

    std::vector<const char*> surfaceExtensions{
        VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef _WIN32
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#else
        VK_KHR_XCB_SURFACE_EXTENSION_NAME,
#endif
    };
    if (validationEnabled) {
        // The debug messenger functions exist only when this extension is
        // enabled on the instance.
        surfaceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkApplicationInfo application{};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.pApplicationName = "InfraForge";
    application.applicationVersion = VK_MAKE_VERSION(0, 3, 0);
    application.pEngineName = "InfraForge";
    application.engineVersion = VK_MAKE_VERSION(0, 3, 0);
    application.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &application;
    createInfo.enabledExtensionCount = static_cast<std::uint32_t>(surfaceExtensions.size());
    createInfo.ppEnabledExtensionNames = surfaceExtensions.data();

    const char* validationLayerName = "VK_LAYER_KHRONOS_validation";
    if (validationEnabled) {
        std::uint32_t layerCount = 0;
        VK_CHECK(vkEnumerateInstanceLayerProperties(&layerCount, nullptr), "validation layer probe");
        std::vector<VkLayerProperties> layers(layerCount);
        VK_CHECK(vkEnumerateInstanceLayerProperties(&layerCount, layers.data()), "validation layer enumeration");
        bool present = false;
        for (const auto& layer : layers) {
            if (std::strcmp(layer.layerName, validationLayerName) == 0) {
                present = true;
                break;
            }
        }
        if (!present) {
            throw RendererError(
                "validation was requested but the VK_LAYER_KHRONOS_validation layer is not installed",
                VK_ERROR_INITIALIZATION_FAILED);
        }
        createInfo.enabledLayerCount = 1;
        createInfo.ppEnabledLayerNames = &validationLayerName;
    }

    VkInstance rawInstance = VK_NULL_HANDLE;
    VK_CHECK(vkCreateInstance(&createInfo, nullptr, &rawInstance), "Vulkan instance creation");
    instance_ = UniqueVulkan<VkInstance>{
        rawInstance,
        [](VkInstance instance) { vkDestroyInstance(instance, nullptr); }};

    if (validationEnabled) {
        VkDebugUtilsMessengerCreateInfoEXT messengerInfo{};
        messengerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        messengerInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        messengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        messengerInfo.pfnUserCallback = debugCallback;

        const auto createMessenger = loadCreateMessenger(instance_.get());
        if (createMessenger == nullptr) {
            throw RendererError(
                "validation was requested but vkCreateDebugUtilsMessengerEXT is unavailable",
                VK_ERROR_INITIALIZATION_FAILED);
        }
        VkDebugUtilsMessengerEXT rawMessenger = VK_NULL_HANDLE;
        VK_CHECK(createMessenger(instance_.get(), &messengerInfo, nullptr, &rawMessenger),
            "debug messenger creation");
        const VkInstance capturedInstance = instance_.get();
        messenger_ = UniqueVulkan<VkDebugUtilsMessengerEXT>{
            rawMessenger,
            [capturedInstance](VkDebugUtilsMessengerEXT messenger) {
                if (const auto destroyMessenger = loadDestroyMessenger(capturedInstance)) {
                    destroyMessenger(capturedInstance, messenger, nullptr);
                }
            }};
    }

    info_.apiVersionText = formatApiVersion(loaderVersion);
    info_.validationEnabled = validationEnabled;
}

} // namespace infraforge::viewport
