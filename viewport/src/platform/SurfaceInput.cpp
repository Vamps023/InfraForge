#include "infraforge/viewport/platform/SurfaceInput.hpp"

#include <mutex>

namespace infraforge::viewport {
namespace {

std::mutex handlerMutex;
SurfaceInputHandler handler;

} // namespace

void setSurfaceInputHandler(SurfaceInputHandler inputHandler) {
    std::lock_guard lock{handlerMutex};
    handler = std::move(inputHandler);
}

// Called by the platform window procedures; safe to invoke from the message
// pump thread even while no handler is registered.
void dispatchSurfaceInput(const SurfaceInputEvent& event) {
    SurfaceInputHandler local;
    {
        std::lock_guard lock{handlerMutex};
        local = handler;
    }
    if (local) {
        local(event);
    }
}

} // namespace infraforge::viewport
