#include <doctest/doctest.h>

#include "infraforge/viewport/renderer/SwapchainState.hpp"

#include <array>

namespace {

using infraforge::viewport::SwapchainAction;
using infraforge::viewport::SwapchainHealth;
using infraforge::viewport::SwapchainInputs;
using infraforge::viewport::evaluateSwapchainFrame;

constexpr SwapchainInputs healthyFrame{
    .acquireResult = VK_SUCCESS,
    .presentResult = VK_SUCCESS,
    .surfaceWidth = 1280,
    .surfaceHeight = 720,
};

} // namespace

TEST_SUITE("swapchain lifecycle state machine") {
    TEST_CASE("a healthy frame continues without recreation") {
        const auto decision = evaluateSwapchainFrame(healthyFrame);
        CHECK(decision.action == SwapchainAction::Continue);
        CHECK(decision.health == SwapchainHealth::Healthy);
    }

    TEST_CASE("zero-sized surfaces suspend instead of presenting") {
        const auto zeroWidth = evaluateSwapchainFrame(SwapchainInputs{
            .acquireResult = VK_SUCCESS,
            .presentResult = VK_SUCCESS,
            .surfaceWidth = 0,
            .surfaceHeight = 720,
        });
        CHECK(zeroWidth.action == SwapchainAction::Suspend);

        const auto zeroHeight = evaluateSwapchainFrame(SwapchainInputs{
            .acquireResult = VK_SUCCESS,
            .presentResult = VK_SUCCESS,
            .surfaceWidth = 1280,
            .surfaceHeight = 0,
        });
        CHECK(zeroHeight.action == SwapchainAction::Suspend);
        const auto bothZero = evaluateSwapchainFrame(SwapchainInputs{
            .acquireResult = VK_SUCCESS,
            .presentResult = VK_SUCCESS,
            .surfaceWidth = 0,
            .surfaceHeight = 0,
        });
        CHECK(bothZero.action == SwapchainAction::Suspend);
    }

    TEST_CASE("out-of-date results demand recreation regardless of stage") {
        const auto acquire = evaluateSwapchainFrame(SwapchainInputs{
            .acquireResult = VK_ERROR_OUT_OF_DATE_KHR,
            .presentResult = VK_SUCCESS,
            .surfaceWidth = 1280,
            .surfaceHeight = 720,
        });
        CHECK(acquire.action == SwapchainAction::Recreate);

        const auto present = evaluateSwapchainFrame(SwapchainInputs{
            .acquireResult = VK_SUCCESS,
            .presentResult = VK_ERROR_OUT_OF_DATE_KHR,
            .surfaceWidth = 1280,
            .surfaceHeight = 720,
        });
        CHECK(present.action == SwapchainAction::Recreate);
    }

    TEST_CASE("suboptimal acquire still renders the acquired image before recreating") {
        // Regression: SUBOPTIMAL_KHR is a successful acquire — the image is
        // acquired and the acquire semaphore is signaled, so the frame must
        // be rendered and presented (consuming the semaphore) before the
        // swapchain is recreated. Recreating immediately would reuse a
        // signaled binary semaphore, which Vulkan forbids.
        const auto acquireSuboptimal = evaluateSwapchainFrame(SwapchainInputs{
            .acquireResult = VK_SUBOPTIMAL_KHR,
            .presentResult = VK_SUCCESS,
            .surfaceWidth = 1280,
            .surfaceHeight = 720,
        });
        CHECK(acquireSuboptimal.action == SwapchainAction::ContinueThenRecreate);
        CHECK(acquireSuboptimal.health == SwapchainHealth::Suboptimal);
    }

    TEST_CASE("suboptimal present recreates after the completed frame") {
        const auto presentSuboptimal = evaluateSwapchainFrame(SwapchainInputs{
            .acquireResult = VK_SUCCESS,
            .presentResult = VK_SUBOPTIMAL_KHR,
            .surfaceWidth = 1280,
            .surfaceHeight = 720,
        });
        CHECK(presentSuboptimal.action == SwapchainAction::Recreate);
        CHECK(presentSuboptimal.health == SwapchainHealth::Suboptimal);
    }

    TEST_CASE("a suboptimal acquire combined with a failed present still recreates") {
        const auto presentOutOfDate = evaluateSwapchainFrame(SwapchainInputs{
            .acquireResult = VK_SUBOPTIMAL_KHR,
            .presentResult = VK_ERROR_OUT_OF_DATE_KHR,
            .surfaceWidth = 1280,
            .surfaceHeight = 720,
        });
        CHECK(presentOutOfDate.action == SwapchainAction::Recreate);

        const auto presentSuboptimal = evaluateSwapchainFrame(SwapchainInputs{
            .acquireResult = VK_SUBOPTIMAL_KHR,
            .presentResult = VK_SUBOPTIMAL_KHR,
            .surfaceWidth = 1280,
            .surfaceHeight = 720,
        });
        CHECK(presentSuboptimal.action == SwapchainAction::Recreate);
        CHECK(presentSuboptimal.health == SwapchainHealth::Suboptimal);
    }

    TEST_CASE("timeout and not-ready acquires skip the frame without touching swapchain state") {
        // Regression: these results mean no image was acquired and the
        // acquire semaphore was never signaled; the frame must be skipped
        // instead of reading a stale image index or waiting on a semaphore
        // that will never signal.
        for (const VkResult result : {VK_TIMEOUT, VK_NOT_READY}) {
            const auto decision = evaluateSwapchainFrame(SwapchainInputs{
                .acquireResult = result,
                .presentResult = VK_SUCCESS,
                .surfaceWidth = 1280,
                .surfaceHeight = 720,
            });
            CHECK(decision.action == SwapchainAction::SkipFrame);
            CHECK(decision.health == SwapchainHealth::Healthy);
        }
    }

    TEST_CASE("surface loss and unexpected acquire errors fail the renderer explicitly") {
        for (const VkResult result :
            {VK_ERROR_SURFACE_LOST_KHR, VK_ERROR_OUT_OF_HOST_MEMORY, VK_ERROR_OUT_OF_DEVICE_MEMORY}) {
            const auto decision = evaluateSwapchainFrame(SwapchainInputs{
                .acquireResult = result,
                .presentResult = VK_SUCCESS,
                .surfaceWidth = 1280,
                .surfaceHeight = 720,
            });
            CHECK(decision.action == SwapchainAction::Fail);
        }
    }

    TEST_CASE("device loss on acquire is an explicit device-lost failure") {
        const auto decision = evaluateSwapchainFrame(SwapchainInputs{
            .acquireResult = VK_ERROR_DEVICE_LOST,
            .presentResult = VK_SUCCESS,
            .surfaceWidth = 1280,
            .surfaceHeight = 720,
        });
        CHECK(decision.action == SwapchainAction::FailDeviceLost);
    }

    TEST_CASE("device loss during present is an explicit failure") {
        const auto decision = evaluateSwapchainFrame(SwapchainInputs{
            .acquireResult = VK_SUCCESS,
            .presentResult = VK_ERROR_DEVICE_LOST,
            .surfaceWidth = 1280,
            .surfaceHeight = 720,
        });
        CHECK(decision.action == SwapchainAction::FailDeviceLost);
    }

    TEST_CASE("surface loss during present fails explicitly instead of reporting device loss") {
        const auto decision = evaluateSwapchainFrame(SwapchainInputs{
            .acquireResult = VK_SUCCESS,
            .presentResult = VK_ERROR_SURFACE_LOST_KHR,
            .surfaceWidth = 1280,
            .surfaceHeight = 720,
        });
        CHECK(decision.action == SwapchainAction::Fail);
    }
}
