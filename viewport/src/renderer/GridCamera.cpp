#include "infraforge/viewport/renderer/GridCamera.hpp"

#include <cstdint>

#include <algorithm>
#include <cmath>

namespace infraforge::viewport {

void GridCamera::setViewport(const std::uint32_t width, const std::uint32_t height) noexcept {
    width_ = std::max<std::uint32_t>(width, 1);
    height_ = std::max<std::uint32_t>(height, 1);
}

void GridCamera::setCenter(const double worldX, const double worldY) noexcept {
    centerX_ = worldX;
    centerY_ = worldY;
}

void GridCamera::setMetersPerPixel(const double metersPerPixel) noexcept {
    metersPerPixel_ = std::clamp(metersPerPixel, 0.01, 1000.0);
}

void GridCamera::writeViewProjection(float* out16) const noexcept {
    const double halfWidth = static_cast<double>(width_) * 0.5 * metersPerPixel_;
    const double halfHeight = static_cast<double>(height_) * 0.5 * metersPerPixel_;

    // Ortho projection with Y down (window space): world +Y renders upward on
    // screen only if we flip; keep math right-handed with Y down so the grid
    // matches "north up" reading direction.
    const double left = centerX_ - halfWidth;
    const double right = centerX_ + halfWidth;
    const double top = centerY_ - halfHeight;
    const double bottom = centerY_ + halfHeight;

    const double sx = 2.0 / (right - left);
    const double sy = 2.0 / (bottom - top);
    const double tx = -(right + left) / (right - left);
    const double ty = -(bottom + top) / (bottom - top);

    // Column-major: out16[0..3] = column 0.
    out16[0] = static_cast<float>(sx);
    out16[1] = 0.0F;
    out16[2] = 0.0F;
    out16[3] = 0.0F;

    out16[4] = 0.0F;
    out16[5] = static_cast<float>(sy);
    out16[6] = 0.0F;
    out16[7] = 0.0F;

    out16[8] = 0.0F;
    out16[9] = 0.0F;
    out16[10] = 1.0F;
    out16[11] = 0.0F;

    out16[12] = static_cast<float>(tx);
    out16[13] = static_cast<float>(ty);
    out16[14] = 0.0F;
    out16[15] = 1.0F;
}

} // namespace infraforge::viewport
