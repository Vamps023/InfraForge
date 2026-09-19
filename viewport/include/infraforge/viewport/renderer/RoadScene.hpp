#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace infraforge::viewport {

// One vertex of a road mesh: canonical-axis, origin-relative float position
// plus normal. The engine performs the precision-sensitive origin subtraction;
// the viewport only converts north-positive project axes to Vulkan's
// north-negative render-local handedness when building the GPU buffer.
struct RoadSceneVertex {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
    float nx{0.0f};
    float ny{0.0f};
    float nz{1.0f};
};

// One road mesh in the scene: a triangle list with vertices and indices.
struct RoadSceneMesh {
    std::string roadId;
    std::int64_t chunkX{0};
    std::int64_t chunkY{0};
    std::vector<RoadSceneVertex> vertices;
    std::vector<std::uint32_t> indices;

    [[nodiscard]] bool isEmpty() const noexcept { return vertices.empty() || indices.empty(); }
    [[nodiscard]] std::string key() const {
        return roadId + ":" + std::to_string(chunkX) + ":" + std::to_string(chunkY);
    }
};

// Converts the canonical-axis wire vertex to the same render-local handedness
// used by TerrainPass and EditorCamera. Keeping the wire vertex unchanged is
// important: CPU picking compares it with canonical project coordinates.
[[nodiscard]] constexpr std::array<float, 6> roadVertexBufferData(
    const RoadSceneVertex& vertex) noexcept {
    return {vertex.x, -vertex.y, vertex.z, vertex.nx, -vertex.ny, vertex.nz};
}

// Road portion of the scene control command. The viewport uploads and
// renders all meshes; replacing the scene drops all existing road meshes.
struct RoadScene {
    std::vector<RoadSceneMesh> meshes;
    std::uint64_t revision{0};

    [[nodiscard]] bool isEmpty() const noexcept { return meshes.empty(); }
    [[nodiscard]] std::size_t meshCount() const noexcept { return meshes.size(); }

    friend bool operator==(const RoadScene&, const RoadScene&) = default;
};

// Parses the road scene object from a control command JSON. The road
// scene is an optional "roads" field within the scene command. Returns
// an empty RoadScene when the field is absent. Throws CommandParseError
// on malformed road data — a broken road scene is never partially applied.
[[nodiscard]] RoadScene parseRoadScene(const std::string_view json);

} // namespace infraforge::viewport
