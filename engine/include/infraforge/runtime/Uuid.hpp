#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace infraforge::runtime {

// RFC 4122 version 4 UUID backed by std::random_device. Canonical text form
// is lowercase 8-4-4-4-12. Used for project identity and event correlation;
// never for entity identity within a project (those use the same form but
// are minted by their owning domain services).
[[nodiscard]] std::string generateUuidV4();

[[nodiscard]] bool isValidUuidText(std::string_view text) noexcept;

// Extracts the version/variant nibbles of a binary UUID for tests.
[[nodiscard]] std::array<std::uint8_t, 16> uuidTextToBytes(std::string_view text);

} // namespace infraforge::runtime
