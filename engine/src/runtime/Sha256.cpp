#include "infraforge/runtime/Sha256.hpp"

#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>

namespace infraforge::runtime {
namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants{{
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
}};

constexpr std::uint32_t rotateRight(std::uint32_t value, unsigned bits) noexcept {
    return (value >> bits) | (value << (32 - bits));
}

} // namespace

Sha256::Sha256() noexcept {
    // FIPS 180-4 initial hash values (fractional parts of the square roots
    // of the first eight primes).
    state_ = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
}

void Sha256::compressBlock(const std::uint8_t* block) {
    std::array<std::uint32_t, 64> w{};
    for (std::size_t i = 0; i < 16; ++i) {
        w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24)
            | (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16)
            | (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8)
            | static_cast<std::uint32_t>(block[i * 4 + 3]);
    }
    for (std::size_t i = 16; i < 64; ++i) {
        const std::uint32_t s0 =
            rotateRight(w[i - 15], 7) ^ rotateRight(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const std::uint32_t s1 =
            rotateRight(w[i - 2], 17) ^ rotateRight(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];
    std::uint32_t e = state_[4];
    std::uint32_t f = state_[5];
    std::uint32_t g = state_[6];
    std::uint32_t h = state_[7];

    for (std::size_t i = 0; i < 64; ++i) {
        const std::uint32_t s1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
        const std::uint32_t ch = (e & f) ^ (~e & g);
        const std::uint32_t temp1 = h + s1 + ch + kRoundConstants[i] + w[i];
        const std::uint32_t s0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void Sha256::update(const void* data, std::size_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    totalBytes_ += size;

    if (buffered_ > 0) {
        const std::size_t take = std::min(size, buffer_.size() - buffered_);
        std::memcpy(buffer_.data() + buffered_, bytes, take);
        buffered_ += take;
        bytes += take;
        size -= take;
        if (buffered_ == buffer_.size()) {
            compressBlock(buffer_.data());
            buffered_ = 0;
        }
    }
    while (size >= buffer_.size()) {
        compressBlock(bytes);
        bytes += buffer_.size();
        size -= buffer_.size();
    }
    if (size > 0) {
        std::memcpy(buffer_.data(), bytes, size);
        buffered_ = size;
    }
}

std::array<std::uint8_t, 32> Sha256::finish() {
    const std::uint64_t bitLength = totalBytes_ * 8;

    // 0x80 padding, zeros, then the 64-bit big-endian bit length.
    const std::uint8_t padding[72]{0x80};
    const std::size_t padLength = (buffered_ < 56) ? (56 - buffered_) : (120 - buffered_);
    update(padding, padLength);
    // update() accounted the padding into totalBytes_; the length field is
    // appended manually so it does not perturb the message length.
    const std::uint8_t lengthBytes[8] = {
        static_cast<std::uint8_t>(bitLength >> 56), static_cast<std::uint8_t>(bitLength >> 48),
        static_cast<std::uint8_t>(bitLength >> 40), static_cast<std::uint8_t>(bitLength >> 32),
        static_cast<std::uint8_t>(bitLength >> 24), static_cast<std::uint8_t>(bitLength >> 16),
        static_cast<std::uint8_t>(bitLength >> 8), static_cast<std::uint8_t>(bitLength)};
    {
        const auto* bytes = lengthBytes;
        std::size_t size = 8;
        // Bypass update() length accounting: feed the block buffer directly.
        if (buffered_ + size > buffer_.size()) {
            throw std::logic_error("sha256 length padding overflow");
        }
        std::memcpy(buffer_.data() + buffered_, bytes, size);
        buffered_ += size;
        if (buffered_ == buffer_.size()) {
            compressBlock(buffer_.data());
            buffered_ = 0;
        }
    }
    if (buffered_ != 0) {
        throw std::logic_error("sha256 did not end on a block boundary");
    }

    std::array<std::uint8_t, 32> digest{};
    for (std::size_t i = 0; i < 8; ++i) {
        digest[i * 4] = static_cast<std::uint8_t>(state_[i] >> 24);
        digest[i * 4 + 1] = static_cast<std::uint8_t>(state_[i] >> 16);
        digest[i * 4 + 2] = static_cast<std::uint8_t>(state_[i] >> 8);
        digest[i * 4 + 3] = static_cast<std::uint8_t>(state_[i]);
    }
    return digest;
}

std::string sha256Hex(const std::array<std::uint8_t, 32>& digest) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(digest.size() * 2);
    for (const std::uint8_t byte : digest) {
        hex.push_back(kDigits[byte >> 4]);
        hex.push_back(kDigits[byte & 0x0f]);
    }
    return hex;
}

std::string sha256HexOfFile(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open file for hashing: " + file.string());
    }
    Sha256 hash;
    std::array<char, 64 * 1024> chunk{};
    while (input) {
        input.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        const std::streamsize read = input.gcount();
        if (read > 0) {
            hash.update(chunk.data(), static_cast<std::size_t>(read));
        }
    }
    if (input.bad()) {
        throw std::runtime_error("failed while reading file for hashing: " + file.string());
    }
    return sha256Hex(hash.finish());
}

} // namespace infraforge::runtime
