#include "infraforge/domain/world/ChunkGrid.hpp"

#include "infraforge/domain/world/WorldPartitionError.hpp"

#include <cmath>
#include <limits>
#include <string>

namespace infraforge::domain::world {

namespace {

WorldPartitionError invalidChunkSizeError(const double chunkSize) {
    return WorldPartitionError{
        WorldPartitionErrorCode::InvalidChunkSize,
        "chunk size must be finite and positive, got " + std::to_string(chunkSize)};
}

} // namespace

ChunkGrid ChunkGrid::fromMetreEdge(
    const geo::ResolvedUnit& linearUnit, const double edgeMetres) {
    // metres-per-unit must be usable as a divisor; the Geo domain resolves
    // it, but the partition refuses to compute on a broken factor.
    if (linearUnit.toMetre <= 0.0 || !std::isfinite(linearUnit.toMetre)) {
        throw WorldPartitionError{
            WorldPartitionErrorCode::InvalidLinearUnit,
            "canonical linear unit factor must be finite and positive, got "
                + std::to_string(linearUnit.toMetre)};
    }
    // Physical edge -> project-unit edge: divide by the metres carried by
    // one canonical unit.
    return ChunkGrid{ChunkGridConfig{edgeMetres / linearUnit.toMetre}};
}

ChunkGrid::ChunkGrid(ChunkGridConfig config) {
    if (config.chunkSize <= 0.0 || !std::isfinite(config.chunkSize)) {
        throw invalidChunkSizeError(config.chunkSize);
    }
    config_ = config;
}

ChunkCoord ChunkGrid::chunkAt(const double easting, const double northing) const {
    return ChunkCoord{axisIndex(easting), axisIndex(northing)};
}

ChunkCoord ChunkGrid::chunkAt(const geo::ProjectGlobalPosition& position) const {
    // Horizontal components only: the partition is a horizontal grid.
    return chunkAt(position.easting, position.northing);
}

std::vector<ChunkCoord> ChunkGrid::chunksIntersecting(const SpatialBounds bounds) const {
    if (bounds.isEmpty()) {
        return {};
    }
    if (!bounds.isFinite()) {
        throw WorldPartitionError{
            WorldPartitionErrorCode::InvalidBounds,
            "cannot enumerate chunks of non-finite bounds"};
    }

    const auto minX = axisIndex(bounds.minEasting);
    const auto maxX = axisIndex(bounds.maxEasting);
    const auto minY = axisIndex(bounds.minNorthing);
    const auto maxY = axisIndex(bounds.maxNorthing);

    const auto cellsX = static_cast<double>(maxX) - static_cast<double>(minX) + 1.0;
    const auto cellsY = static_cast<double>(maxY) - static_cast<double>(minY) + 1.0;
    if (cellsX * cellsY > static_cast<double>(maxEnumeratedChunks)) {
        throw WorldPartitionError{
            WorldPartitionErrorCode::CoordinateOutOfRange,
            "bounds span more chunk cells than can be enumerated"};
    }

    std::vector<ChunkCoord> chunks;
    const auto countX = static_cast<std::size_t>(cellsX);
    const auto countY = static_cast<std::size_t>(cellsY);
    chunks.reserve(countX * countY);
    for (std::int64_t x = minX; x <= maxX; ++x) {
        for (std::int64_t y = minY; y <= maxY; ++y) {
            chunks.push_back(ChunkCoord{x, y});
        }
    }
    return chunks;
}

ChunkGrid::AxisCellBounds ChunkGrid::checkedAxisCellBounds(const std::int64_t index) const {
    // Integer ceiling first: |index| <= 2^53 - 1, so forming index + 1
    // below cannot overflow and converts to double exactly.
    if (!(std::abs(static_cast<double>(index)) <= maxExactChunkIndex)) {
        throw WorldPartitionError{
            WorldPartitionErrorCode::CoordinateOutOfRange,
            "chunk coordinate " + std::to_string(index)
                + " is outside the exactly representable chunk-index range"};
    }

    const auto lower = static_cast<double>(index) * config_.chunkSize;
    const auto upper = static_cast<double>(index + 1) * config_.chunkSize;

    // A cell is usable only when its configured-grid boundaries survive
    // reconstruction as a finite, strictly increasing interval. Near the
    // ceiling the products' floating-point spacing reaches the cell edge
    // for many valid chunk sizes and the two boundaries collapse onto the
    // same double; such cells are rejected instead of silently returning a
    // zero-width footprint.
    if (!std::isfinite(lower) || !std::isfinite(upper) || !(upper > lower)) {
        throw WorldPartitionError{
            WorldPartitionErrorCode::CoordinateOutOfRange,
            "chunk cell " + std::to_string(index)
                + " has no representable footprint for the configured chunk size "
                + std::to_string(config_.chunkSize)};
    }
    return AxisCellBounds{lower, upper};
}

SpatialBounds ChunkGrid::chunkBounds(const ChunkCoord chunk) const {
    const auto x = checkedAxisCellBounds(chunk.x);
    const auto y = checkedAxisCellBounds(chunk.y);
    // Closed edges so that chunkBounds(cell).contains(...) matches chunkAt
    // boundary semantics for the shared edge position. Finiteness and
    // positive width are guaranteed by the checked boundaries.
    return SpatialBounds::ofEdges(x.lower, y.lower, x.upper, y.upper);
}

std::int64_t ChunkGrid::axisIndex(const double value) const {
    // Naive floor division is not boundary-exact for non-binary chunk
    // sizes: a coordinate constructed as k * chunkSize can re-divide to
    // k +/- one quotient ULP (e.g. -11.000000000000002), sending floor to
    // the wrong cell. Classify against the grid's own reconstructed
    // boundary products with exact double comparisons instead — no
    // epsilon, so a value one representable step away from a boundary
    // keeps its true side. Within the supported range the naive quotient
    // is at most one cell off, so a single correction is sufficient.
    double cell = std::floor(value / config_.chunkSize);

    if (value < cell * config_.chunkSize) {
        cell -= 1.0;
    } else if (value >= (cell + 1.0) * config_.chunkSize) {
        cell += 1.0;
    }

    // The negated comparison also rejects NaN, and infinity on either
    // side, before the double-to-int64 conversion.
    if (!(cell >= -maxExactChunkIndex && cell <= maxExactChunkIndex)) {
        throw WorldPartitionError{
            WorldPartitionErrorCode::CoordinateOutOfRange,
            "coordinate " + std::to_string(value)
                + " cannot be mapped to an exactly representable chunk index"};
    }
    // The candidate cell itself must have a representable footprint:
    // mapping a coordinate into a cell whose configured boundaries
    // collapsed fails loudly instead of returning an unusable index. No
    // scanning for a neighbouring cell, no clamping, no wrapping.
    static_cast<void>(checkedAxisCellBounds(static_cast<std::int64_t>(cell)));
    return static_cast<std::int64_t>(cell);
}

} // namespace infraforge::domain::world
