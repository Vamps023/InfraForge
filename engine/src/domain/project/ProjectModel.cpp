#include "infraforge/domain/project/ProjectModel.hpp"

#include <array>
#include <cctype>

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

} // namespace infraforge::domain::project
