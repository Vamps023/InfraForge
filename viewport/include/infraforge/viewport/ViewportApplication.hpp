#pragma once

#include "infraforge/viewport/control/ControlProtocol.hpp"
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

// Runs the viewport application: creates the platform child surface, applies
// control commands, pumps platform messages, and reports lifecycle state on
// stdout. Returns the process exit code (0 = clean shutdown).
[[nodiscard]] int runViewportApplication(const ApplicationArguments& arguments);

} // namespace infraforge::viewport
