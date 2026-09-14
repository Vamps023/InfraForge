#include "infraforge/viewport/renderer/EditorCameraController.hpp"

#include <algorithm>

namespace infraforge::viewport {

void EditorCameraController::frame(const std::optional<std::string>& datasetUuid) {
    const auto included = [&](const TerrainSceneTile& tile) {
        return !datasetUuid.has_value() || tile.datasetUuid == *datasetUuid;
    };
    const auto first = std::find_if(scene_.tiles.begin(), scene_.tiles.end(), included);
    if (first == scene_.tiles.end()) return;
    double minE=first->minEasting, maxE=first->maxEasting;
    double minN=first->minNorthing, maxN=first->maxNorthing;
    for (const TerrainSceneTile& tile : scene_.tiles) {
        if (!included(tile)) continue;
        minE=std::min(minE,tile.minEasting); maxE=std::max(maxE,tile.maxEasting);
        minN=std::min(minN,tile.minNorthing); maxN=std::max(maxN,tile.maxNorthing);
    }
    camera_.frame({{minE,minN,scene_.originHeight},{maxE,maxN,scene_.originHeight}});
}

void EditorCameraController::adoptScene(const TerrainScene& scene) {
    scene_ = scene;
    camera_.setRenderOrigin({scene.originEasting,scene.originNorthing,scene.originHeight});
    if (scene.tiles.empty()) {
        initialSceneFramed_ = false;
        focusedDatasetUuid_.reset();
        camera_.clearUserMoved();
    } else if (!initialSceneFramed_ && !camera_.userMoved()) {
        frame(std::nullopt);
        initialSceneFramed_ = true;
    }
}

void EditorCameraController::handleInput(const SurfaceInputEvent& event) {
    if (event.wheelSteps != 0.0) camera_.dolly(event.wheelSteps);
    if (event.gesture == PointerGesture::Pan) camera_.pan(event.deltaX,event.deltaY);
    if (event.gesture == PointerGesture::Orbit) camera_.orbit(-event.deltaX*0.006,-event.deltaY*0.006);
    switch (event.action) {
    case ViewportAction::FocusTerrain:
        if (!event.datasetUuid.empty()) focusedDatasetUuid_=event.datasetUuid;
        frame(focusedDatasetUuid_); break;
    case ViewportAction::FrameAllTerrain:
        focusedDatasetUuid_.reset(); frame(std::nullopt); break;
    case ViewportAction::Perspective: camera_.setProjection(CameraProjection::Perspective); break;
    case ViewportAction::Top: camera_.setProjection(CameraProjection::Top); break;
    case ViewportAction::None: break;
    }
}

} // namespace infraforge::viewport
