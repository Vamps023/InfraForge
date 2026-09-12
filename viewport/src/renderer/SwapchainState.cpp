#include "infraforge/viewport/renderer/SwapchainState.hpp"

#include "infraforge/viewport/renderer/Vulkan.hpp"

#include <vulkan/vulkan.h>

namespace infraforge::viewport {

namespace {

// Present-phase decision for a frame whose acquire already succeeded: the
// acquire semaphore has been consumed by the queue submit, so every outcome
// here either completed the frame or failed it — never a semaphore leak.
SwapchainDecision evaluatePresentedFrame(const SwapchainInputs& inputs, SwapchainHealth acquireHealth) {
    switch (inputs.presentResult) {
    case VK_SUCCESS:
        break;
    case VK_ERROR_OUT_OF_DATE_KHR:
        // The frame did not go out; recreate before the next acquire.
        return {.action = SwapchainAction::Recreate, .health = SwapchainHealth::Healthy};
    case VK_SUBOPTIMAL_KHR:
        // The frame went out; recreate before the next acquire.
        return {.action = SwapchainAction::Recreate, .health = SwapchainHealth::Suboptimal};
    case VK_ERROR_DEVICE_LOST:
        return {.action = SwapchainAction::FailDeviceLost, .health = acquireHealth};
    default:
        return {.action = SwapchainAction::Fail, .health = acquireHealth};
    }

    if (acquireHealth == SwapchainHealth::Suboptimal) {
        return {.action = SwapchainAction::ContinueThenRecreate, .health = SwapchainHealth::Suboptimal};
    }
    return {.action = SwapchainAction::Continue, .health = SwapchainHealth::Healthy};
}

} // namespace

SwapchainDecision evaluateSwapchainFrame(const SwapchainInputs& inputs) {
    if (inputs.surfaceWidth == 0 || inputs.surfaceHeight == 0) {
        // Minimized or fully hidden surface: suspend instead of presenting
        // invalid extents; recreation happens once a real size returns. A
        // live swapchain never has a zero extent, so this cannot follow a
        // successful acquire.
        return {.action = SwapchainAction::Suspend, .health = SwapchainHealth::Healthy};
    }

    switch (inputs.acquireResult) {
    case VK_SUCCESS:
        break;
    case VK_SUBOPTIMAL_KHR:
        // A successful acquisition: the image must be rendered and presented
        // so the signaled acquire semaphore is consumed, and the swapchain is
        // recreated after the present.
        return evaluatePresentedFrame(inputs, SwapchainHealth::Suboptimal);
    case VK_ERROR_OUT_OF_DATE_KHR:
        // The acquire failed without signaling the semaphore.
        return {.action = SwapchainAction::Recreate, .health = SwapchainHealth::Healthy};
    case VK_NOT_READY:
    case VK_TIMEOUT:
        // No image was acquired and the acquire semaphore is unsignaled; the
        // next loop iteration can retry as-is.
        return {.action = SwapchainAction::SkipFrame, .health = SwapchainHealth::Healthy};
    case VK_ERROR_DEVICE_LOST:
        return {.action = SwapchainAction::FailDeviceLost, .health = SwapchainHealth::Healthy};
    default:
        // Surface loss, host/device memory exhaustion, and any unexpected
        // result fail the renderer explicitly instead of continuing to use
        // an image that was never acquired.
        return {.action = SwapchainAction::Fail, .health = SwapchainHealth::Healthy};
    }

    return evaluatePresentedFrame(inputs, SwapchainHealth::Healthy);
}

VkSurfaceFormatKHR selectSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) {
    if (formats.empty()) {
        throw RendererError(
            "surface reports no supported (format, colorspace) pairs",
            VK_ERROR_INITIALIZATION_FAILED);
    }
    for (const VkSurfaceFormatKHR& candidate : formats) {
        if (candidate.format == VK_FORMAT_B8G8R8A8_UNORM
            && candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return candidate;
        }
    }
    // Take the first supported pair whole: falling back to the first format
    // while keeping a different colorspace would combine halves the surface
    // never offered together.
    return formats.front();
}

} // namespace infraforge::viewport
