#pragma once

#include "infraforge/domain/geo/GeoTypes.hpp"

#include <cmath>
#include <limits>

namespace infraforge::domain::world {

// Canonical horizontal spatial bounds in project-global space (ADR-0007
// coordinate model): an axis-aligned easting/northing rectangle with
// double-precision closed edges [min, max].
//
// The logical chunk grid partitions the horizontal plane, so bounds carry
// the horizontal extent only; vertical extent is deliberately not part of
// bounds or chunk identity. No renderer-local coordinates, scale factors,
// or float reduction ever enter this type.
//
// Boundary semantics (deterministic, tested):
// - Edges are closed: contains() is min <= v <= max, and two rectangles
//   touching exactly at an edge do intersect.
// - Empty bounds (min > max on either axis) contain nothing, intersect
//   nothing, and vanish from unions/expansions until expanded.
// - Default construction is empty bounds.
struct SpatialBounds {
    double minEasting{std::numeric_limits<double>::infinity()};
    double minNorthing{std::numeric_limits<double>::infinity()};
    double maxEasting{-std::numeric_limits<double>::infinity()};
    double maxNorthing{-std::numeric_limits<double>::infinity()};

    [[nodiscard]] static constexpr SpatialBounds empty() noexcept { return SpatialBounds{}; }

    [[nodiscard]] static constexpr SpatialBounds ofPoint(
        const double easting, const double northing) noexcept {
        return SpatialBounds{easting, northing, easting, northing};
    }

    [[nodiscard]] static constexpr SpatialBounds ofPoint(
        const geo::ProjectGlobalPosition& position) noexcept {
        // Horizontal components only; height does not participate in the
        // horizontal partition.
        return ofPoint(position.easting, position.northing);
    }

    [[nodiscard]] static constexpr SpatialBounds ofEdges(
        const double minEasting, const double minNorthing,
        const double maxEasting, const double maxNorthing) noexcept {
        return SpatialBounds{minEasting, minNorthing, maxEasting, maxNorthing};
    }

    [[nodiscard]] constexpr bool isEmpty() const noexcept {
        return minEasting > maxEasting || minNorthing > maxNorthing;
    }

    // Empty bounds are well-formed by definition; non-empty bounds must
    // carry finite edges.
    [[nodiscard]] bool isFinite() const noexcept {
        return isEmpty() || (std::isfinite(minEasting) && std::isfinite(minNorthing)
            && std::isfinite(maxEasting) && std::isfinite(maxNorthing));
    }

    // Grows the bounds to include a position; empty bounds become the
    // point's degenerate rectangle.
    constexpr void expandTo(const double easting, const double northing) noexcept {
        minEasting = minOf(minEasting, easting);
        minNorthing = minOf(minNorthing, northing);
        maxEasting = maxOf(maxEasting, easting);
        maxNorthing = maxOf(maxNorthing, northing);
    }

    constexpr void expandTo(const geo::ProjectGlobalPosition& position) noexcept {
        expandTo(position.easting, position.northing);
    }

    constexpr void uniteWith(const SpatialBounds other) noexcept {
        if (other.isEmpty()) {
            return;
        }
        if (isEmpty()) {
            *this = other;
            return;
        }
        minEasting = minOf(minEasting, other.minEasting);
        minNorthing = minOf(minNorthing, other.minNorthing);
        maxEasting = maxOf(maxEasting, other.maxEasting);
        maxNorthing = maxOf(maxNorthing, other.maxNorthing);
    }

    [[nodiscard]] constexpr SpatialBounds unitedWith(const SpatialBounds other) const noexcept {
        SpatialBounds united{*this};
        united.uniteWith(other);
        return united;
    }

    // Closed-interval intersection: an empty result means the bounds are
    // disjoint (or one side was empty).
    [[nodiscard]] constexpr SpatialBounds intersectedWith(const SpatialBounds other) const noexcept {
        if (isEmpty() || other.isEmpty()) {
            return empty();
        }
        const SpatialBounds intersection{
            maxOf(minEasting, other.minEasting),
            maxOf(minNorthing, other.minNorthing),
            minOf(maxEasting, other.maxEasting),
            minOf(maxNorthing, other.maxNorthing)};
        if (intersection.isEmpty()) {
            return empty();
        }
        return intersection;
    }

    [[nodiscard]] constexpr bool intersects(const SpatialBounds other) const noexcept {
        return !intersectedWith(other).isEmpty();
    }

    [[nodiscard]] constexpr bool contains(const double easting, const double northing) const noexcept {
        return !isEmpty() && easting >= minEasting && easting <= maxEasting
            && northing >= minNorthing && northing <= maxNorthing;
    }

    [[nodiscard]] constexpr bool contains(const geo::ProjectGlobalPosition& position) const noexcept {
        return contains(position.easting, position.northing);
    }

    [[nodiscard]] constexpr bool contains(const SpatialBounds other) const noexcept {
        if (other.isEmpty()) {
            return true;
        }
        if (isEmpty()) {
            return false;
        }
        return other.minEasting >= minEasting && other.maxEasting <= maxEasting
            && other.minNorthing >= minNorthing && other.maxNorthing <= maxNorthing;
    }

    friend bool operator==(const SpatialBounds&, const SpatialBounds&) = default;

private:
    static constexpr double minOf(const double a, const double b) noexcept {
        return a < b ? a : b;
    }

    static constexpr double maxOf(const double a, const double b) noexcept {
        return a > b ? a : b;
    }
};

} // namespace infraforge::domain::world
