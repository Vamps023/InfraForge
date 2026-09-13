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
    const double scaled = std::floor(value / config_.chunkSize);
    // The negated comparison also rejects NaN, and infinity on either side.
    if (!(scaled >= -maxChunkIndex && scaled <= maxChunkIndex)) {
        throw WorldPartitionError{
            WorldPartitionErrorCode::CoordinateOutOfRange,
            "coordinate " + std::to_string(value)
            + " cannot be mapped to an exactly representable chunk index"};
    }
    // scaled is an integer with |scaled| <= 2^53, so the cast is exact.
    return static_cast<std::int64_t>(scaled);
}

} // namespace infraforge::domain::world
