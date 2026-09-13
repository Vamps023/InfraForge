#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace infraforge::runtime {

// Portable SHA-256 (FIPS 180-4). Used for terrain import provenance and
// project-owned storage integrity checks so a corrupted or silently
// substituted raster file can be detected on reopen. Streaming byte API;
// no platform crypto dependency so engine tests and Linux CI use one path.
class Sha256 {
public:
    Sha256() noexcept;

    void update(const void* data, std::size_t size);

    // Finalizes the digest. The instance must not be updated afterwards.
    [[nodiscard]] std::array<std::uint8_t, 32> finish();

private:
    void compressBlock(const std::uint8_t* block);

    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> buffer_{};
    std::uint64_t totalBytes_{0};
    std::size_t buffered_{0};
};

// Hashes a file's bytes in bounded streaming chunks. Throws
// std::runtime_error when the file cannot be opened or read.
[[nodiscard]] std::string sha256HexOfFile(const std::filesystem::path& file);

// Lowercase hex form of a raw SHA-256 digest.
[[nodiscard]] std::string sha256Hex(const std::array<std::uint8_t, 32>& digest);

} // namespace infraforge::runtime
