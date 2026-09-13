#pragma once

#include "infraforge/viewport/control/ControlProtocol.hpp"
#include "infraforge/viewport/renderer/VulkanRenderer.hpp"

#include <cstdint>
#include <optional>
#include "infraforge/viewport/platform/NativeSurface.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace infraforge::viewport {

struct ApplicationArguments {
    std::uint64_t parentWindowHandle{0};
    SurfacePlacement initialPlacement;
    bool validationEnabled{false};
};

// Parses strict command-line arguments:
//   --parent-window <hex-handle> --screen-x N --screen-y N --width N --height N
//   --dpi-scale F
// Returns nullopt (after the caller prints usage) on invalid input.
[[nodiscard]] bool parseApplicationArguments(
    int argc,
    char** argv,
    ApplicationArguments& arguments,
    std::string& errorMessage);

// Decides what a visibility control command reports. A live renderer projects
// its own states, so nothing is reported; a renderer that is not running
// re-asserts its last published record so a visibility change can never turn
// a failed or device-lost renderer into an implied "ready".
[[nodiscard]] std::optional<RendererStatus> visibilityStatusReport(
    bool rendererRunning,
    const RendererStatus& lastRendererStatus);

// Runs the viewport application: creates the platform child surface, applies
// control commands, pumps platform messages, and reports lifecycle state on
// stdout. Returns the process exit code (0 = clean shutdown).
[[nodiscard]] int runViewportApplication(const ApplicationArguments& arguments);

} // namespace infraforge::viewport
