#include "infraforge/runtime/Timestamp.hpp"

#include <chrono>
#include <ctime>
#include <format>

namespace infraforge::runtime {

std::string utcTimestampNow() {
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    return std::format("{:%Y-%m-%dT%H:%M:%SZ}", now);
}

} // namespace infraforge::runtime
