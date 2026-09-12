#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace infraforge::viewport {

inline constexpr std::string_view kReadyPrefix = "INFRAFORGE_VIEWPORT_READY ";
inline constexpr std::string_view kStatusPrefix = "INFRAFORGE_VIEWPORT_STATUS ";

// Physical-pixel placement of the child surface, in SCREEN coordinates. The
// viewport converts to parent-client coordinates at apply time via
// ScreenToClient so a window moved between message send and apply still
// lands correctly.
struct SurfacePlacement {
    std::int32_t screenX{0};
    std::int32_t screenY{0};
    std::uint32_t width{0};
    std::uint32_t height{0};
    double dpiScale{1.0};

    friend bool operator==(const SurfacePlacement&, const SurfacePlacement&) = default;
};

// One command from the desktop shell, one JSON object per stdin line:
//   {"type":"place","screenX":..,"screenY":..,"width":..,"height":..,"dpiScale":..}
//   {"type":"visibility","visible":true}
//   {"type":"shutdown"}
// Unknown types and malformed lines are rejected explicitly, never ignored.
struct PlaceCommand {
    SurfacePlacement placement;
};
struct VisibilityCommand {
    bool visible{true};
};
struct ShutdownCommand {
};
using ControlCommand = std::variant<PlaceCommand, VisibilityCommand, ShutdownCommand>;

struct CommandParseError : std::runtime_error {
    explicit CommandParseError(std::string message)
        : std::runtime_error(message),
          message(std::move(message)) {}

    std::string message;
};

// Parses one control line. Throws CommandParseError on malformed input.
[[nodiscard]] ControlCommand parseControlCommand(std::string_view line);

// Machine-readable stdout records consumed by the desktop supervisor.
[[nodiscard]] std::string formatReadyRecord(const SurfacePlacement& placement, std::string_view platform);
[[nodiscard]] std::string formatStatusRecord(
    std::string_view state,
    std::string_view detail,
    std::string_view gpuName = {},
    std::string_view vulkanVersion = {},
    bool validationEnabled = false);

} // namespace infraforge::viewport
