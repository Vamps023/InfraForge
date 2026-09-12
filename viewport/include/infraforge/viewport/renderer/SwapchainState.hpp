#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

namespace infraforge::viewport {

// Pure decision logic for the swapchain lifecycle (docs/04_RENDERER/
// VULKAN_ARCHITECTURE.md): out-of-date, suboptimal, and zero-sized extents
// are normal states, not fatal errors. Unit-tested without a GPU.
enum class SwapchainHealth : std::uint8_t {
    Healthy,
    Suboptimal,
};

enum class SwapchainAction : std::uint8_t {
    Continue,     // frame completed or was legitimately skipped
    Recreate,     // swapchain must be recreated before the next frame
    Suspend,      // zero-sized surface; stop rendering until resized
    FailDeviceLost,
};

struct SwapchainInputs {
    VkResult acquireResult;
    VkResult presentResult;
    std::uint32_t surfaceWidth;
    std::uint32_t surfaceHeight;
};

struct SwapchainDecision {
    SwapchainAction action{SwapchainAction::Continue};
    SwapchainHealth health{SwapchainHealth::Healthy};
};

[[nodiscard]] SwapchainDecision evaluateSwapchainFrame(const SwapchainInputs& inputs);

} // namespace infraforge::viewport
