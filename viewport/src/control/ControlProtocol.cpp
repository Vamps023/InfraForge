#include "infraforge/viewport/control/ControlProtocol.hpp"

#include <cstdint>

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
namespace infraforge::viewport {
namespace {

[[noreturn]] void failParse(std::string message) {
    throw CommandParseError{std::move(message)};
}

double requireNumber(const nlohmann::json& json, const char* key) {
    if (!json.at(key).is_number()) {
        failParse(std::string("control field '") + key + "' must be a number");
    }
    const double value = json.at(key).get<double>();
    if (!std::isfinite(value)) {
        failParse(std::string("control field '") + key + "' must be finite");
    }
    return value;
}

std::int32_t requireInt32(const nlohmann::json& json, const char* key) {
    const double value = requireNumber(json, key);
    if (value < -2147483600.0 || value > 2147483600.0) {
        failParse(std::string("control field '") + key + "' is out of range");
    }
    return static_cast<std::int32_t>(value);
}

std::uint32_t requireUint32(const nlohmann::json& json, const char* key) {
    const double value = requireNumber(json, key);
    if (value < 0.0 || value > 4294967000.0) {
        failParse(std::string("control field '") + key + "' is out of range");
    }
    return static_cast<std::uint32_t>(value);
}

} // namespace

ControlCommand parseControlCommand(const std::string_view line) {
    nlohmann::json json;
    try {
        json = nlohmann::json::parse(line);
    } catch (const nlohmann::json::exception& error) {
        failParse(std::string("malformed control command: ") + error.what());
    }
    if (!json.is_object() || !json.contains("type") || !json.at("type").is_string()) {
        failParse("control command must be an object with a string 'type'");
    }

    const std::string type = json.at("type").get<std::string>();
    static constexpr std::array<std::string_view, 5> kKnownTypes{"place", "visibility", "shutdown", "scene", "camera"};
    bool knownType = false;
    for (const std::string_view candidate : kKnownTypes) {
        knownType = knownType || type == candidate;
    }
    if (!knownType) {
        failParse("unknown control command type '" + type + "'");
    }

    if (type == "shutdown") {
        if (json.size() != 1) {
            failParse("shutdown command takes no extra fields");
        }
        return ShutdownCommand{};
    }

    if (type == "scene") {
        const bool hasTerrain = json.contains("terrain");
        const bool hasRoads = json.contains("roads");
        if (!hasTerrain && !hasRoads) {
            failParse("scene command requires at least one of 'terrain' or 'roads'");
        }
        if (json.size() != 1U + static_cast<std::size_t>(hasTerrain)
                + static_cast<std::size_t>(hasRoads)) {
            failParse("scene command contains unknown fields");
        }
        try {
            SceneCommand command;
            if (hasTerrain) {
                if (!json.at("terrain").is_object()) {
                    failParse("scene field 'terrain' must be an object");
                }
                command.terrain = parseTerrainScene(json.at("terrain").dump());
            }
            if (hasRoads) {
                if (!json.at("roads").is_object()) {
                    failParse("scene field 'roads' must be an object");
                }
                command.roads = parseRoadScene(json.at("roads").dump());
            }
            return command;
        } catch (const CommandParseError& error) {
            failParse(std::string("scene command rejected: ") + error.message);
        }
    }

    if (type == "camera") {
        if (!json.contains("action") || !json.at("action").is_string()
            || (json.size() != 2 && json.size() != 3)) {
            failParse("camera command requires string 'action' and optional 'datasetUuid'");
        }
        const std::string actionName = json.at("action").get<std::string>();
        ViewportAction action = ViewportAction::None;
        if (actionName == "focus-terrain") action = ViewportAction::FocusTerrain;
        else if (actionName == "frame-all") action = ViewportAction::FrameAllTerrain;
        else if (actionName == "perspective") action = ViewportAction::Perspective;
        else if (actionName == "top") action = ViewportAction::Top;
        else failParse("unknown camera action '" + actionName + "'");
        std::string datasetUuid;
        if (json.contains("datasetUuid")) {
            if (!json.at("datasetUuid").is_string()) failParse("camera datasetUuid must be a string");
            datasetUuid = json.at("datasetUuid").get<std::string>();
        }
        if (action == ViewportAction::FocusTerrain && datasetUuid.empty()) {
            failParse("focus-terrain requires a non-empty datasetUuid");
        }
        return CameraCommand{.action = action, .datasetUuid = std::move(datasetUuid)};
    }

    if (type == "visibility") {
        if (json.size() != 2 || !json.contains("visible") || !json.at("visible").is_boolean()) {
            failParse("visibility command requires boolean 'visible'");
        }
        return VisibilityCommand{.visible = json.at("visible").get<bool>()};
    }

    if (json.size() != 6 || !json.contains("screenX") || !json.contains("screenY")
        || !json.contains("width") || !json.contains("height") || !json.contains("dpiScale")) {
        failParse("place command requires screenX, screenY, width, height, dpiScale");
    }
    SurfacePlacement placement;
    placement.screenX = requireInt32(json, "screenX");
    placement.screenY = requireInt32(json, "screenY");
    placement.width = requireUint32(json, "width");
    placement.height = requireUint32(json, "height");
    const double dpiScale = requireNumber(json, "dpiScale");
    if (dpiScale < 0.05 || dpiScale > 10.0) {
        failParse("control field 'dpiScale' is out of range");
    }
    placement.dpiScale = dpiScale;
    return PlaceCommand{.placement = placement};
}

std::string formatReadyRecord(const SurfacePlacement& placement, const std::string_view platform) {
    const nlohmann::json record = {
        {"width", placement.width},
        {"height", placement.height},
        {"dpiScale", placement.dpiScale},
        {"platform", platform},
    };
    return std::string{kReadyPrefix} + record.dump();
}

std::string formatStatusRecord(
    const std::string_view state,
    const std::string_view detail,
    const std::string_view gpuName,
    const std::string_view vulkanVersion,
    const bool validationEnabled) {
    nlohmann::json record = {
        {"state", state},
        {"detail", detail},
        {"validation", validationEnabled},
    };
    if (!gpuName.empty()) {
        record["gpu"] = gpuName;
    }
    if (!vulkanVersion.empty()) {
        record["vulkan"] = vulkanVersion;
    }
    return std::string{kStatusPrefix} + record.dump();
}

std::string formatInteractionRecord(const double easting, const double northing,
    const double height, const std::string_view roadId) {
    nlohmann::json record = {{"kind", "primary-click"}, {"easting", easting},
        {"northing", northing}, {"height", height}};
    if (!roadId.empty()) record["roadId"] = roadId;
    return std::string{kInteractionPrefix} + record.dump();
}

} // namespace infraforge::viewport
