#include "infraforge/viewport/renderer/EditorCamera.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace infraforge::viewport {
namespace {

struct Vec3 {
    double x;
    double y;
    double z;
};

[[nodiscard]] Vec3 operator-(const CameraPoint3d& a, const CameraPoint3d& b) noexcept {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

[[nodiscard]] Vec3 cross(const Vec3& a, const Vec3& b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

[[nodiscard]] double dot(const Vec3& a, const Vec3& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] Vec3 normalized(const Vec3& value) noexcept {
    const double length = std::sqrt(dot(value, value));
    return length > 0.0 ? Vec3{value.x / length, value.y / length, value.z / length}
                        : Vec3{0.0, 0.0, 0.0};
}

using Matrix = std::array<double, 16>;

[[nodiscard]] Matrix multiply(const Matrix& a, const Matrix& b) noexcept {
    Matrix result{};
    for (std::size_t column = 0; column < 4; ++column) {
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t k = 0; k < 4; ++k) {
                result[column * 4 + row] += a[k * 4 + row] * b[column * 4 + k];
            }
        }
    }
    return result;
}

} // namespace

void EditorCamera::setViewport(const std::uint32_t width, const std::uint32_t height) noexcept {
    width_ = std::max(width, 1U);
    height_ = std::max(height, 1U);
}

void EditorCamera::setRenderOrigin(const CameraPoint3d origin) noexcept { renderOrigin_ = origin; }
void EditorCamera::setTarget(const CameraPoint3d target) noexcept { target_ = target; }
void EditorCamera::setDistance(const double distance) noexcept {
    distance_ = std::clamp(distance, kMinDistance, kMaxDistance);
    topHalfHeight_ = std::max(kMinDistance, distance_ * std::tan(verticalFov_ * 0.5));
}

void EditorCamera::setOrientation(const double yawRadians, const double pitchRadians) noexcept {
    yaw_ = std::remainder(yawRadians, 2.0 * std::numbers::pi);
    pitch_ = std::clamp(pitchRadians, kMinPitch, kMaxPitch);
}

void EditorCamera::setProjection(const CameraProjection projection) noexcept { projection_ = projection; }

void EditorCamera::orbit(const double yawDeltaRadians, const double pitchDeltaRadians) noexcept {
    if (projection_ == CameraProjection::Top) {
        projection_ = CameraProjection::Perspective;
    }
    setOrientation(yaw_ + yawDeltaRadians, pitch_ + pitchDeltaRadians);
    userMoved_ = true;
}

void EditorCamera::pan(const double screenDxPixels, const double screenDyPixels) noexcept {
    const double scale = metersPerPixel();
    const Vec3 right{std::cos(yaw_), std::sin(yaw_), 0.0};
    const Vec3 screenUp{-std::sin(yaw_), std::cos(yaw_), 0.0};
    target_.x -= right.x * screenDxPixels * scale;
    target_.y -= right.y * screenDxPixels * scale;
    target_.x += screenUp.x * screenDyPixels * scale;
    target_.y += screenUp.y * screenDyPixels * scale;
    userMoved_ = true;
}

void EditorCamera::dolly(const double wheelSteps) noexcept {
    const double factor = std::exp(-0.2 * wheelSteps);
    if (projection_ == CameraProjection::Top) {
        topHalfHeight_ = std::clamp(topHalfHeight_ * factor, kMinDistance, kMaxDistance);
        distance_ = std::clamp(topHalfHeight_ / std::tan(verticalFov_ * 0.5), kMinDistance, kMaxDistance);
    } else {
        setDistance(distance_ * factor);
    }
    userMoved_ = true;
}

void EditorCamera::frame(const CameraBounds3d& bounds, const double padding) noexcept {
    target_ = {
        (bounds.minimum.x + bounds.maximum.x) * 0.5,
        (bounds.minimum.y + bounds.maximum.y) * 0.5,
        (bounds.minimum.z + bounds.maximum.z) * 0.5};
    const double extentX = std::max(1.0, bounds.maximum.x - bounds.minimum.x);
    const double extentY = std::max(1.0, bounds.maximum.y - bounds.minimum.y);
    const double extentZ = std::max(1.0, bounds.maximum.z - bounds.minimum.z);
    const double halfVertical = 0.5 * std::max(extentY, extentX / aspectRatio());
    const double radius = 0.5 * std::sqrt(extentX * extentX + extentY * extentY + extentZ * extentZ);
    topHalfHeight_ = std::max(1.0, halfVertical * padding);
    setDistance(std::max(radius * padding / std::sin(verticalFov_ * 0.5), topHalfHeight_));
}

CameraPoint3d EditorCamera::position() const noexcept {
    if (projection_ == CameraProjection::Top) {
        return {target_.x, target_.y, target_.z + distance_};
    }
    const double horizontal = std::cos(pitch_) * distance_;
    return {
        target_.x + std::sin(yaw_) * horizontal,
        target_.y - std::cos(yaw_) * horizontal,
        target_.z - std::sin(pitch_) * distance_};
}

double EditorCamera::aspectRatio() const noexcept {
    return static_cast<double>(width_) / static_cast<double>(height_);
}

double EditorCamera::nearPlane() const noexcept { return std::max(0.05, distance_ * 0.001); }
double EditorCamera::farPlane() const noexcept { return std::max(nearPlane() + 100.0, distance_ * 20.0); }

double EditorCamera::metersPerPixel() const noexcept {
    const double visibleHeight = projection_ == CameraProjection::Top
        ? topHalfHeight_ * 2.0
        : 2.0 * distance_ * std::tan(verticalFov_ * 0.5);
    return visibleHeight / static_cast<double>(height_);
}

std::array<float, 16> EditorCamera::viewProjection() const noexcept {
    const CameraPoint3d eyeGlobal = position();
    const CameraPoint3d eye{
        eyeGlobal.x - renderOrigin_.x,
        -(eyeGlobal.y - renderOrigin_.y),
        eyeGlobal.z - renderOrigin_.z};
    const CameraPoint3d targetLocal{
        target_.x - renderOrigin_.x,
        -(target_.y - renderOrigin_.y),
        target_.z - renderOrigin_.z};
    const Vec3 forward = normalized(targetLocal - eye);
    const Vec3 worldUp{0.0, 0.0, 1.0};
    const Vec3 topUp{0.0, 1.0, 0.0};
    const Vec3 right = normalized(cross(forward, projection_ == CameraProjection::Top ? topUp : worldUp));
    const Vec3 up = cross(right, forward);

    Matrix view{};
    view[0] = right.x; view[4] = right.y; view[8] = right.z; view[12] = -dot(right, {eye.x, eye.y, eye.z});
    view[1] = up.x; view[5] = up.y; view[9] = up.z; view[13] = -dot(up, {eye.x, eye.y, eye.z});
    view[2] = -forward.x; view[6] = -forward.y; view[10] = -forward.z; view[14] = dot(forward, {eye.x, eye.y, eye.z});
    view[15] = 1.0;

    Matrix projection{};
    const double near = nearPlane();
    const double far = farPlane();
    if (projection_ == CameraProjection::Perspective) {
        const double focal = 1.0 / std::tan(verticalFov_ * 0.5);
        projection[0] = focal / aspectRatio();
        projection[5] = -focal;
        projection[10] = far / (near - far);
        projection[11] = -1.0;
        projection[14] = (far * near) / (near - far);
    } else {
        const double halfWidth = topHalfHeight_ * aspectRatio();
        projection[0] = 1.0 / halfWidth;
        projection[5] = -1.0 / topHalfHeight_;
        projection[10] = 1.0 / (near - far);
        projection[14] = near / (near - far);
        projection[15] = 1.0;
    }

    const Matrix result = multiply(projection, view);
    std::array<float, 16> output{};
    std::transform(result.begin(), result.end(), output.begin(), [](double value) {
        return static_cast<float>(value);
    });
    return output;
}

void EditorCamera::writeViewProjection(float* out16) const noexcept {
    const auto matrix = viewProjection();
    std::copy(matrix.begin(), matrix.end(), out16);
}

} // namespace infraforge::viewport
