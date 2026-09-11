#include "infraforge/runtime/FileSystemUtf8.hpp"

namespace infraforge::runtime {

std::string utf8String(const std::filesystem::path& path) {
    const auto utf8 = path.u8string();
    return std::string{utf8.begin(), utf8.end()};
}

std::filesystem::path pathFromUtf8(const std::string_view utf8Text) {
    const auto* first = reinterpret_cast<const char8_t*>(utf8Text.data());
    return std::filesystem::path{first, first + utf8Text.size()};
}

} // namespace infraforge::runtime
