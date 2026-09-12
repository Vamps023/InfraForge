#pragma once

#include <atomic>

namespace infraforge::application::validation {

// Cooperative cancellation boundary for validators. The validation service
// sets the flag when a cancellation request arrives; long-running validators
// poll `isCancelled()` at safe points and return early. The token is
// thread-safe because the service runs on the application executor while
// cancellation requests may arrive from the network thread.
//
// Cancellation is advisory: a validator that returns early must not leave
// partial diagnostics that imply a complete result. The service discards
// partial results when cancelled.
class CancellationToken {
public:
    void requestCancellation() noexcept {
        cancelled_.store(true, std::memory_order_release);
    }

    [[nodiscard]] bool isCancelled() const noexcept {
        return cancelled_.load(std::memory_order_acquire);
    }

private:
    std::atomic_bool cancelled_{false};
};

} // namespace infraforge::application::validation
