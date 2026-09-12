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
}

void Win32Surface::create(const std::uint64_t parentWindowHandle, const SurfacePlacement& placement) {
    if (handle_ != nullptr) {
        throw NativeSurfaceError("child surface already created");
    }
    const HWND parent = reinterpret_cast<HWND>(parentWindowHandle);
    if (parent == nullptr || !IsWindow(parent)) {
        throw NativeSurfaceError("parent window handle is not a valid window");
    }

    static constexpr wchar_t kClassName[] = L"InfraForgeViewportChild";
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = viewportWndProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = CreateSolidBrush(RGB(13, 16, 20));
    windowClass.lpszClassName = kClassName;
    if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw NativeSurfaceError("failed to register the viewport child window class");
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

std::unique_ptr<NativeSurface> createPlatformSurface() {
    return std::make_unique<Win32Surface>();
}

} // namespace infraforge::viewport
