#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR 1
#endif

#include "infraforge/viewport/renderer/VulkanSurface.hpp"

#ifdef _WIN32
#include <Windows.h>
#include <vulkan/vulkan_win32.h>
#endif

#include <string>

namespace infraforge::viewport {

#ifdef _WIN32

void VulkanSurface::create(const VkInstance instance, const std::uint64_t nativeWindowHandle) {
    VkWin32SurfaceCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    createInfo.hinstance = GetModuleHandleW(nullptr);
    createInfo.hwnd = reinterpret_cast<HWND>(nativeWindowHandle);

    VkSurfaceKHR rawSurface = VK_NULL_HANDLE;
    VK_CHECK(vkCreateWin32SurfaceKHR(instance, &createInfo, nullptr, &rawSurface),
        "Win32 Vulkan surface creation");
    surface_ = UniqueVulkan<VkSurfaceKHR>{
        rawSurface,
        [instance](VkSurfaceKHR surface) { vkDestroySurfaceKHR(instance, surface, nullptr); }};
}

#else

void VulkanSurface::create(VkInstance, std::uint64_t) {
    throw RendererError(
        "Vulkan surface creation is not implemented on this platform; "
        "Windows is the first runtime acceptance platform for the viewport",
        VK_ERROR_INITIALIZATION_FAILED);
}

#endif

} // namespace infraforge::viewport
