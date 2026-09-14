#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace infraforge::domain::world {

// Explicit invalidation classes for incremental chunk work
// (docs/02_DATA/WORLD_CHUNKS.md, TRD "Rendering"). A closed, strongly typed
// set — never free-form strings — so invalidation routing can switch
// exhaustively and new classes are added as visible domain vocabulary.
enum class InvalidationClass : std::uint8_t {
    Geometry,
    Material,
    Topology,
    Terrain,
    Simulation,
    Road,
};

[[nodiscard]] std::string_view invalidationClassName(InvalidationClass invalidationClass) noexcept;
[[nodiscard]] std::optional<InvalidationClass> invalidationClassFromName(
    std::string_view name) noexcept;

// Number of distinct invalidation classes; iteration bound for callers that
// sweep the full class set.
[[nodiscard]] constexpr std::size_t invalidationClassCount() noexcept { return 6; }

// Bit mask over InvalidationClass values, carried through mutation results
// and per-chunk dirty state. Zero bits mean "no invalidation declared".
class InvalidationMask {
public:
    constexpr InvalidationMask() noexcept = default;

    [[nodiscard]] static constexpr InvalidationMask of(const InvalidationClass cls) noexcept {
        InvalidationMask mask;
        mask.add(cls);
        return mask;
    }

    [[nodiscard]] static constexpr InvalidationMask fromBits(const std::uint8_t bits) noexcept {
        return InvalidationMask{static_cast<std::uint8_t>(bits & kAllBits)};
    }

    constexpr void add(const InvalidationClass cls) noexcept { bits_ |= bitOf(cls); }

    [[nodiscard]] constexpr bool contains(const InvalidationClass cls) const noexcept {
        return (bits_ & bitOf(cls)) != 0;
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return bits_ == 0; }
    [[nodiscard]] constexpr std::uint8_t bits() const noexcept { return bits_; }

    constexpr InvalidationMask& operator|=(const InvalidationMask other) noexcept {
        bits_ |= other.bits_;
        return *this;
    }

    [[nodiscard]] friend constexpr InvalidationMask operator|(
        const InvalidationMask a, const InvalidationMask b) noexcept {
        return InvalidationMask{static_cast<std::uint8_t>(a.bits_ | b.bits_)};
    }

    friend bool operator==(const InvalidationMask&, const InvalidationMask&) = default;

private:
    static constexpr std::uint8_t kAllBits = 0b0011'1111;

    constexpr explicit InvalidationMask(const std::uint8_t bits) noexcept
        : bits_(bits) {}

    static constexpr std::uint8_t bitOf(const InvalidationClass cls) noexcept {
        return static_cast<std::uint8_t>(std::uint8_t{1} << static_cast<std::uint8_t>(cls));
    }

    std::uint8_t bits_{0};
};

} // namespace infraforge::domain::world
