#include "infraforge/viewport/renderer/TerrainScene.hpp"

#include "infraforge/viewport/control/ControlProtocol.hpp"

#include <nlohmann/json.hpp>

namespace infraforge::viewport {
namespace {

[[nodiscard]] double requiredNumber(const nlohmann::json& object, const char* field) {
    const auto found = object.find(field);
    if (found == object.end() || !found->is_number()) {
        throw CommandParseError(std::string{"scene field \""} + field + "\" must be a number");
    }
    return found->get<double>();
}

[[nodiscard]] std::int64_t requiredInteger(const nlohmann::json& object, const char* field) {
    const auto found = object.find(field);
    if (found == object.end()) {
        throw CommandParseError(std::string{"scene field \""} + field + "\" must be an integer");
    }
    if (found->is_number_integer()) {
        return found->get<std::int64_t>();
    }
    if (found->is_string()) {
        // BigInt values are serialized as decimal strings for lossless
        // transport beyond Number.MAX_SAFE_INTEGER (BLOCKER 8).
        try {
            return std::stoll(found->get<std::string>());
        } catch (...) {
            throw CommandParseError(std::string{"scene field \""} + field + "\" is not a valid integer string");
        }
    }
    throw CommandParseError(std::string{"scene field \""} + field + "\" must be an integer");
}

[[nodiscard]] std::uint64_t requiredUnsigned(const nlohmann::json& object, const char* field) {
    const auto found = object.find(field);
    if (found == object.end()) {
        throw CommandParseError(std::string{"scene field \""} + field + "\" must be a non-negative integer");
    }
    if (found->is_number_unsigned()) {
        return found->get<std::uint64_t>();
    }
    if (found->is_string()) {
        // BigInt values are serialized as decimal strings for lossless
        // transport beyond Number.MAX_SAFE_INTEGER (BLOCKER 8).
        try {
            return std::stoull(found->get<std::string>());
        } catch (...) {
            throw CommandParseError(std::string{"scene field \""} + field + "\" is not a valid unsigned integer string");
        }
    }
    throw CommandParseError(std::string{"scene field \""} + field + "\" must be a non-negative integer");
}

[[nodiscard]] std::string requiredString(const nlohmann::json& object, const char* field) {
    const auto found = object.find(field);
    if (found == object.end() || !found->is_string()) {
        throw CommandParseError(std::string{"scene field \""} + field + "\" must be a string");
    }
    return found->get<std::string>();
}

} // namespace

TerrainScene parseTerrainScene(const std::string_view json) {
    const auto parsed = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        throw CommandParseError("scene payload must be a JSON object");
    }

    TerrainScene scene;
    scene.originEasting = requiredNumber(parsed, "originEasting");
    scene.originNorthing = requiredNumber(parsed, "originNorthing");
    scene.originHeight = requiredNumber(parsed, "originHeight");
    scene.missingTiles = requiredUnsigned(parsed, "missingTiles");
    scene.revision = requiredUnsigned(parsed, "revision");

    const auto tiles = parsed.find("tiles");
    if (tiles == parsed.end() || !tiles->is_array()) {
        throw CommandParseError("scene field \"tiles\" must be an array");
    }
    scene.tiles.reserve(tiles->size());
    for (const auto& entry : *tiles) {
        if (!entry.is_object()) {
            throw CommandParseError("scene tile entries must be objects");
        }
        TerrainSceneTile tile;
        tile.datasetUuid = requiredString(entry, "datasetUuid");
        tile.datasetRevision = requiredUnsigned(entry, "datasetRevision");
        tile.chunkX = requiredInteger(entry, "chunkX");
        tile.chunkY = requiredInteger(entry, "chunkY");
        tile.path = requiredString(entry, "path");
        tile.minEasting = requiredNumber(entry, "minE");
        tile.minNorthing = requiredNumber(entry, "minN");
        tile.maxEasting = requiredNumber(entry, "maxE");
        tile.maxNorthing = requiredNumber(entry, "maxN");
        if (tile.path.empty()) {
            throw CommandParseError("scene tile path must not be empty");
        }
        if (tile.maxEasting < tile.minEasting || tile.maxNorthing < tile.minNorthing) {
            throw CommandParseError("scene tile bounds must satisfy min <= max");
        }
        scene.tiles.push_back(std::move(tile));
    }
    return scene;
}

} // namespace infraforge::viewport
