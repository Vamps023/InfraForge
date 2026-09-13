#pragma once

#include <functional>

namespace infraforge::viewport {

// Raw mouse input of the native child surface. The renderer owns the
// camera; this event only carries deltas (wheel steps, drag pixels).
struct SurfaceInputEvent {
    double wheelSteps{0.0}; // positive = wheel up (zoom in)
    double dragDx{0.0};     // pixels dragged left/right while left button held
    double dragDy{0.0};     // pixels dragged up/down while left button held
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
