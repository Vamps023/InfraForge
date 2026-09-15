#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace infraforge::viewport {

// One vertex of a road mesh: position (render-local float) + normal.
// The vertex is already converted to render-local coordinates by the
// engine before transport; the viewport does not touch canonical space.
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
    std::vector<RoadSceneVertex> vertices;
    std::vector<std::uint32_t> indices;

    [[nodiscard]] bool isEmpty() const noexcept { return vertices.empty() || indices.empty(); }
};

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
