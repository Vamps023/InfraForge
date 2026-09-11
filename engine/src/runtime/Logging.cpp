#include "infraforge/runtime/Logging.hpp"

#include "infraforge/runtime/Timestamp.hpp"

#include <iostream>
#include <mutex>

namespace infraforge::runtime {
namespace {

std::mutex g_logMutex;

void emitLine(
    std::string_view level,
    std::string_view component,
    std::string_view event,
    std::initializer_list<std::pair<std::string_view, std::string_view>> fields) {
    // Field values are engine-controlled identifiers/messages, so plain JSON
    // string escaping of quotes and backslashes is sufficient here.
    auto escape = [](std::string_view value) {
        std::string escaped;
        escaped.reserve(value.size());
        for (const char character : value) {
            if (character == '"' || character == '\\') {
                escaped.push_back('\\');
            }
            if (static_cast<unsigned char>(character) < 0x20) {
                escaped.push_back(' ');
                continue;
            }
            escaped.push_back(character);
        }
        return escaped;
    };

    std::string line;
    line.reserve(128 + fields.size() * 32);
    line += "{\"ts\":\"";
    line += utcTimestampNow();
    line += "\",\"level\":\"";
    line += level;
    line += "\",\"component\":\"";
    line += component;
    line += "\",\"event\":\"";
    line += event;
    line += '"';
    for (const auto& [key, value] : fields) {
        line += ",\"";
        line += key;
        line += "\":\"";
        line += escape(value);
        line += '"';
    }
    line += "}\n";

    std::lock_guard guard{g_logMutex};
    std::cerr << line;
    std::cerr.flush();
}

} // namespace

void logInfo(
    std::string_view component,
    std::string_view event,
    std::initializer_list<std::pair<std::string_view, std::string_view>> fields) {
    emitLine("info", component, event, fields);
}

void logWarn(
    std::string_view component,
    std::string_view event,
    std::initializer_list<std::pair<std::string_view, std::string_view>> fields) {
    emitLine("warn", component, event, fields);
}

void logError(
    std::string_view component,
    std::string_view event,
    std::initializer_list<std::pair<std::string_view, std::string_view>> fields) {
    emitLine("error", component, event, fields);
}

} // namespace infraforge::runtime
