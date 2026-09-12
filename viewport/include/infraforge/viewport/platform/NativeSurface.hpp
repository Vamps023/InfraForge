#pragma once

#include "infraforge/viewport/control/ControlProtocol.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace infraforge::viewport {

// A real native child surface parented into the desktop shell's window.
// Implementations own the platform window; destruction must happen on the
// thread that created it (the viewport application loop thread).
class NativeSurface {
public:
    virtual ~NativeSurface() = default;

    // Creates the child surface inside the given parent window, initially
    // visible or hidden per the shell's startup visibility decision — a
    // surface created while a blocking overlay is open or the host window is
    // minimized must never become visible before the first runtime
    // visibility control command. Throws NativeSurfaceError on failure.
    virtual void create(
        std::uint64_t parentWindowHandle,
        const SurfacePlacement& placement,
        bool initialVisible) = 0;

    // Repositions/resizes the child surface to the given screen-coordinate
    // placement. Cheap; called on every shell layout change.
    virtual void place(const SurfacePlacement& placement) = 0;

    virtual void setVisible(bool visible) = 0;

    // Asks the application loop thread to close the surface; safe to call
    // from any thread.
    virtual void requestClose() = 0;

    // Wakes the platform message loop so queued control commands are drained
    // promptly; safe to call from any thread. No-op without a live surface.
    virtual void requestWake() = 0;

    // Platform handle used for Vulkan surface creation (HWND on Windows).
    // Returns 0 when the surface does not exist.
    [[nodiscard]] virtual std::uint64_t nativeHandle() const = 0;

    [[nodiscard]] virtual SurfacePlacement placement() const = 0;
};

class NativeSurfaceError : public std::runtime_error {
public:
    explicit NativeSurfaceError(std::string message)
        : std::runtime_error(std::move(message)) {}
};

// Platform name used in readiness records and diagnostics.
[[nodiscard]] std::string_view surfacePlatformName();

} // namespace infraforge::viewport
