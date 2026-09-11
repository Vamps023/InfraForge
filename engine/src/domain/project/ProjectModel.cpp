#include "infraforge/domain/project/ProjectModel.hpp"

#include <array>
#include <cctype>
#include <cmath>

namespace infraforge::domain::project {
namespace {

constexpr std::size_t kMaxDisplayNameLength = 128;

// Windows reserves these device names regardless of extension; rejecting them
// keeps project directories portable across all supported platforms.
constexpr std::array<std::string_view, 22> kReservedDeviceNames{
    "CON", "PRN", "AUX", "NUL",
    "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
    "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
};

std::optional<ValidationError> invalidName(std::string message) {
    return ValidationError{.field = "display_name", .message = std::move(message)};
}

} // namespace

std::string_view trafficSideName(TrafficSide side) noexcept {
    switch (side) {
    case TrafficSide::Left:
        return "left";
    case TrafficSide::Right:
        return "right";
    }
    return "";
}

std::optional<TrafficSide> trafficSideFromName(std::string_view name) noexcept {
    if (name == "left") {
        return TrafficSide::Left;
    }
    if (name == "right") {
        return TrafficSide::Right;
    }
    return std::nullopt;
}

std::string_view axisConventionName(AxisConvention convention) noexcept {
    switch (convention) {
    case AxisConvention::EastingNorthingUp:
        return "easting_northing_up";
    }
    return "";
}

std::optional<AxisConvention> axisConventionFromName(std::string_view name) noexcept {
    if (name == "easting_northing_up") {
        return AxisConvention::EastingNorthingUp;
    }
    return std::nullopt;
}

std::optional<ValidationError> validateDisplayName(std::string_view name) {
    if (name.empty()) {
        return invalidName("project name must not be empty");
    }
    if (name.size() > kMaxDisplayNameLength) {
        return invalidName("project name must not exceed 128 characters");
    }
    if (name.front() == ' ' || name.back() == ' ') {
        return invalidName("project name must not start or end with whitespace");
    }
    for (const char character : name) {
        const bool allowed = std::isalnum(static_cast<unsigned char>(character)) != 0
            || character == ' ' || character == '-' || character == '_';
        if (!allowed) {
            return invalidName("project name may only contain letters, digits, spaces, '-' and '_'");
        }
    }

    const std::string stem{name.substr(0, name.find('.'))};
    for (const std::string_view reserved : kReservedDeviceNames) {
        if (stem.size() == reserved.size()) {
            bool equal = true;
            for (std::size_t index = 0; index < reserved.size(); ++index) {
                equal = equal
                    && std::toupper(static_cast<unsigned char>(stem[index]))
                        == std::toupper(static_cast<unsigned char>(reserved[index]));
            }
            if (equal) {
                return invalidName("project name uses a reserved device name");
            }
        }
    }
    return std::nullopt;
}

std::optional<ValidationError> validateGeoreference(const GeoreferenceConfig& georeference) {
    if (georeference.horizontalCrs.empty()) {
        return ValidationError{.field = "georeference.horizontal_crs", .message = "horizontal CRS must not be empty"};
    }
    if (georeference.horizontalCrs.size() > 256) {
        return ValidationError{.field = "georeference.horizontal_crs", .message = "horizontal CRS identifier is too long"};
    }
    if (georeference.linearUnit.empty()) {
        return ValidationError{.field = "georeference.linear_unit", .message = "linear unit must not be empty"};
    }
    if (georeference.linearUnit.size() > 64) {
        return ValidationError{.field = "georeference.linear_unit", .message = "linear unit identifier is too long"};
    }
    if (georeference.axisConvention != AxisConvention::EastingNorthingUp) {
        return ValidationError{.field = "georeference.axis_convention", .message = "axis convention is unspecified"};
    }
    if (!std::isfinite(georeference.originEasting) || !std::isfinite(georeference.originNorthing)) {
        return ValidationError{.field = "georeference.origin", .message = "project origin must be finite"};
    }
    if (georeference.verticalCrs.size() > 256) {
        return ValidationError{.field = "georeference.vertical_crs", .message = "vertical CRS identifier is too long"};
    }
    return std::nullopt;
}

} // namespace infraforge::domain::project
