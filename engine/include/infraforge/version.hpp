#pragma once

#include <cstdint>
#include <string_view>

namespace infraforge {

inline constexpr std::string_view kEngineExecutableName = "infraforge-engine";
inline constexpr std::string_view kEngineVersion = "0.1.0";
inline constexpr std::uint32_t kProtocolMajor = 1;
inline constexpr std::uint32_t kProtocolMinor = 9;

} // namespace infraforge
