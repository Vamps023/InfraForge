#pragma once

#include <array>
#include <cstdint>

namespace infraforge::viewport {

struct CameraPoint3d {
    double x{0.0};
    double y{0.0};
    double z{0.0};

    friend bool operator==(const CameraPoint3d&, const CameraPoint3d&) = default;
};

struct CameraBounds3d {
    CameraPoint3d minimum;
    CameraPoint3d maximum;
};

enum class CameraProjection : std::uint8_t {
    Perspective,
    Top,
};

// Presentation-only Z-up editor camera. Target coordinates remain double-
// precision project-global values. writeViewProjection() subtracts the
// render origin in double precision before producing the float GPU matrix.
class EditorCamera {
public:
    void setViewport(std::uint32_t width, std::uint32_t height) noexcept;
    void setRenderOrigin(CameraPoint3d origin) noexcept;
    void setTarget(CameraPoint3d target) noexcept;
    void setDistance(double distance) noexcept;
    void setOrientation(double yawRadians, double pitchRadians) noexcept;
    void setProjection(CameraProjection projection) noexcept;

    void orbit(double yawDeltaRadians, double pitchDeltaRadians) noexcept;
    void pan(double screenDxPixels, double screenDyPixels) noexcept;
    void dolly(double wheelSteps) noexcept;
    void frame(const CameraBounds3d& bounds, double padding = 1.15) noexcept;

    [[nodiscard]] CameraPoint3d target() const noexcept { return target_; }
    [[nodiscard]] CameraPoint3d renderOrigin() const noexcept { return renderOrigin_; }
    [[nodiscard]] CameraPoint3d position() const noexcept;
    [[nodiscard]] double distance() const noexcept { return distance_; }
    [[nodiscard]] double yaw() const noexcept { return yaw_; }
    [[nodiscard]] double pitch() const noexcept { return pitch_; }
    [[nodiscard]] double verticalFov() const noexcept { return verticalFov_; }
    [[nodiscard]] double aspectRatio() const noexcept;
    [[nodiscard]] double nearPlane() const noexcept;
    [[nodiscard]] double farPlane() const noexcept;
    [[nodiscard]] double metersPerPixel() const noexcept;
    [[nodiscard]] CameraProjection projection() const noexcept { return projection_; }
    [[nodiscard]] std::uint32_t viewportWidth() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t viewportHeight() const noexcept { return height_; }
    [[nodiscard]] bool userMoved() const noexcept { return userMoved_; }
    void clearUserMoved() noexcept { userMoved_ = false; }

    // Column-major projection*view matrix using Vulkan's [0,1] depth range.
    // Framebuffer Y is flipped in the projection because Vulkan's positive
    // viewport height maps positive NDC Y downward.
    [[nodiscard]] std::array<float, 16> viewProjection() const noexcept;
    void writeViewProjection(float* out16) const noexcept;

private:
    static constexpr double kMinPitch = -1.5533430342749532; // -89 degrees
    static constexpr double kMaxPitch = -0.08726646259971647; // -5 degrees
    static constexpr double kMinDistance = 0.1;
    static constexpr double kMaxDistance = 100000000.0;

    CameraPoint3d target_{};
    CameraPoint3d renderOrigin_{};
    double distance_{200.0};
    double yaw_{0.0};
    double pitch_{-0.7853981633974483};
    double verticalFov_{0.7853981633974483};
    double topHalfHeight_{100.0};
    CameraProjection projection_{CameraProjection::Perspective};
    std::uint32_t width_{1280};
    std::uint32_t height_{720};
    bool userMoved_{false};
};

} // namespace infraforge::viewport
