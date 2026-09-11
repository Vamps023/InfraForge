#include "infraforge/runtime/Timestamp.hpp"

#include <chrono>
#include <ctime>
#include <string>

namespace infraforge::runtime {

std::string utcTimestampNow() {
    const std::time_t time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    char buffer[32]{};
    const std::size_t written = std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return std::string{buffer, written};
}

} // namespace infraforge::runtime
