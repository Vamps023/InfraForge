#include "infraforge/viewport/renderer/RoadScene.hpp"
#include "infraforge/viewport/control/ControlProtocol.hpp"

#include <nlohmann/json.hpp>

namespace infraforge::viewport {
namespace {

[[noreturn]] void failParse(std::string message) {
    throw CommandParseError{std::move(message)};
}

float requireFloat(const nlohmann::json& json, const char* key) {
    if (!json.contains(key) || !json.at(key).is_number()) {
        failParse(std::string("road vertex field '") + key + "' must be a number");
    }
    return static_cast<float>(json.at(key).get<double>());
}

} // namespace

RoadScene parseRoadScene(const std::string_view json) {
    RoadScene scene;
    nlohmann::json parsed;
    try {
        parsed = nlohmann::json::parse(json);
    } catch (const nlohmann::json::exception& error) {
        failParse(std::string("malformed road scene: ") + error.what());
    }

    // The road scene is an optional "roads" field within the scene command.
    if (!parsed.is_object() || !parsed.contains("roads")) {
        return scene;  // No road data — empty scene.
    }

    const auto& roads = parsed.at("roads");
    if (!roads.is_array()) {
        failParse("road scene 'roads' field must be an array");
    }

    scene.meshes.reserve(roads.size());
    for (const auto& meshJson : roads) {
        if (!meshJson.is_object()) {
            failParse("each road mesh must be an object");
        }
        RoadSceneMesh mesh;
        if (!meshJson.contains("roadId") || !meshJson.at("roadId").is_string()) {
            failParse("road mesh requires string 'roadId'");
        }
        mesh.roadId = meshJson.at("roadId").get<std::string>();

        if (!meshJson.contains("vertices") || !meshJson.at("vertices").is_array()) {
            failParse("road mesh requires array 'vertices'");
        }
        const auto& verts = meshJson.at("vertices");
        mesh.vertices.reserve(verts.size());
        for (const auto& v : verts) {
            if (!v.is_object()) {
                failParse("each road vertex must be an object");
            }
            RoadSceneVertex vertex;
            vertex.x = requireFloat(v, "x");
            vertex.y = requireFloat(v, "y");
            vertex.z = requireFloat(v, "z");
            vertex.nx = v.contains("nx") && v.at("nx").is_number()
                ? static_cast<float>(v.at("nx").get<double>()) : 0.0f;
            vertex.ny = v.contains("ny") && v.at("ny").is_number()
                ? static_cast<float>(v.at("ny").get<double>()) : 0.0f;
            vertex.nz = v.contains("nz") && v.at("nz").is_number()
                ? static_cast<float>(v.at("nz").get<double>()) : 1.0f;
            mesh.vertices.push_back(vertex);
        }

        if (!meshJson.contains("indices") || !meshJson.at("indices").is_array()) {
            failParse("road mesh requires array 'indices'");
        }
        const auto& idxs = meshJson.at("indices");
        mesh.indices.reserve(idxs.size());
        for (const auto& idx : idxs) {
            if (!idx.is_number()) {
                failParse("each road index must be a number");
            }
            mesh.indices.push_back(static_cast<std::uint32_t>(idx.get<double>()));
        }

        scene.meshes.push_back(std::move(mesh));
    }

    if (parsed.contains("roadRevision") && parsed.at("roadRevision").is_number()) {
        scene.revision = static_cast<std::uint64_t>(parsed.at("roadRevision").get<double>());
    }

    return scene;
}

} // namespace infraforge::viewport
