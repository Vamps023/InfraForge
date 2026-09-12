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

// The actions distinguish what the caller holds when the decision is made:
// Recreate and Suspend mean nothing was acquired, so the acquire semaphore
// is unsignaled and can be reused as-is; ContinueThenRecreate means an image
// was acquired and must be rendered and presented so the signaled acquire
// semaphore is consumed.
enum class SwapchainAction : std::uint8_t {
    Continue,            // frame completed normally
    SkipFrame,           // nothing was acquired; retry on the next loop iteration
    Recreate,            // nothing was acquired; recreate before the next acquire
    ContinueThenRecreate,// acquired image rendered; recreate after the present
    Suspend,             // zero-sized surface; stop rendering until resized
    Fail,                // explicit renderer failure (surface lost, host/device limits)
    FailDeviceLost,      // device lost: terminal for the renderer
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
