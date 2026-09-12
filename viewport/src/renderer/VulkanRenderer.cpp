#include "infraforge/viewport/renderer/VulkanRenderer.hpp"

#include "infraforge/runtime/Logging.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <vector>

namespace infraforge::viewport {
namespace {

constexpr auto kSuspendPollInterval = std::chrono::milliseconds{16};
constexpr std::uint64_t kAcquireTimeout = std::numeric_limits<std::uint64_t>::max() / 2;

} // namespace

VulkanRenderer::VulkanRenderer(
    const std::uint64_t nativeWindowHandle,
    RendererStatusCallback statusCallback,
    const bool validationEnabled)
    : nativeWindowHandle_(nativeWindowHandle),
      statusCallback_(std::move(statusCallback)),
      validationEnabled_(validationEnabled) {}

VulkanRenderer::~VulkanRenderer() {
    stop();
}

void VulkanRenderer::publish(std::string state, std::string detail) const {
    if (!statusCallback_) {
        return;
    }
    RendererStatus status;
    status.state = std::move(state);
    status.detail = std::move(detail);
    status.gpuName = device_.info().deviceName;
    status.vulkanVersion = instance_.info().apiVersionText;
    status.validationEnabled = instance_.info().validationEnabled;
    statusCallback_(status);
}

void VulkanRenderer::ensureRenderFinishedSemaphores() {
    // One render-finished semaphore per swapchain image: a presented image's
    // semaphore must not be reused until that image is re-acquired, so frame
    // slots alone are not enough.
    renderFinished_.clear();
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (std::size_t image = 0; image < swapchain_.framebuffers().size(); ++image) {
        VkSemaphore rawFinished = VK_NULL_HANDLE;
        VK_CHECK(vkCreateSemaphore(device_.get(), &semaphoreInfo, nullptr, &rawFinished),
            "render finished semaphore creation");
        renderFinished_.emplace_back(rawFinished, [device = device_.get()](VkSemaphore semaphore) {
            vkDestroySemaphore(device, semaphore, nullptr);
        });
    }
}

bool VulkanRenderer::start(const std::uint32_t initialWidth, const std::uint32_t initialHeight) {
    requestedWidth_ = initialWidth;
    requestedHeight_ = initialHeight;

    publish("starting", "initializing Vulkan 1.3 renderer");
    try {
        instance_.create(validationEnabled_);
        surface_.create(instance_.get(), nativeWindowHandle_);
        device_.create(instance_.get(), surface_.get());

        // The swapchain owns the render pass for the single grid subpass; the
        // pass survives swapchain recreation (identical attachment format).
        swapchain_.create(
            device_.physical(), device_.get(), surface_.get(), device_.queueFamily(),
            VK_NULL_HANDLE, requestedWidth_, requestedHeight_);

        gridPass_.create(
            device_.physical(), device_.get(), device_.queue(), device_.queueFamily(),
            swapchain_.renderPass());

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = device_.queueFamily();
        VkCommandPool rawPool = VK_NULL_HANDLE;
        VK_CHECK(vkCreateCommandPool(device_.get(), &poolInfo, nullptr, &rawPool),
            "frame command pool creation");
        commandPool_ = UniqueVulkan<VkCommandPool>{rawPool, [device = device_.get()](VkCommandPool pool) {
            vkDestroyCommandPool(device, pool, nullptr);
        }};

        VkCommandBufferAllocateInfo commandInfo{};
        commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        commandInfo.commandPool = commandPool_.get();
        commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandInfo.commandBufferCount = kFramesInFlight;
        VK_CHECK(vkAllocateCommandBuffers(device_.get(), &commandInfo, commandBuffers_.data()),
            "frame command buffer allocation");

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (std::size_t slot = 0; slot < kFramesInFlight; ++slot) {
            VkSemaphore rawAvailable = VK_NULL_HANDLE;
            VK_CHECK(vkCreateSemaphore(device_.get(), &semaphoreInfo, nullptr, &rawAvailable),
                "image available semaphore creation");
            imageAvailable_[slot] = UniqueVulkan<VkSemaphore>{rawAvailable, [device = device_.get()](VkSemaphore s) {
                vkDestroySemaphore(device, s, nullptr);
            }};

            VkFence rawFence = VK_NULL_HANDLE;
            VK_CHECK(vkCreateFence(device_.get(), &fenceInfo, nullptr, &rawFence), "frame fence creation");
            inFlight_[slot] = UniqueVulkan<VkFence>{rawFence, [device = device_.get()](VkFence fence) {
                vkDestroyFence(device, fence, nullptr);
            }};
            // Fences start signaled so the first wait/resets for each slot
            // are valid before any submit.
            fenceSubmitted_[slot] = true;
        }
        ensureRenderFinishedSemaphores();
    } catch (const RendererError& error) {
        const std::string detail =
            std::string{error.what()} + " (" + vulkanResultName(error.result()) + ")";
        runtime::logError("viewport", "renderer.init_failed", {{"detail", detail}});
        publish("failed", detail);
        stop();
        return false;
    }

    camera_.setViewport(requestedWidth_, requestedHeight_);
    initialized_ = true;
    running_.store(true, std::memory_order_release);
    thread_ = std::thread([this] { runLoop(); });
    return true;
}

void VulkanRenderer::resize(const std::uint32_t width, const std::uint32_t height) {
    std::lock_guard lock{stateMutex_};
    if (width != requestedWidth_ || height != requestedHeight_) {
        requestedWidth_ = width;
        requestedHeight_ = height;
        sizeDirty_ = true;
    }
}

void VulkanRenderer::setVisible(const bool visible) {
    std::lock_guard lock{stateMutex_};
    visible_ = visible;
}

void VulkanRenderer::stop() {
    const bool wasRunning = running_.load(std::memory_order_acquire);
    if (wasRunning) {
        running_.store(false, std::memory_order_release);
        if (thread_.joinable()) {
            thread_.join();
        }
    }
    if (initialized_) {
        vkDeviceWaitIdle(device_.get());
        gridPass_.destroy();
        swapchain_.destroy();
        commandPool_.reset();
        renderFinished_.clear();
        for (std::size_t slot = 0; slot < kFramesInFlight; ++slot) {
            inFlight_[slot].reset();
            imageAvailable_[slot].reset();
        }
        initialized_ = false;
    }
    if (wasRunning || initialized_) {
        publish("stopped", "renderer shutdown complete");
    }
}

void VulkanRenderer::recreateSwapchain() {
    // Called on the render thread only after vkDeviceWaitIdle; stateMutex_
    // guards the requested extent.
    swapchain_.create(
        device_.physical(), device_.get(), surface_.get(), device_.queueFamily(),
        swapchain_.renderPass(), requestedWidth_, requestedHeight_);
    ensureRenderFinishedSemaphores();
    camera_.setViewport(requestedWidth_, requestedHeight_);
}

void VulkanRenderer::runLoop() {
    publish("ready", "rendering");
    bool suspended = false;
    while (running_.load(std::memory_order_acquire)) {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        bool visible = true;
        bool sizeDirty = false;
        {
            std::lock_guard lock{stateMutex_};
            width = requestedWidth_;
            height = requestedHeight_;
            visible = visible_;
            sizeDirty = sizeDirty_;
            sizeDirty_ = false;
        }

        if (!visible || width == 0 || height == 0) {
            if (!suspended) {
                suspended = true;
                publish("suspended", visible ? "surface has zero extent" : "surface hidden by shell");
            }
            std::this_thread::sleep_for(kSuspendPollInterval);
            continue;
        }

        try {
            if (suspended || sizeDirty) {
                suspended = false;
                vkDeviceWaitIdle(device_.get());
                {
                    std::lock_guard lock{stateMutex_};
                    recreateSwapchain();
                }
                publish("recreating", "swapchain recreated for new surface geometry");
                publish("ready", "rendering");
            }

            renderFrame();
        } catch (const RendererError& error) {
            runtime::logError("viewport", "renderer.frame_failed",
                {{"detail", std::string{error.what()} + " (" + vulkanResultName(error.result()) + ")"}});
            publish("failed", error.what());
            running_.store(false, std::memory_order_release);
            break;
        }
    }
}

void VulkanRenderer::renderFrame() {
    const std::size_t slot = frameSlot_;

    if (fenceSubmitted_[slot]) {
        const VkFence inFlightFence = inFlight_[slot].get();
        VK_CHECK(vkWaitForFences(device_.get(), 1, &inFlightFence, VK_TRUE, kAcquireTimeout),
            "frame fence wait");
        VK_CHECK(vkResetFences(device_.get(), 1, &inFlightFence), "frame fence reset");
        fenceSubmitted_[slot] = false;
    }

    const VkSemaphore availableSemaphore = imageAvailable_[slot].get();
    std::uint32_t imageIndex = 0;
    const VkResult acquireResult = vkAcquireNextImageKHR(
        device_.get(), swapchain_.get(), kAcquireTimeout, availableSemaphore, VK_NULL_HANDLE,
        &imageIndex);
    if (imageIndex >= renderFinished_.size()) {
        throw RendererError(
            "acquired image index is outside the render-finished semaphore set",
            VK_ERROR_UNKNOWN);
    }
    const VkSemaphore finishedSemaphore = renderFinished_[imageIndex].get();

    const SwapchainInputs acquireInputs{
        .acquireResult = acquireResult,
        .presentResult = VK_SUCCESS,
        .surfaceWidth = swapchain_.extent().width,
        .surfaceHeight = swapchain_.extent().height,
    };
    const SwapchainDecision acquireDecision = evaluateSwapchainFrame(acquireInputs);
    switch (acquireDecision.action) {
    case SwapchainAction::Suspend:
    case SwapchainAction::Recreate: {
        vkDeviceWaitIdle(device_.get());
        {
            std::lock_guard lock{stateMutex_};
            recreateSwapchain();
        }
        publish("recreating", "swapchain recreated after out-of-date acquire");
        publish("ready", "rendering");
        return;
    }
    case SwapchainAction::FailDeviceLost:
        publish("device_lost", vulkanResultName(acquireResult));
        running_.store(false, std::memory_order_release);
        return;
    case SwapchainAction::Continue:
        break;
    }

    VkCommandBuffer command = commandBuffers_[slot];
    VK_CHECK(vkResetCommandBuffer(command, 0), "command buffer reset");

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK_CHECK(vkBeginCommandBuffer(command, &beginInfo), "command buffer begin");

    gridPass_.recordFrame(command, swapchain_.framebuffers()[imageIndex], camera_);

    VK_CHECK(vkEndCommandBuffer(command), "command buffer end");

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &availableSemaphore;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &command;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &finishedSemaphore;
    VK_CHECK(vkQueueSubmit(device_.queue(), 1, &submitInfo, inFlight_[slot].get()), "frame submit");
    fenceSubmitted_[slot] = true;

    const VkSwapchainKHR swapchainHandle = swapchain_.get();
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &finishedSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchainHandle;
    presentInfo.pImageIndices = &imageIndex;

    const VkResult presentResult = vkQueuePresentKHR(device_.queue(), &presentInfo);
    const SwapchainInputs presentInputs{
        .acquireResult = VK_SUCCESS,
        .presentResult = presentResult,
        .surfaceWidth = swapchain_.extent().width,
        .surfaceHeight = swapchain_.extent().height,
    };
    const SwapchainDecision presentDecision = evaluateSwapchainFrame(presentInputs);
    switch (presentDecision.action) {
    case SwapchainAction::Recreate: {
        vkDeviceWaitIdle(device_.get());
        {
            std::lock_guard lock{stateMutex_};
            recreateSwapchain();
        }
        publish("recreating", "swapchain recreated after out-of-date present");
        publish("ready", "rendering");
        break;
    }
    case SwapchainAction::FailDeviceLost:
        publish("device_lost", vulkanResultName(presentResult));
        running_.store(false, std::memory_order_release);
        break;
    default:
        break;
    }

    frameSlot_ = (frameSlot_ + 1) % kFramesInFlight;
}

} // namespace infraforge::viewport
