#pragma once

#include <functional>
#include <string>

namespace infraforge::viewport {

enum class PointerGesture {
    None,
    Pan,
    Orbit,
};

enum class ViewportAction {
    None,
    FocusTerrain,
    FrameAllTerrain,
    Perspective,
    Top,
};

// Neutral native-surface input. Platform procedures translate OS messages;
// the renderer owns all camera semantics.
struct SurfaceInputEvent {
    PointerGesture gesture{PointerGesture::None};
    ViewportAction action{ViewportAction::None};
    double wheelSteps{0.0};
    double deltaX{0.0};
    double deltaY{0.0};
    bool primaryClick{false};
    double screenX{0.0};
    double screenY{0.0};
    std::string datasetUuid;
};

using SurfaceInputHandler = std::function<void(const SurfaceInputEvent&)>;

// Registers the process-wide surface input handler. The viewport owns a
// single native child surface at a time (one window class, one process), so
// a single handler is the complete contract; passing nullptr unregisters.
void setSurfaceInputHandler(SurfaceInputHandler handler);

// Platform window procedures call this with raw mouse input. Defined in
// SurfaceInput.cpp; keeps the wndproc free of handler-lifetime concerns.
void dispatchSurfaceInput(const SurfaceInputEvent& event);

} // namespace infraforge::viewport
