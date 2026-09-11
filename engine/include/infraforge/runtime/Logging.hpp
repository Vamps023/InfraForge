#pragma once

#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

namespace infraforge::runtime {

// Emits one structured JSON line to stderr:
// {"ts":"...","level":"info","component":"...","event":"...", ...fields}
// Session tokens and other secrets must never be passed as fields.
void logInfo(
    std::string_view component,
    std::string_view event,
    std::initializer_list<std::pair<std::string_view, std::string_view>> fields = {});
void logWarn(
    std::string_view component,
    std::string_view event,
    std::initializer_list<std::pair<std::string_view, std::string_view>> fields = {});
void logError(
    std::string_view component,
    std::string_view event,
    std::initializer_list<std::pair<std::string_view, std::string_view>> fields = {});

} // namespace infraforge::runtime
