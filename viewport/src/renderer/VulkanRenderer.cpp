#include "infraforge/viewport/renderer/VulkanRenderer.hpp"

#include "infraforge/runtime/Logging.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
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

        // The swapchain owns the render pass for the terrain + grid subpass;
        // the pass survives swapchain recreation (stable attachment formats).
        swapchain_.create(
            device_.physical(), device_.get(), surface_.get(), device_.queueFamily(),
            VK_NULL_HANDLE, requestedWidth_, requestedHeight_);

        gridPass_.create(
            device_.physical(), device_.get(), device_.queue(), device_.queueFamily(),
            swapchain_.renderPass());
        terrainPass_.create(
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
    renderThread_.start([this](std::atomic_bool& running) { runLoop(running); });
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

void VulkanRenderer::setTerrainScene(const TerrainScene& scene) {
    {
        std::lock_guard lock{stateMutex_};
        pendingScene_ = scene;
        // Reset camera fit so a new project's terrain is re-framed.
        // BLOCKER 2/3: prevents stale camera origin from a previous project
        // leaking into the new scene.
        cameraFitted_ = false;
        renderOriginE_ = 0.0;
        renderOriginN_ = 0.0;
    }
    terrainPass_.setScene(scene);
}

void VulkanRenderer::postCameraInput(const SurfaceInputEvent& event) {
    std::lock_guard lock{inputMutex_};
    // Bounded queue: input arrives at human rates; dropping stale deltas
    // beats unbounded growth if the render thread stalls.
    constexpr std::size_t kMaxQueuedInput = 64;
    if (inputQueue_.size() >= kMaxQueuedInput) {
        inputQueue_.pop_front();
    }
    inputQueue_.push_back(event);
}

void VulkanRenderer::stop() {
    const bool wasRunning = renderThread_.running();
    // Always stop and join, including after the render thread cleared its own
    // flag on a frame failure — the thread is still joinable then, and a
    // joinable std::thread destroyed calls std::terminate.
    renderThread_.stop();
    if (initialized_) {
        vkDeviceWaitIdle(device_.get());
        terrainPass_.destroy();
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
    if (wasRunning) {
        publish("stopped", "renderer shutdown complete");
    }
    // A self-stopped renderer keeps its last explicit status ("failed" /
    // "device_lost"); teardown does not overwrite it with "stopped".
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

void VulkanRenderer::runLoop(std::atomic_bool& running) {
    publish("ready", "rendering");
    bool suspended = false;
    while (running.load(std::memory_order_acquire)) {
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

        // Fit the camera to the first non-empty scene so imported terrain is
        // framed on arrival; user input takes over afterwards.
        {
            std::optional<TerrainScene> fitScene;
            {
                std::lock_guard lock{stateMutex_};
                fitScene = std::move(pendingScene_);
                pendingScene_.reset();
            }
            if (!cameraFitted_ && fitScene.has_value() && !fitScene->tiles.empty()) {
                // Capture the render origin from the scene so the camera
                // operates in render-local coordinates (matching terrain
                // vertices). BLOCKER 2: prevents canonical vs render-local
                // coordinate mismatch for projects with large origins.
                // BLOCKER 12: terrain vertices use Render Y = -north, so the
                // camera center and extent must use the same negation to
                // avoid a Y-axis mirror between geometry and camera.
                renderOriginE_ = fitScene->originEasting;
                renderOriginN_ = fitScene->originNorthing;
                double minE = fitScene->tiles.front().minEasting - renderOriginE_;
                double maxE = fitScene->tiles.front().maxEasting - renderOriginE_;
                // Render Y = -(northing - originN), so min/max swap.
                double minR = -(fitScene->tiles.front().maxNorthing - renderOriginN_);
                double maxR = -(fitScene->tiles.front().minNorthing - renderOriginN_);
                for (const TerrainSceneTile& tile : fitScene->tiles) {
                    minE = std::min(minE, tile.minEasting - renderOriginE_);
                    maxE = std::max(maxE, tile.maxEasting - renderOriginE_);
                    minR = std::min(minR, -(tile.maxNorthing - renderOriginN_));
                    maxR = std::max(maxR, -(tile.minNorthing - renderOriginN_));
                }
                const double extentX = std::max(1.0, maxE - minE);
                const double extentY = std::max(1.0, maxR - minR);
                const double mpp = std::clamp(
                    std::max(extentX / static_cast<double>(width),
                        extentY / static_cast<double>(height)),
                    0.05, 100000.0);
                camera_.setMetersPerPixel(mpp);
                // Camera center in render-local coordinates (Y negated).
                camera_.setCenter((minE + maxE) * 0.5, (minR + maxR) * 0.5);
                cameraFitted_ = true;
            }
        }

        // Drain raw mouse input into camera motion; the render thread owns
        // the camera, so application happens here (grab-style panning and
        // exponential wheel zoom).
        {
            std::deque<SurfaceInputEvent> events;
            {
                std::lock_guard lock{inputMutex_};
                events.swap(inputQueue_);
            }
            for (const SurfaceInputEvent& event : events) {
                const double mpp = std::clamp(
                    camera_.metersPerPixel() * std::exp(-0.25 * event.wheelSteps), 0.05, 100000.0);
                camera_.setMetersPerPixel(mpp);
                camera_.setCenter(
                    camera_.centerWorldX() - event.dragDx * mpp,
                    camera_.centerWorldY() + event.dragDy * mpp);
            }
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

            renderFrame(running);
        } catch (const RendererError& error) {
            runtime::logError("viewport", "renderer.frame_failed",
                {{"detail", std::string{error.what()} + " (" + vulkanResultName(error.result()) + ")"}});
            publish("failed", error.what());
            running.store(false, std::memory_order_release);
            break;
        }
    }
}

void VulkanRenderer::renderFrame(std::atomic_bool& running) {
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
    bool recreateAfterPresent = false;
    switch (acquireDecision.action) {
    case SwapchainAction::ContinueThenRecreate:
        // SUBOPTIMAL is a successful acquisition: the image must be rendered
        // and presented so the signaled acquire semaphore is consumed.
        recreateAfterPresent = true;
        break;
    case SwapchainAction::Continue:
        break;
    case SwapchainAction::SkipFrame:
        // Nothing was acquired: the acquire semaphore is unsignaled and the
        // fence state is untouched, so the next loop iteration retries as-is.
        return;
    case SwapchainAction::Recreate:
        // The acquire failed without signaling anything (OUT_OF_DATE);
        // recreate before the next attempt.
        vkDeviceWaitIdle(device_.get());
        {
            std::lock_guard lock{stateMutex_};
            recreateSwapchain();
        }
        publish("recreating", "swapchain recreated after out-of-date acquire");
        publish("ready", "rendering");
        return;
    case SwapchainAction::Suspend:
        // A live swapchain never has a zero extent, so this cannot follow a
        // successful acquire; fail loudly rather than strand a signaled
        // acquire semaphore.
        throw RendererError("frame acquired for a zero-sized surface", VK_ERROR_UNKNOWN);
    case SwapchainAction::Fail:
        runtime::logError("viewport", "renderer.acquire_failed",
            {{"result", vulkanResultName(acquireResult)}});
        publish("failed", vulkanResultName(acquireResult));
        running.store(false, std::memory_order_release);
        return;
    case SwapchainAction::FailDeviceLost:
        publish("device_lost", vulkanResultName(acquireResult));
        running.store(false, std::memory_order_release);
        return;
    }

    VkCommandBuffer command = commandBuffers_[slot];
    VK_CHECK(vkResetCommandBuffer(command, 0), "command buffer reset");

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK_CHECK(vkBeginCommandBuffer(command, &beginInfo), "command buffer begin");

    // Terrain streams and draws inside the same render pass as the grid;
    // depth testing resolves terrain/terrain and terrain/grid occlusion.
    constexpr std::array<float, 4> kClearColor = {0.051F, 0.063F, 0.078F, 1.0F}; // #0d1014
    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = VkClearColorValue{{kClearColor[0], kClearColor[1], kClearColor[2], kClearColor[3]}};
    clearValues[1].depthStencil = VkClearDepthStencilValue{1.0F, 0};

    VkRenderPassBeginInfo passBegin{};
    passBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    passBegin.renderPass = swapchain_.renderPass();
    passBegin.framebuffer = swapchain_.framebuffers()[imageIndex];
    passBegin.renderArea.offset = VkOffset2D{0, 0};
    passBegin.renderArea.extent = swapchain_.extent();
    passBegin.clearValueCount = static_cast<std::uint32_t>(clearValues.size());
    passBegin.pClearValues = clearValues.data();
    vkCmdBeginRenderPass(command, &passBegin, VK_SUBPASS_CONTENTS_INLINE);

    terrainPass_.update(camera_);
    terrainPass_.record(command, camera_);
    gridPass_.record(command, camera_);

    vkCmdEndRenderPass(command);
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
    case SwapchainAction::Recreate:
        recreateAfterPresent = true;
        break;
    case SwapchainAction::FailDeviceLost:
        publish("device_lost", vulkanResultName(presentResult));
        running.store(false, std::memory_order_release);
        break;
    case SwapchainAction::Fail:
        runtime::logError("viewport", "renderer.present_failed",
            {{"result", vulkanResultName(presentResult)}});
        publish("failed", vulkanResultName(presentResult));
        running.store(false, std::memory_order_release);
        break;
    default:
        break;
    }

    frameSlot_ = (frameSlot_ + 1) % kFramesInFlight;

    if (recreateAfterPresent) {
        vkDeviceWaitIdle(device_.get());
        {
            std::lock_guard lock{stateMutex_};
            recreateSwapchain();
        }
        publish("recreating", "swapchain recreated after frame completion");
        publish("ready", "rendering");
    }
}

} // namespace infraforge::viewport
