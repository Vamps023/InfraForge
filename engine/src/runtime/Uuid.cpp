#include "infraforge/runtime/Uuid.hpp"

#include <random>
#include <stdexcept>

namespace infraforge::runtime {
namespace {

std::array<std::uint8_t, 16> generateRandomBytes() {
    // A fresh std::random_device per call avoids reproducing a deterministic
    // sequence if an implementation maps it to a seeded generator.
    std::random_device device;
    std::array<std::uint8_t, 16> bytes{};
    constexpr std::size_t kBytesPerDraw = sizeof(std::random_device::result_type);
    static_assert(16 % kBytesPerDraw == 0, "unexpected random_device word size");

    for (std::size_t offset = 0; offset < bytes.size(); offset += kBytesPerDraw) {
        const auto value = device();
        for (std::size_t byteIndex = 0; byteIndex < kBytesPerDraw; ++byteIndex) {
            bytes[offset + byteIndex] =
                static_cast<std::uint8_t>((value >> (8 * byteIndex)) & 0xFFU);
        }
    }
    return bytes;
}

int hexNibble(const char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    throw std::invalid_argument("invalid hexadecimal character in UUID text");
}

} // namespace

std::string generateUuidV4() {
    auto bytes = generateRandomBytes();
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0F) | 0x40);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3F) | 0x80);

    static constexpr char kHexDigits[] = "0123456789abcdef";
    std::string text;
    text.reserve(36);
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index == 4 || index == 6 || index == 8 || index == 10) {
            text.push_back('-');
        }
        text.push_back(kHexDigits[bytes[index] >> 4]);
        text.push_back(kHexDigits[bytes[index] & 0x0F]);
    }
    return text;
}

bool isValidUuidText(std::string_view text) noexcept {
    if (text.size() != 36) {
        return false;
    }
    static constexpr std::array<std::size_t, 4> kDashPositions{8, 13, 18, 23};
    std::size_t dashIndex = 0;
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (dashIndex < kDashPositions.size() && index == kDashPositions[dashIndex]) {
            if (text[index] != '-') {
                return false;
            }
            ++dashIndex;
            continue;
        }
        const char character = text[index];
        const bool hex = (character >= '0' && character <= '9')
            || (character >= 'a' && character <= 'f');
        if (!hex) {
            return false;
        }
    }
    return true;
}

std::array<std::uint8_t, 16> uuidTextToBytes(std::string_view text) {
    if (!isValidUuidText(text)) {
        throw std::invalid_argument("malformed UUID text");
    }
    std::array<std::uint8_t, 16> bytes{};
    std::size_t byteIndex = 0;
    for (std::size_t index = 0; index < text.size(); index += 2) {
        while (text[index] == '-') {
            ++index;
        }
        bytes[byteIndex++] =
            static_cast<std::uint8_t>((hexNibble(text[index]) << 4) | hexNibble(text[index + 1]));
    }
    return bytes;
}

} // namespace infraforge::runtime
