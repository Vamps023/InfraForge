#include <doctest/doctest.h>

#include "infraforge/viewport/renderer/GridCamera.hpp"

#include <array>
#include <utility>

namespace {

using infraforge::viewport::GridCamera;

} // namespace

TEST_SUITE("grid camera") {
    TEST_CASE("a world origin maps to the viewport center") {
        GridCamera camera;
        camera.setViewport(1000, 500);
        camera.setCenter(0.0, 0.0);
        camera.setMetersPerPixel(1.0);

        std::array<float, 16> m{};
        camera.writeViewProjection(m.data());

        // Transform (0,0): clip.x = m[12], clip.y = m[13] (column-major).
        CHECK_EQ(m[12], doctest::Approx(0.0F).epsilon(0.0001));
        CHECK_EQ(m[13], doctest::Approx(0.0F).epsilon(0.0001));
    }

    TEST_CASE("the visible world extent matches viewport times meters-per-pixel") {
        GridCamera camera;
        camera.setViewport(1000, 500);
        camera.setCenter(0.0, 0.0);
        camera.setMetersPerPixel(2.0);

        std::array<float, 16> m{};
        camera.writeViewProjection(m.data());

        const auto transform = [&](const float wx, const float wy) {
            const float cx = m[0] * wx + m[4] * wy + m[12];
            const float cy = m[1] * wx + m[5] * wy + m[13];
            return std::pair<float, float>{cx, cy};
        };

        // Right edge (x=+1000 m) maps to clip x=+1; top edge (y=-500 m) to
        // clip y=-1 (Y grows downward on screen).
        const auto right = transform(1000.0F, 0.0F);
        CHECK_EQ(right.first, doctest::Approx(1.0F).epsilon(0.001));

        const auto top = transform(0.0F, -500.0F);
        CHECK_EQ(top.second, doctest::Approx(-1.0F).epsilon(0.001));

        const auto bottom = transform(0.0F, 500.0F);
        CHECK_EQ(bottom.second, doctest::Approx(1.0F).epsilon(0.001));
    }

    TEST_CASE("panning moves the world center") {
        GridCamera camera;
        camera.setViewport(800, 600);
        camera.setCenter(40.0, -20.0);
        camera.setMetersPerPixel(1.0);

        std::array<float, 16> m{};
        camera.writeViewProjection(m.data());

        const auto at = [&](const float wx, const float wy) {
            return std::pair<float, float>{
                m[0] * wx + m[4] * wy + m[12],
                m[1] * wx + m[5] * wy + m[13]};
        };
        const auto center = at(40.0F, -20.0F);
        CHECK_EQ(center.first, doctest::Approx(0.0F).epsilon(0.001));
        CHECK_EQ(center.second, doctest::Approx(0.0F).epsilon(0.001));
    }

    TEST_CASE("viewport clamps degenerate sizes") {
        GridCamera camera;
        camera.setViewport(0, 0);
        CHECK_EQ(camera.viewportWidth(), 1);
        CHECK_EQ(camera.viewportHeight(), 1);
    }
}
