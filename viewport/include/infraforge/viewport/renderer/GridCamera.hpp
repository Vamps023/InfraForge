#pragma once

#include <cstdint>

namespace infraforge::viewport {

// Presentation-only orthographic top-down camera for the viewport grid. It
// owns no canonical state; it is derived viewport presentation and can be
// discarded at any time.
class GridCamera {
public:
    void setViewport(std::uint32_t width, std::uint32_t height) noexcept;
    void setCenter(double worldX, double worldY) noexcept;
    void setMetersPerPixel(double metersPerPixel) noexcept;

    [[nodiscard]] double centerWorldX() const noexcept { return centerX_; }
    [[nodiscard]] double centerWorldY() const noexcept { return centerY_; }
    [[nodiscard]] double metersPerPixel() const noexcept { return metersPerPixel_; }
    [[nodiscard]] std::uint32_t viewportWidth() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t viewportHeight() const noexcept { return height_; }

    // Column-major 4x4 projection*view matrix (std140-compatible layout for
    // push constants); Y grows downward on screen to match window space.
    void writeViewProjection(float* out16) const noexcept;

private:
    double centerX_{0.0};
    double centerY_{0.0};
    double metersPerPixel_{1.0};
    std::uint32_t width_{1280};
    std::uint32_t height_{720};
};

} // namespace infraforge::viewport
