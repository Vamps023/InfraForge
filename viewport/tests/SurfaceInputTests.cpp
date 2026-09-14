#include <doctest/doctest.h>

#include "infraforge/viewport/platform/SurfaceInput.hpp"

using namespace infraforge::viewport;

TEST_SUITE("surface input contract") {
TEST_CASE("pan orbit wheel and actions remain distinguishable") {
    SurfaceInputEvent received;
    setSurfaceInputHandler([&](const SurfaceInputEvent& event) { received = event; });
    dispatchSurfaceInput(SurfaceInputEvent{.gesture=PointerGesture::Pan,.deltaX=4.0,.deltaY=-2.0});
    CHECK(received.gesture == PointerGesture::Pan);
    CHECK(received.deltaX == doctest::Approx(4.0));
    dispatchSurfaceInput(SurfaceInputEvent{.gesture=PointerGesture::Orbit,.deltaX=-3.0});
    CHECK(received.gesture == PointerGesture::Orbit);
    dispatchSurfaceInput(SurfaceInputEvent{.wheelSteps=1.0});
    CHECK(received.gesture == PointerGesture::None);
    CHECK(received.wheelSteps == doctest::Approx(1.0));
    dispatchSurfaceInput(SurfaceInputEvent{.action=ViewportAction::FrameAllTerrain});
    CHECK(received.action == ViewportAction::FrameAllTerrain);
}

TEST_CASE("unregistered handler receives no further camera input") {
    int calls = 0;
    setSurfaceInputHandler([&](const SurfaceInputEvent&) { ++calls; });
    dispatchSurfaceInput({});
    setSurfaceInputHandler(nullptr);
    dispatchSurfaceInput({});
    CHECK_EQ(calls, 1);
}
}
