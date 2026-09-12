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

    TEST_CASE("suboptimal frames present once and then recreate") {
        const auto acquireSuboptimal = evaluateSwapchainFrame(SwapchainInputs{
            .acquireResult = VK_SUBOPTIMAL_KHR,
            .presentResult = VK_SUCCESS,
            .surfaceWidth = 1280,
            .surfaceHeight = 720,
        });
        CHECK(acquireSuboptimal.action == SwapchainAction::Recreate);
        CHECK(acquireSuboptimal.health == SwapchainHealth::Suboptimal);

        const auto presentSuboptimal = evaluateSwapchainFrame(SwapchainInputs{
            .acquireResult = VK_SUCCESS,
            .presentResult = VK_SUBOPTIMAL_KHR,
            .surfaceWidth = 1280,
            .surfaceHeight = 720,
        });
        CHECK(presentSuboptimal.action == SwapchainAction::Recreate);
        CHECK(presentSuboptimal.health == SwapchainHealth::Suboptimal);
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
}
