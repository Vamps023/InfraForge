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

SpatialBounds ChunkGrid::chunkBounds(const ChunkCoord chunk) const {
    // ChunkCoord is a public value type, so its full int64 range can reach
    // this call even though chunkAt never produces coordinates beyond
    // maxChunkIndex. Enforce the documented supported range explicitly:
    // beyond it the (k+1) footprint edge rounds back onto k and the cell
    // would collapse to zero width instead of failing loudly.
    const auto beyondRange = [](const std::int64_t axis) {
        return std::abs(static_cast<double>(axis)) > maxChunkIndex;
    };
    if (beyondRange(chunk.x) || beyondRange(chunk.y)) {
        throw WorldPartitionError{
            WorldPartitionErrorCode::CoordinateOutOfRange,
            "chunk coordinate (" + std::to_string(chunk.x) + ", "
                + std::to_string(chunk.y)
                + ") is outside the supported chunk-index range"};
    }

    const auto x = static_cast<double>(chunk.x);
    const auto y = static_cast<double>(chunk.y);
    const auto size = config_.chunkSize;
    const SpatialBounds footprint{x * size, y * size, (x + 1.0) * size, (y + 1.0) * size};
    if (!footprint.isFinite()) {
        throw WorldPartitionError{
            WorldPartitionErrorCode::CoordinateOutOfRange,
            "chunk footprint is not representable in finite coordinates"};
    }
    return footprint;
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

    // The negated comparison also rejects NaN, and infinity on either side.
    if (!(cell >= -maxChunkIndex && cell <= maxChunkIndex)) {
        throw WorldPartitionError{
            WorldPartitionErrorCode::CoordinateOutOfRange,
            "coordinate " + std::to_string(value)
                + " cannot be mapped to an exactly representable chunk index"};
    }
    // cell is an integer with |cell| <= 2^53 - 1, so the cast is exact.
    return static_cast<std::int64_t>(cell);
}

} // namespace infraforge::domain::world
