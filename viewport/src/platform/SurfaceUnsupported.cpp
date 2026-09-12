#include "infraforge/viewport/platform/SurfaceFactory.hpp"

namespace infraforge::viewport {
namespace {

// Explicit, honest placeholder for non-Windows platforms: the viewport
// process compiles so CI keeps checking the control/renderer logic, but a
// runtime attempt fails with an actionable message instead of pretending a
// native surface exists. Windows is the first runtime acceptance platform
// (docs/01_ARCHITECTURE/PROCESS_MODEL.md).
class UnsupportedSurface final : public NativeSurface {
public:
    void create(std::uint64_t, const SurfacePlacement&) override {
        throw NativeSurfaceError(
            "native child-surface embedding is not implemented on this platform; "
            "Windows is the first runtime acceptance platform for the viewport");
    }
    void place(const SurfacePlacement&) override {
        throw NativeSurfaceError("no native surface exists on this platform");
    }
    void setVisible(bool) override {}
    void requestClose() override {}
    void requestWake() override {}
    [[nodiscard]] std::uint64_t nativeHandle() const override {
        return 0;
    }
    [[nodiscard]] SurfacePlacement placement() const override {
        return {};
    }
};

} // namespace

std::unique_ptr<NativeSurface> createPlatformSurface() {
    return std::make_unique<UnsupportedSurface>();
}

std::string_view surfacePlatformName() {
    return "unsupported";
}

} // namespace infraforge::viewport
