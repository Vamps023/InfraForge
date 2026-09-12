#pragma once

#include <atomic>
#include <functional>
#include <thread>
#include <utility>

namespace infraforge::viewport {

// Owns a worker thread and its stop flag. stop() (and destruction) always
// joins a joinable thread — including one that cleared its own running flag
// after a frame failure — because destroying a joinable std::thread calls
// std::terminate. The worker receives the flag as a mutable reference: it
// polls it to keep running and may clear it when a failure ends rendering.
class RenderThread final {
public:
    using Body = std::function<void(std::atomic_bool&)>;

    RenderThread() = default;
    ~RenderThread() {
        stop();
    }

    RenderThread(const RenderThread&) = delete;
    RenderThread& operator=(const RenderThread&) = delete;

    void start(Body body) {
        stop();
        running_.store(true, std::memory_order_release);
        thread_ = std::thread([this, body = std::move(body)] { body(running_); });
    }

    void stop() {
        running_.store(false, std::memory_order_release);
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    [[nodiscard]] bool running() const noexcept { return running_.load(std::memory_order_acquire); }

private:
    std::thread thread_;
    std::atomic_bool running_{false};
};

} // namespace infraforge::viewport
