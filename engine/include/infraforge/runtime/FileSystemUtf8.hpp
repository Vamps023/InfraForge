#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace infraforge::runtime {

// UTF-8 text form of a filesystem path for SQLite APIs, protocol payloads,
// and structured logs.
[[nodiscard]] std::string utf8String(const std::filesystem::path& path);

// Filesystem path from UTF-8 text (protocol payloads use UTF-8 strings).
// std::filesystem::u8path is deprecated in C++20; this constructs from
// char8_t iterators instead.
[[nodiscard]] std::filesystem::path pathFromUtf8(std::string_view utf8Text);

} // namespace infraforge::runtime
