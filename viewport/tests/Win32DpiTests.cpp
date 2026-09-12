#include <doctest/doctest.h>

#include "infraforge/viewport/platform/SurfaceFactory.hpp"

#ifdef _WIN32
#include <Windows.h>

namespace {

using infraforge::viewport::enablePlatformDpiAwareness;

} // namespace

TEST_SUITE("win32 dpi awareness") {
    TEST_CASE("the viewport process opts into per-monitor v2 awareness") {
        // Regression guard for the mixed-DPI lifecycle: the viewport process
        // must be per-monitor-v2 aware before it creates its child HWND, or
        // Windows DPI-virtualizes it against the host window.
        REQUIRE(enablePlatformDpiAwareness());
        CHECK(AreDpiAwarenessContextsEqual(
            GetThreadDpiAwarenessContext(),
            DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != 0);
    }
}
#endif
