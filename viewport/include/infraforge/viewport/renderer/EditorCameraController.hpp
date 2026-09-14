#pragma once

#include "infraforge/viewport/platform/SurfaceInput.hpp"
#include "infraforge/viewport/renderer/EditorCamera.hpp"
#include "infraforge/viewport/renderer/TerrainScene.hpp"

#include <optional>
#include <string>

namespace infraforge::viewport {

// Render-thread camera/scene policy separated from Vulkan so scene refresh,
// framing, and input behavior are deterministic CPU-testable logic.
class EditorCameraController {
public:
    void adoptScene(const TerrainScene& scene);
    void handleInput(const SurfaceInputEvent& event);

    [[nodiscard]] EditorCamera& camera() noexcept { return camera_; }
    [[nodiscard]] const EditorCamera& camera() const noexcept { return camera_; }
    [[nodiscard]] const TerrainScene& scene() const noexcept { return scene_; }

private:
    void frame(const std::optional<std::string>& datasetUuid);

    EditorCamera camera_;
    TerrainScene scene_;
    std::optional<std::string> focusedDatasetUuid_;
    bool initialSceneFramed_{false};
};

} // namespace infraforge::viewport
