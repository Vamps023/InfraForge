#include <doctest/doctest.h>

#include "infraforge/viewport/renderer/EditorCamera.hpp"
#include "infraforge/viewport/renderer/EditorCameraController.hpp"

#include <array>
#include <cmath>
#include <numbers>

using namespace infraforge::viewport;

namespace {
TerrainScene sceneAt(double origin, std::uint64_t revision = 1) {
    TerrainScene scene;
    scene.originEasting=origin; scene.originNorthing=origin+1000.0; scene.originHeight=500.0;
    scene.revision=revision;
    scene.tiles.push_back(TerrainSceneTile{.datasetUuid="a",.datasetRevision=revision,.chunkX=0,.chunkY=0,
        .path="a",.minEasting=origin,.minNorthing=origin+1000.0,.maxEasting=origin+100.0,.maxNorthing=origin+1100.0});
    scene.tiles.push_back(TerrainSceneTile{.datasetUuid="b",.datasetRevision=revision,.chunkX=1,.chunkY=0,
        .path="b",.minEasting=origin+300.0,.minNorthing=origin+1000.0,.maxEasting=origin+400.0,.maxNorthing=origin+1100.0});
    return scene;
}
double separation(const CameraPoint3d& a, const CameraPoint3d& b) {
    return std::hypot(std::hypot(a.x - b.x, a.y - b.y), a.z - b.z);
}
std::array<double, 4> project(const std::array<float, 16>& m, CameraPoint3d p, CameraPoint3d origin) {
    const std::array<double, 4> v{p.x-origin.x, p.y-origin.y, p.z-origin.z, 1.0};
    std::array<double, 4> r{};
    for (std::size_t row=0; row<4; ++row) for (std::size_t col=0; col<4; ++col)
        r[row] += static_cast<double>(m[col*4+row]) * v[col];
    return r;
}
}

TEST_SUITE("editor camera") {
TEST_CASE("perspective matrix is finite with Vulkan depth") {
    EditorCamera camera;
    camera.setViewport(1600, 900);
    camera.setDistance(500.0);
    const auto matrix = camera.viewProjection();
    for (float value : matrix) CHECK(std::isfinite(value));
    const auto clip = project(matrix, camera.target(), camera.renderOrigin());
    CHECK(clip[3] > 0.0);
    CHECK(clip[2]/clip[3] >= 0.0);
    CHECK(clip[2]/clip[3] <= 1.0);
}

TEST_CASE("aspect and resize preserve pose") {
    EditorCamera camera;
    camera.setTarget({10.0,20.0,30.0}); camera.setOrientation(0.7,-0.6); camera.setDistance(800.0);
    camera.setViewport(800,800); const auto square = camera.viewProjection();
    camera.setViewport(1600,800); const auto wide = camera.viewProjection();
    CHECK(wide[0] == doctest::Approx(square[0]*0.5));
    CHECK(camera.target() == CameraPoint3d{10.0,20.0,30.0});
    CHECK(camera.yaw() == doctest::Approx(0.7));
    CHECK(camera.pitch() == doctest::Approx(-0.6));
    CHECK(camera.distance() == doctest::Approx(800.0));
}

TEST_CASE("top is orthographic and preserves focus") {
    EditorCamera camera;
    camera.setTarget({100.0,-200.0,5.0}); camera.setProjection(CameraProjection::Top);
    const auto matrix = camera.viewProjection();
    const auto a = project(matrix,{100.0,-200.0,5.0},camera.renderOrigin());
    const auto b = project(matrix,{100.0,-200.0,50.0},camera.renderOrigin());
    CHECK(a[0]/a[3] == doctest::Approx(b[0]/b[3]));
    CHECK(a[1]/a[3] == doctest::Approx(b[1]/b[3]));
    CHECK(camera.target() == CameraPoint3d{100.0,-200.0,5.0});
}

TEST_CASE("orbit preserves distance and pitch clamps") {
    EditorCamera camera;
    camera.setTarget({4.0,5.0,6.0}); camera.setDistance(250.0); camera.setOrientation(0.0,-0.5);
    camera.orbit(std::numbers::pi/2.0,0.2);
    CHECK(separation(camera.position(),camera.target()) == doctest::Approx(250.0));
    camera.orbit(0.0,1000.0); CHECK(camera.pitch() < 0.0);
    camera.orbit(0.0,-2000.0); CHECK(camera.pitch() > -std::numbers::pi/2.0);
    CHECK(separation(camera.position(),camera.target()) == doctest::Approx(250.0));
}

TEST_CASE("pan is camera-relative and dolly clamps") {
    EditorCamera camera; camera.setViewport(1000,1000); camera.setDistance(100.0);
    camera.setOrientation(0.0,-0.5); camera.pan(10.0,0.0);
    CHECK(camera.target().x < 0.0); CHECK(camera.target().y == doctest::Approx(0.0));
    camera.setTarget({0.0,0.0,0.0}); camera.setOrientation(std::numbers::pi/2.0,-0.5); camera.pan(10.0,0.0);
    CHECK(camera.target().x == doctest::Approx(0.0).epsilon(1e-10)); CHECK(camera.target().y < 0.0);
    camera.dolly(10000.0); CHECK(camera.distance() >= 0.1);
    camera.dolly(-10000.0); CHECK(camera.distance() <= 100000000.0);
}

TEST_CASE("frame centers bounds and contains all corners") {
    EditorCamera camera; camera.setViewport(1600,900);
    const CameraBounds3d bounds{{-100.0,-50.0,20.0},{100.0,50.0,120.0}};
    camera.frame(bounds,1.2); CHECK(camera.target() == CameraPoint3d{0.0,0.0,70.0});
    const auto matrix = camera.viewProjection();
    for (double x : {-100.0,100.0}) for (double y : {-50.0,50.0}) for (double z : {20.0,120.0}) {
        const auto clip=project(matrix,{x,y,z},camera.renderOrigin());
        CHECK(std::abs(clip[0]/clip[3]) < 1.0); CHECK(std::abs(clip[1]/clip[3]) < 1.0);
        CHECK(clip[2]/clip[3] > 0.0); CHECK(clip[2]/clip[3] < 1.0);
    }
}

TEST_CASE("large UTM coordinates equal origin-local math") {
    EditorCamera global; global.setViewport(1920,1080); global.setRenderOrigin({380000.0,2049000.0,600.0});
    global.setTarget({380125.0,2049250.0,650.0}); global.setDistance(1200.0); global.setOrientation(0.8,-0.65);
    EditorCamera local; local.setViewport(1920,1080); local.setTarget({125.0,250.0,50.0});
    local.setDistance(1200.0); local.setOrientation(0.8,-0.65);
    const auto a=global.viewProjection(), b=local.viewProjection();
    for (std::size_t i=0;i<a.size();++i) CHECK(a[i] == doctest::Approx(b[i]).epsilon(1e-6));
}

TEST_CASE("screen center maps to canonical horizontal plane") {
    EditorCamera camera; camera.setViewport(1000, 800);
    camera.setTarget({500000.0, 4650000.0, 100.0});
    camera.setDistance(500.0); camera.setOrientation(0.0, -0.7);
    const auto point = camera.screenToHorizontalPlane(500.0, 400.0, 100.0);
    REQUIRE(point.has_value());
    CHECK(point->x == doctest::Approx(500000.0));
    CHECK(point->y == doctest::Approx(4650000.0));
    CHECK(point->z == doctest::Approx(100.0));
    CHECK_FALSE(camera.screenToHorizontalPlane(-1.0, 0.0, 100.0).has_value());
}

TEST_CASE("scene refresh and resize preserve an established user camera") {
    EditorCameraController controller;
    controller.camera().setViewport(1200,800);
    controller.adoptScene(sceneAt(380000.0));
    controller.handleInput(SurfaceInputEvent{.gesture=PointerGesture::Orbit,.deltaX=25.0,.deltaY=-12.0,.datasetUuid={}});
    controller.handleInput(SurfaceInputEvent{.gesture=PointerGesture::Pan,.deltaX=8.0,.deltaY=4.0,.datasetUuid={}});
    const auto target=controller.camera().target(); const double yaw=controller.camera().yaw();
    const double pitch=controller.camera().pitch(), distance=controller.camera().distance();
    controller.adoptScene(sceneAt(380000.0,2));
    controller.camera().setViewport(1800,900);
    CHECK(controller.camera().target() == target);
    CHECK(controller.camera().yaw() == doctest::Approx(yaw));
    CHECK(controller.camera().pitch() == doctest::Approx(pitch));
    CHECK(controller.camera().distance() == doctest::Approx(distance));
}

TEST_CASE("focus selected frames one dataset and frame all covers the scene") {
    EditorCameraController controller; controller.camera().setViewport(1000,800);
    controller.adoptScene(sceneAt(380000.0));
    controller.handleInput(SurfaceInputEvent{.action=ViewportAction::FocusTerrain,.datasetUuid="b"});
    CHECK(controller.camera().target().x == doctest::Approx(380350.0));
    controller.handleInput(SurfaceInputEvent{.action=ViewportAction::FrameAllTerrain,.datasetUuid={}});
    CHECK(controller.camera().target().x == doctest::Approx(380200.0));
}
}
