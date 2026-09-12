#include "infraforge/viewport/renderer/SwapchainState.hpp"

#include <vulkan/vulkan.h>

namespace infraforge::viewport {

SwapchainDecision evaluateSwapchainFrame(const SwapchainInputs& inputs) {
    if (inputs.surfaceWidth == 0 || inputs.surfaceHeight == 0) {
        // Minimized or fully hidden surface: suspend instead of presenting
        // invalid extents; recreation happens once a real size returns.
        return {.action = SwapchainAction::Suspend, .health = SwapchainHealth::Healthy};
    }

    if (inputs.acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        return {.action = SwapchainAction::Recreate, .health = SwapchainHealth::Healthy};
    }

    SwapchainHealth health = SwapchainHealth::Healthy;
    if (inputs.acquireResult == VK_SUBOPTIMAL_KHR) {
        // Present this frame, then recreate before the next one.
        health = SwapchainHealth::Suboptimal;
    }

    switch (inputs.presentResult) {
    case VK_ERROR_OUT_OF_DATE_KHR:
        return {.action = SwapchainAction::Recreate, .health = SwapchainHealth::Healthy};
    case VK_SUBOPTIMAL_KHR:
        return {.action = SwapchainAction::Recreate, .health = SwapchainHealth::Suboptimal};
    default:
        break;
    }

    if (inputs.presentResult != VK_SUCCESS) {
        return {.action = SwapchainAction::FailDeviceLost, .health = health};
    }

    if (health == SwapchainHealth::Suboptimal) {
        return {.action = SwapchainAction::Recreate, .health = SwapchainHealth::Suboptimal};
    }
    return {.action = SwapchainAction::Continue, .health = SwapchainHealth::Healthy};
}

} // namespace infraforge::viewport
