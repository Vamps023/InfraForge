#include "infraforge/viewport/platform/SurfaceFactory.hpp"

#include <cstdint>

#include "infraforge/viewport/platform/Win32Surface.hpp"
#include "infraforge/runtime/Logging.hpp"

#include <Windows.h>

#include <string>
#include <string_view>

namespace infraforge::viewport {
namespace {

// Sent from the control path to the application loop thread, which owns the
// window; DestroyWindow is therefore executed on the creating thread.
constexpr UINT kAppShutdownMessage = WM_APP + 0x1F0;
// Wakes the blocking GetMessage loop after a control command was queued.
constexpr UINT kAppWakeMessage = WM_APP + 0x1F1;
// Child window class; shared by every surface this process ever creates.
constexpr wchar_t kClassName[] = L"InfraForgeViewportChild";

LRESULT CALLBACK viewportWndProc(const HWND window, const UINT message, const WPARAM wParam, const LPARAM lParam) {
    switch (message) {
    case kAppShutdownMessage:
        DestroyWindow(window);
        return 0;
    case kAppWakeMessage:
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

} // namespace

Win32Surface::~Win32Surface() {
    if (handle_ != nullptr) {
        DestroyWindow(static_cast<HWND>(handle_));
        handle_ = nullptr;
    }
    // The last surface of the child class unregisters it and releases the
    // class background brush created in create(). UnregisterClassW fails
    // while other windows of the class are alive, leaving the brush with
    // the class until that surface performs the same teardown.
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    if (GetClassInfoExW(instance, kClassName, &windowClass)
        && UnregisterClassW(kClassName, instance) != 0
        && windowClass.hbrBackground != nullptr) {
        DeleteObject(windowClass.hbrBackground);
    }
}

void Win32Surface::create(const std::uint64_t parentWindowHandle, const SurfacePlacement& placement) {
    if (handle_ != nullptr) {
        throw NativeSurfaceError("child surface already created");
    }
    const HWND parent = reinterpret_cast<HWND>(parentWindowHandle);
    if (parent == nullptr || !IsWindow(parent)) {
        throw NativeSurfaceError("parent window handle is not a valid window");
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = viewportWndProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    // The class owns this brush for its lifetime; it is released when the
    // last surface unregisters the class. When the class already exists the
    // fresh brush is discarded immediately instead of leaking.
    HBRUSH classBrush = CreateSolidBrush(RGB(13, 16, 20));
    windowClass.hbrBackground = classBrush;
    windowClass.lpszClassName = kClassName;
    if (RegisterClassExW(&windowClass) == 0) {
        const DWORD registerError = GetLastError();
        DeleteObject(classBrush);
        if (registerError != ERROR_CLASS_ALREADY_EXISTS) {
            throw NativeSurfaceError("failed to register the viewport child window class");
        }
    }

    // Initial size is 1x1; the first place() call sets the real geometry
    // through the same code path the shell will keep using.
    const HWND window = CreateWindowExW(
        0,
        kClassName,
        L"InfraForge Viewport",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        0, 0, 1, 1,
        parent,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    if (window == nullptr) {
        throw NativeSurfaceError("failed to create the viewport child window");
    }

    handle_ = window;
    parentHandle_ = parentWindowHandle;

    // Keep the child above the web-contents sibling; the shell re-places the
    // surface on every layout change and never lets it steal activation.
    SetWindowPos(window, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    place(placement);

    runtime::logInfo("viewport", "surface.parent",
        {{"parent", std::to_string(parentWindowHandle)},
            {"child", std::to_string(static_cast<std::uint64_t>(reinterpret_cast<uintptr_t>(window)))}});
}

void Win32Surface::place(const SurfacePlacement& placement) {
    if (handle_ == nullptr) {
        throw NativeSurfaceError("cannot place a surface that has not been created");
    }
    const HWND window = static_cast<HWND>(handle_);
    const HWND parent = reinterpret_cast<HWND>(parentHandle_);

    // Chromium re-parents children of its widget windows when it rebuilds the
    // native window tree (for example on state changes); re-assert our
    // parentage and stacking on every placement so the surface self-heals.
    if (GetAncestor(window, GA_ROOT) != GetAncestor(parent, GA_ROOT) || GetParent(window) != parent) {
        (void)SetParent(window, parent);
        SetWindowPos(window, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    POINT origin{placement.screenX, placement.screenY};
    if (!ScreenToClient(parent, &origin)) {
        throw NativeSurfaceError("failed to convert placement to parent-client coordinates");
    }
    if (!SetWindowPos(window, HWND_TOP, origin.x, origin.y,
            static_cast<int>(placement.width), static_cast<int>(placement.height), SWP_NOACTIVATE)) {
        throw NativeSurfaceError("failed to resize the viewport child window");
    }
    placement_ = placement;
}

void Win32Surface::setVisible(const bool visible) {
    if (handle_ == nullptr) {
        return;
    }
    ShowWindow(static_cast<HWND>(handle_), visible ? SW_SHOWNA : SW_HIDE);
}

void Win32Surface::requestClose() {
    if (handle_ != nullptr) {
        (void)PostMessageW(static_cast<HWND>(handle_), kAppShutdownMessage, 0, 0);
    }
}

void Win32Surface::requestWake() {
    if (handle_ != nullptr) {
        (void)PostMessageW(static_cast<HWND>(handle_), kAppWakeMessage, 0, 0);
    }
}

std::uint64_t Win32Surface::nativeHandle() const {
    return reinterpret_cast<std::uint64_t>(handle_);
}

SurfacePlacement Win32Surface::placement() const {
    return placement_;
}

std::string_view surfacePlatformName() {
    return "win32";
}

bool enablePlatformDpiAwareness() {
    // The child surface must track the host monitor's scale in physical
    // pixels: a process left at the default DPI awareness is DPI-virtualized
    // by the system, which breaks mixed-DPI parent/child placement. Try
    // Per-Monitor V2 first; SetProcessDpiAware is the floor for systems
    // without the context API.
    if (SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != 0) {
        return true;
    }
    if (GetLastError() == ERROR_ACCESS_DENIED) {
        // The awareness is already fixed (embedded manifest or an earlier
        // call); report the active mode instead of overriding it.
        return AreDpiAwarenessContextsEqual(
            GetThreadDpiAwarenessContext(),
            DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != 0;
    }
    return SetProcessDPIAware() != 0;
}

std::unique_ptr<NativeSurface> createPlatformSurface() {
    return std::make_unique<Win32Surface>();
}

} // namespace infraforge::viewport
