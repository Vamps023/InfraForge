#include "infraforge/viewport/ViewportApplication.hpp"

#include "infraforge/viewport/platform/SurfaceFactory.hpp"
#include "infraforge/viewport/renderer/VulkanRenderer.hpp"
#include "infraforge/runtime/Logging.hpp"

#include <charconv>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <ostream>
#include <string>
#include <string_view>
#include <variant>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace infraforge::viewport {
namespace {

// Control commands arrive on a dedicated stdin thread; the application loop
// thread drains the queue after each platform-message wake.
class ControlQueue {
public:
    explicit ControlQueue(std::function<void()> wake)
        : wake_(std::move(wake)) {}

    void push(ControlCommand command) {
        {
            std::lock_guard lock{mutex_};
            commands_.push_back(std::move(command));
        }
        if (wake_) {
            wake_();
        }
        signal_.notify_all();
    }

    [[nodiscard]] bool empty() const {
        std::lock_guard lock{mutex_};
        return commands_.empty();
    }

    [[nodiscard]] ControlCommand pop() {
        std::lock_guard lock{mutex_};
        ControlCommand command = std::move(commands_.front());
        commands_.pop_front();
        return command;
    }

private:
    std::function<void()> wake_;
    mutable std::mutex mutex_;
    std::condition_variable signal_;
    std::deque<ControlCommand> commands_;
};

void reportStatus(
    const std::string_view state,
    const std::string_view detail,
    const std::string_view gpuName = {},
    const std::string_view vulkanVersion = {},
    const bool validation = false) {
    std::cout << formatStatusRecord(state, detail, gpuName, vulkanVersion, validation) << std::endl;
}

void readControlLines(std::istream& stream, ControlQueue& queue, const std::atomic_bool& stopped) {
    std::string line;
    while (!stopped.load(std::memory_order_relaxed) && std::getline(stream, line)) {
        if (line.empty()) {
            continue;
        }
        try {
            queue.push(parseControlCommand(line));
        } catch (const CommandParseError& error) {
            // A malformed control command is a shell bug; report and keep
            // serving rather than killing the surface.
            runtime::logWarn("viewport", "control.command_rejected", {{"detail", error.what()}});
        }
    }
    // stdin closed: the shell is gone or is shutting the viewport down.
    queue.push(ShutdownCommand{});
}

} // namespace

bool parseApplicationArguments(
    const int argc,
    char** argv,
    ApplicationArguments& arguments,
    std::string& errorMessage) {
    bool haveParent = false;
    bool haveX = false;
    bool haveY = false;
    bool haveWidth = false;
    bool haveHeight = false;
    bool haveDpi = false;

    for (int index = 1; index < argc;) {
        const std::string_view key{argv[index]};

        if (key == "--validate") {
            arguments.validationEnabled = true;
            ++index;
            continue;
        }

        if (index + 1 >= argc) {
            errorMessage = "missing value for argument";
            return false;
        }
        const std::string_view value{argv[index + 1]};
        index += 2;

        const auto parseHex = [&](std::uint64_t& target, const std::uint64_t maximum) {
            const auto result = std::from_chars(value.data(), value.data() + value.size(), target, 16);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || target > maximum) {
                errorMessage = "invalid numeric value for argument";
                return false;
            }
            return true;
        };
        const auto parseUnsignedDecimal = [&](std::uint64_t& target, const std::uint64_t maximum) {
            const auto result = std::from_chars(value.data(), value.data() + value.size(), target, 10);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || target > maximum) {
                errorMessage = "invalid numeric value for argument";
                return false;
            }
            return true;
        };
        const auto parseSigned = [&](std::int32_t& target) {
            std::int64_t parsed = 0;
            const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed, 10);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size()
                || parsed < -2147483600 || parsed > 2147483600) {
                errorMessage = "invalid numeric value for argument";
                return false;
            }
            target = static_cast<std::int32_t>(parsed);
            return true;
        };

        if (key == "--parent-window" && !haveParent) {
            std::uint64_t parsed = 0;
            if (!parseHex(parsed, 0xFFFFFFFFFFFFFFFFULL)) {
                return false;
            }
            arguments.parentWindowHandle = parsed;
            haveParent = true;
        } else if (key == "--screen-x" && !haveX) {
            if (!parseSigned(arguments.initialPlacement.screenX)) {
                return false;
            }
            haveX = true;
        } else if (key == "--screen-y" && !haveY) {
            if (!parseSigned(arguments.initialPlacement.screenY)) {
                return false;
            }
            haveY = true;
        } else if (key == "--width" && !haveWidth) {
            std::uint64_t parsed = 0;
            if (!parseUnsignedDecimal(parsed, 100000) || parsed == 0) {
                return false;
            }
            arguments.initialPlacement.width = static_cast<std::uint32_t>(parsed);
            haveWidth = true;
        } else if (key == "--height" && !haveHeight) {
            std::uint64_t parsed = 0;
            if (!parseUnsignedDecimal(parsed, 100000) || parsed == 0) {
                return false;
            }
            arguments.initialPlacement.height = static_cast<std::uint32_t>(parsed);
            haveHeight = true;
        } else if (key == "--dpi-scale" && !haveDpi) {
            std::uint64_t milliPercent = 0;
            const auto result = std::from_chars(value.data(), value.data() + value.size(), milliPercent, 10);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size()
                || milliPercent < 5 || milliPercent > 10000) {
                errorMessage = "invalid --dpi-scale value (use milli-percent, e.g. 150)";
                return false;
            }
            arguments.initialPlacement.dpiScale = static_cast<double>(milliPercent) / 100.0;
            haveDpi = true;
        } else {
            errorMessage = "unknown or duplicated argument";
            return false;
        }
    }

    if (!haveParent || !haveX || !haveY || !haveWidth || !haveHeight || !haveDpi) {
        errorMessage = "missing required arguments";
        return false;
    }
    if (arguments.parentWindowHandle == 0) {
        errorMessage = "parent window handle must be non-zero";
        return false;
    }
    return true;
}

int runViewportApplication(const ApplicationArguments& arguments) {
    reportStatus("starting", "creating native viewport surface");

    auto surface = createPlatformSurface();
    try {
        surface->create(arguments.parentWindowHandle, arguments.initialPlacement);
    } catch (const NativeSurfaceError& error) {
        reportStatus("failed", error.what());
        runtime::logError("viewport", "surface.create_failed", {{"detail", error.what()}});
        return 1;
    }

    ControlQueue queue([&surface] { surface->requestWake(); });

    std::cout << formatReadyRecord(surface->placement(), surfacePlatformName()) << std::endl;
    runtime::logInfo("viewport", "surface.created",
        {{"parent", std::to_string(arguments.parentWindowHandle)},
            {"screenX", std::to_string(surface->placement().screenX)},
            {"screenY", std::to_string(surface->placement().screenY)},
            {"width", std::to_string(surface->placement().width)},
            {"height", std::to_string(surface->placement().height)},
            {"dpiScale", std::to_string(surface->placement().dpiScale)}});

    std::atomic_bool stdinStopped{false};
    std::atomic_bool renderFailed{false};

    VulkanRenderer renderer(
        surface->nativeHandle(),
        [](const RendererStatus& status) {
            reportStatus(status.state, status.detail, status.gpuName, status.vulkanVersion, status.validationEnabled);
        },
        arguments.validationEnabled);
    if (!renderer.start(surface->placement().width, surface->placement().height)) {
        // The renderer published its failure diagnostics; keep serving the
        // control protocol so the shell can shut the process down cleanly.
        renderFailed.store(true, std::memory_order_relaxed);
    }

    std::thread stdinThread([&queue, &stdinStopped] {
        readControlLines(std::cin, queue, stdinStopped);
    });

    bool running = true;
    int exitCode = 0;

#ifdef _WIN32
    MSG message;
    while (running) {
        const BOOL pumpResult = GetMessageW(&message, nullptr, 0, 0);
        if (pumpResult <= 0) {
            // WM_QUIT or error: exit the pump; the queue drain below still
            // runs so shutdown stays deterministic.
            exitCode = pumpResult < 0 ? 1 : 0;
            break;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);

        while (running && !queue.empty()) {
            ControlCommand command = queue.pop();
            if (std::holds_alternative<ShutdownCommand>(command)) {
                running = false;
                break;
            }
            if (auto* place = std::get_if<PlaceCommand>(&command)) {
                try {
                    surface->place(place->placement);
                    renderer.resize(place->placement.width, place->placement.height);
                } catch (const NativeSurfaceError& error) {
                    reportStatus("failed", error.what());
                    runtime::logError("viewport", "surface.place_failed", {{"detail", error.what()}});
                    running = false;
                    exitCode = 1;
                    break;
                }
            } else if (auto* visibility = std::get_if<VisibilityCommand>(&command)) {
                surface->setVisible(visibility->visible);
                renderer.setVisible(visibility->visible);
                if (renderFailed.load(std::memory_order_relaxed)) {
                    reportStatus(visibility->visible ? "ready" : "suspended",
                        visibility->visible ? "surface visible" : "surface hidden by shell");
                }
            }
        }
    }
#else
    // Non-Windows platforms cannot create a surface (createPlatformSurface
    // failed earlier on this path), so this loop only exercises the control
    // path in tests and honors shutdown.
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
        while (!queue.empty()) {
            ControlCommand command = queue.pop();
            if (std::holds_alternative<ShutdownCommand>(command)) {
                running = false;
            }
        }
    }
#endif

    stdinStopped.store(true, std::memory_order_relaxed);
    renderer.stop();
    surface->requestClose();
    if (stdinThread.joinable()) {
        stdinThread.join();
    }
    reportStatus("stopped", "viewport shutdown complete");
    return exitCode;
}

} // namespace infraforge::viewport
