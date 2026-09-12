#pragma once

#include "infraforge/viewport/renderer/GridCamera.hpp"
#include "infraforge/viewport/renderer/GridPass.hpp"
#include "infraforge/viewport/renderer/SwapchainState.hpp"
#include "infraforge/viewport/renderer/VulkanDevice.hpp"
#include "infraforge/viewport/renderer/VulkanInstance.hpp"
#include "infraforge/viewport/renderer/VulkanSurface.hpp"
#include "infraforge/viewport/renderer/VulkanSwapchain.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace infraforge::viewport {

// Renderer status projected to the shell: the UI-facing lifecycle states.
struct RendererStatus {
    std::string state;    // starting|ready|suspended|recreating|device_lost|failed|stopped
    std::string detail;
    std::string gpuName;
    std::string vulkanVersion;
    bool validationEnabled{false};
};

using RendererStatusCallback = std::function<void(const RendererStatus&)>;

// Owns the Vulkan 1.3 renderer for the native child surface: RAII core
// objects, a dedicated render thread running the swapchain lifecycle state
// machine, the grid pass, and status projection. Presentation only — no
// canonical project state lives here.
class VulkanRenderer final {
public:
    VulkanRenderer(std::uint64_t nativeWindowHandle, RendererStatusCallback statusCallback, bool validationEnabled);
    ~VulkanRenderer();

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    // Initializes Vulkan and starts the render thread. On failure the
    // renderer reports state "failed" and start() returns false; the process
    // stays alive so the failure is visible.
    bool start(std::uint32_t initialWidth, std::uint32_t initialHeight);

    // Thread-safe placement updates from the control path.
    void resize(std::uint32_t width, std::uint32_t height);
    void setVisible(bool visible);

    // Stops the render thread and tears down all Vulkan objects in reverse
    // dependency order (wait idle → pass → swapchain → device → surface →
    // instance).
    void stop();

    [[nodiscard]] bool running() const noexcept { return running_.load(std::memory_order_acquire); }

private:
    void runLoop();
    void publish(std::string state, std::string detail) const;
    void ensureRenderFinishedSemaphores();
    // Render thread only; callers hold stateMutex_ when touching the extent.
    void recreateSwapchain();
    void renderFrame();

    std::uint64_t nativeWindowHandle_;
    RendererStatusCallback statusCallback_;
    bool validationEnabled_;

    VulkanInstance instance_;
    VulkanSurface surface_;
    VulkanDevice device_;
    VulkanSwapchain swapchain_;
    GridPass gridPass_;
    GridCamera camera_;

    // Frame-in-flight synchronization (double buffered). Acquire semaphores
    // and fences are per frame slot; render-finished semaphores are per
    // swapchain image because a presented image's semaphore stays in use
    // until that image is re-acquired. The pool owns the command buffers.
    static constexpr std::size_t kFramesInFlight = 2;
    std::array<UniqueVulkan<VkSemaphore>, kFramesInFlight> imageAvailable_{};
    std::vector<UniqueVulkan<VkSemaphore>> renderFinished_;
    std::array<UniqueVulkan<VkFence>, kFramesInFlight> inFlight_{};
    std::array<bool, kFramesInFlight> fenceSubmitted_{};
    UniqueVulkan<VkCommandPool> commandPool_;
    std::array<VkCommandBuffer, kFramesInFlight> commandBuffers_{};
    std::size_t frameSlot_{0};

    mutable std::mutex stateMutex_;
    std::uint32_t requestedWidth_{0};
    std::uint32_t requestedHeight_{0};
    bool sizeDirty_{false};
    bool visible_{true};

    std::thread thread_;
    std::atomic_bool running_{false};
    bool initialized_{false};
};

} // namespace infraforge::viewport
