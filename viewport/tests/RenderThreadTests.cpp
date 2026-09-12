#include <doctest/doctest.h>

#include "infraforge/viewport/renderer/RenderThread.hpp"

#include <atomic>
#include <chrono>
#include <thread>

namespace {

using infraforge::viewport::RenderThread;

} // namespace

TEST_SUITE("render thread lifecycle") {
    TEST_CASE("stop joins a worker that cleared its own running flag") {
        RenderThread thread;
        std::atomic_bool bodyExited{false};
        thread.start([&bodyExited](std::atomic_bool& running) {
            // Renderer failure path: the worker stops itself and exits while
            // the thread object remains joinable.
            running.store(false, std::memory_order_release);
            bodyExited.store(true, std::memory_order_release);
        });
        // With the historical bug (join skipped because the flag was already
        // false) the joinable std::thread was destroyed -> std::terminate.
        thread.stop();
        CHECK(bodyExited.load(std::memory_order_acquire));
        CHECK_FALSE(thread.running());
    }

    TEST_CASE("a started worker observes a running flag that stop() ends") {
        RenderThread thread;
        std::atomic_bool observedRunning{false};
        std::atomic_bool bodyDone{false};
        thread.start([&](std::atomic_bool& running) {
            observedRunning.store(running.load(std::memory_order_acquire),
                std::memory_order_release);
            while (running.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }
            bodyDone.store(true, std::memory_order_release);
        });
        while (!observedRunning.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        CHECK(thread.running());
        thread.stop();
        CHECK(bodyDone.load(std::memory_order_acquire));
        CHECK_FALSE(thread.running());
    }

    TEST_CASE("stop without start and repeated stops are safe no-ops") {
        RenderThread thread;
        thread.stop();
        CHECK_FALSE(thread.running());
        thread.stop();
        CHECK_FALSE(thread.running());
    }
}
