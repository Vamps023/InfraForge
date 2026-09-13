#pragma once

#include "infraforge/viewport/platform/NativeSurface.hpp"

#include <cstdint>

namespace infraforge::viewport {

// Win32 child window surface. Created on the application loop thread; the
// message pump runs on the same thread so DestroyWindow in the destructor is
// legal.
class Win32Surface final : public NativeSurface {
public:
    Win32Surface() = default;
    ~Win32Surface() override;

    Win32Surface(const Win32Surface&) = delete;
    Win32Surface& operator=(const Win32Surface&) = delete;

    void create(std::uint64_t parentWindowHandle, const SurfacePlacement& placement) override;
    void place(const SurfacePlacement& placement) override;
    void setVisible(bool visible) override;
    void requestClose() override;
    void requestWake() override;
    [[nodiscard]] std::uint64_t nativeHandle() const override;
    [[nodiscard]] SurfacePlacement placement() const override;

private:
    void* handle_{nullptr};      // HWND
    std::uint64_t parentHandle_{0};
    SurfacePlacement placement_{};
};

} // namespace infraforge::viewport
