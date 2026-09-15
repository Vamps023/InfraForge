#include "infraforge/viewport/renderer/RoadScene.hpp"
#include "infraforge/viewport/control/ControlProtocol.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <limits>

namespace infraforge::viewport {
namespace {

[[noreturn]] void failParse(std::string message) {
    throw CommandParseError{std::move(message)};
}

// Bounded vertex/index counts to reject unbounded payloads.
inline constexpr std::size_t kMaxVertexCount = 1 << 24;   // ~16M vertices
inline constexpr std::size_t kMaxIndexCount = 1 << 26;    // ~64M indices
inline constexpr std::size_t kMaxRoadCount = 1 << 16;    // ~65K roads

float requireFiniteFloat(const nlohmann::json& json, const char* key) {
    if (!json.contains(key) || !json.at(key).is_number()) {
        failParse(std::string("road vertex field '") + key + "' must be a number");
    }
    const double value = json.at(key).get<double>();
    // Blocker 11: reject non-finite numbers (NaN, infinity).
    if (!std::isfinite(value)) {
        failParse(std::string("road vertex field '") + key +
            "' must be finite (not NaN or infinity)");
    }
    return static_cast<float>(value);
}

// Validates an optional finite float normal component with a default.
float optionalFiniteFloat(const nlohmann::json& json, const char* key, float defaultValue) {
    if (!json.contains(key)) return defaultValue;
    if (!json.at(key).is_number()) {
        failParse(std::string("road vertex field '") + key +
            "' must be a number when present");
    }
    const double value = json.at(key).get<double>();
    if (!std::isfinite(value)) {
        failParse(std::string("road vertex field '") + key +
            "' must be finite (not NaN or infinity)");
    }
    return static_cast<float>(value);
}

// Validates a non-negative integer index within uint32 range and vertex bounds.
std::uint32_t requireValidIndex(const nlohmann::json& idx, std::uint32_t vertexCount) {
    if (!idx.is_number()) {
        failParse("each road index must be a number");
    }
    // Blocker 11: reject non-finite or non-integer indices.
    const double value = idx.get<double>();
    if (!std::isfinite(value)) {
        failParse("road index must be finite (not NaN or infinity)");
    }
    if (value < 0.0) {
        failParse("road index must be non-negative");
    }
    // Blocker 11: reject unsafe numeric conversion (doubles truncated to
    // uint32). Require the value to be an integer within uint32 range.
    if (value > static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
        failParse("road index exceeds uint32 range");
    }
    // Check integer-ness: fractional indices are not valid mesh indices.
    const double intPart = std::floor(value);
    if (std::abs(value - intPart) > 1e-9) {
        failParse("road index must be an integer");
    }
    const auto u32 = static_cast<std::uint32_t>(value);
    // Blocker 11: reject out-of-range indices (index >= vertex count).
    if (u32 >= vertexCount) {
        failParse("road index " + std::to_string(u32) +
            " is out of range (vertex count: " + std::to_string(vertexCount) + ")");
    }
    return u32;
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
    // Blocker 11: bounded road count.
    if (roads.size() > kMaxRoadCount) {
        failParse("road scene exceeds maximum road count");
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
        // Blocker 11: reject empty road IDs.
        if (mesh.roadId.empty()) {
            failParse("road mesh 'roadId' must not be empty");
        }

        if (!meshJson.contains("vertices") || !meshJson.at("vertices").is_array()) {
            failParse("road mesh requires array 'vertices'");
        }
        const auto& verts = meshJson.at("vertices");
        // Blocker 11: bounded vertex counts.
        if (verts.size() > kMaxVertexCount) {
            failParse("road mesh exceeds maximum vertex count");
        }
        // Blocker 11: reject empty vertex arrays (a mesh with no vertices
        // is not a valid renderable mesh).
        if (verts.empty()) {
            failParse("road mesh 'vertices' must not be empty");
        }
        mesh.vertices.reserve(verts.size());
        for (const auto& v : verts) {
            if (!v.is_object()) {
                failParse("each road vertex must be an object");
            }
            RoadSceneVertex vertex;
            vertex.x = requireFiniteFloat(v, "x");
            vertex.y = requireFiniteFloat(v, "y");
            vertex.z = requireFiniteFloat(v, "z");
            vertex.nx = optionalFiniteFloat(v, "nx", 0.0f);
            vertex.ny = optionalFiniteFloat(v, "ny", 0.0f);
            vertex.nz = optionalFiniteFloat(v, "nz", 1.0f);
            mesh.vertices.push_back(vertex);
        }

        if (!meshJson.contains("indices") || !meshJson.at("indices").is_array()) {
            failParse("road mesh requires array 'indices'");
        }
        const auto& idxs = meshJson.at("indices");
        // Blocker 11: bounded index counts.
        if (idxs.size() > kMaxIndexCount) {
            failParse("road mesh exceeds maximum index count");
        }
        mesh.indices.reserve(idxs.size());
        const auto vertexCount = static_cast<std::uint32_t>(mesh.vertices.size());
        for (const auto& idx : idxs) {
            mesh.indices.push_back(requireValidIndex(idx, vertexCount));
        }

        scene.meshes.push_back(std::move(mesh));
    }

    // Blocker 11: validate revision representation. The revision is a
    // uint64; accept JSON numbers that fit losslessly, or strings that
    // parse as uint64 (to support lossless transport of large revisions).
    if (parsed.contains("roadRevision")) {
        const auto& rev = parsed.at("roadRevision");
        if (rev.is_number()) {
            const double value = rev.get<double>();
            if (!std::isfinite(value) || value < 0.0) {
                failParse("roadRevision must be a non-negative finite number");
            }
            if (value > static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
                failParse("roadRevision exceeds uint64 range");
            }
            scene.revision = static_cast<std::uint64_t>(value);
        } else if (rev.is_string()) {
            // String-encoded uint64 for lossless transport of large revisions.
            const auto& str = rev.get<std::string>();
            try {
                scene.revision = std::stoull(str);
            } catch (...) {
                failParse("roadRevision string must be a valid uint64");
            }
        } else {
            failParse("roadRevision must be a number or numeric string");
        }
    }

    return scene;
}

} // namespace infraforge::viewport
