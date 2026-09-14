#include "infraforge/domain/terrain/TerrainDownloadProvider.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace infraforge::domain::terrain {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Earth radius in metres (WGS84).
constexpr double kEarthRadiusMetres = 6378137.0;
// WebMercator latitude clamp to avoid singularity (BLOCKER 3/15).
// Web Mercator is only valid within ±85.05112878°; accepting ±90° would
// produce non-finite values. The selection grid uses this clamp for
// deterministic tile computation; canonical transforms use PROJ.
constexpr double kMaxLat = 85.05112878;

// Convert latitude in degrees to metres using the WebMercator projection.
// This is an approximation suitable for tile grid computation, not for
// canonical CRS transforms (which use PROJ).
//
// BLOCKER 16 note: The selection grid uses WebMercator for deterministic
// tile size approximation. At high latitudes (>60°), the physical scale
// differs significantly — a "4 km tile" at 70° latitude is approximately
// 2 km in the east-west direction. This is acceptable for the UI selection
// grid because:
//   1. The grid is a project-area unit, not canonical terrain geometry
//   2. The canonical terrain data uses real CRS via GDAL/PROJ
//   3. The provider requests use the tile bounds in WGS84, not the
//      WebMercator approximation
// The canonical transformation always uses GeoTransformService (PROJ).
[[nodiscard]] double latToMeters(double latDeg) {
    const double lat = std::clamp(latDeg, -kMaxLat, kMaxLat);
    const double rad = lat * kPi / 180.0;
    return std::log(std::tan(kPi / 4.0 + rad / 2.0)) * kEarthRadiusMetres;
}

[[nodiscard]] double lonToMeters(double lonDeg) {
    return lonDeg * kPi / 180.0 * kEarthRadiusMetres;
}

[[nodiscard]] double metersToLon(double meters) {
    return meters / (kPi / 180.0 * kEarthRadiusMetres);
}

[[nodiscard]] double metersToLat(double meters) {
    const double rad = 2.0 * std::atan(std::exp(meters / kEarthRadiusMetres)) - kPi / 2.0;
    return rad * 180.0 / kPi;
}

} // namespace

std::vector<SelectionTile> computeSelectionGrid(
    const GeoBounds& area, std::uint32_t tileSizeMetres) {
    if (area.isEmpty()) {
        return {};
    }
    // Validate tile size.
    switch (tileSizeMetres) {
    case 1000: case 2000: case 4000: case 8000: case 16000: break;
    default:
        throw std::invalid_argument("tile size must be 1000, 2000, 4000, 8000, or 16000");
    }

    // BLOCKER 15: Reject invalid latitude for Web Mercator. Accepting ±90°
    // would produce non-finite log(tan(...)) values.
    if (area.south < -kMaxLat || area.north > kMaxLat) {
        throw std::invalid_argument(
            "latitude must be within Web Mercator valid range [-85.05, 85.05]");
    }

    // Project the area to WebMercator metres for deterministic grid computation.
    const double minXM = lonToMeters(area.west);
    const double maxXM = lonToMeters(area.east);
    const double minYM = latToMeters(area.south);
    const double maxYM = latToMeters(area.north);

    const double widthM = maxXM - minXM;
    const double heightM = maxYM - minYM;
    if (widthM <= 0.0 || heightM <= 0.0 || !std::isfinite(widthM) || !std::isfinite(heightM)) {
        return {};
    }

    const double tileM = static_cast<double>(tileSizeMetres);
    // BLOCKER 2: Use checked arithmetic for rows/cols to prevent overflow.
    const double colsD = std::ceil(widthM / tileM);
    const double rowsD = std::ceil(heightM / tileM);
    if (!std::isfinite(colsD) || !std::isfinite(rowsD) || colsD < 0.0 || rowsD < 0.0) {
        throw std::invalid_argument("selection grid dimensions are not finite");
    }
    if (colsD > static_cast<double>(kMaxTerrainSelectionTiles) ||
        rowsD > static_cast<double>(kMaxTerrainSelectionTiles)) {
        throw std::invalid_argument(
            "selection grid dimensions exceed the maximum of "
            + std::to_string(kMaxTerrainSelectionTiles) + " tiles per axis");
    }

    const std::int64_t cols64 = static_cast<std::int64_t>(colsD);
    const std::int64_t rows64 = static_cast<std::int64_t>(rowsD);
    if (cols64 <= 0 || rows64 <= 0) {
        return {};
    }

    // BLOCKER 2: Checked multiplication for total tile count.
    if (cols64 > static_cast<std::int64_t>(kMaxTerrainSelectionTiles) ||
        rows64 > static_cast<std::int64_t>(kMaxTerrainSelectionTiles)) {
        throw std::invalid_argument(
            "selection grid dimensions exceed the maximum of "
            + std::to_string(kMaxTerrainSelectionTiles) + " tiles per axis");
    }
    // Total count: check overflow before computing.
    if (cols64 > static_cast<std::int64_t>(kMaxTerrainSelectionTiles) / rows64 + 1) {
        throw std::invalid_argument(
            "terrain selection contains too many tiles; maximum supported selection is "
            + std::to_string(kMaxTerrainSelectionTiles) + " tiles");
    }
    const std::int64_t totalCount = cols64 * rows64;
    if (totalCount > static_cast<std::int64_t>(kMaxTerrainSelectionTiles)) {
        throw std::invalid_argument(
            "terrain selection contains " + std::to_string(totalCount)
            + " tiles; maximum supported selection is "
            + std::to_string(kMaxTerrainSelectionTiles) + " tiles");
    }

    std::vector<SelectionTile> tiles;
    tiles.reserve(static_cast<std::size_t>(totalCount));
    for (std::int64_t row = 0; row < rows64; ++row) {
        for (std::int64_t col = 0; col < cols64; ++col) {
            SelectionTile tile;
            tile.col = static_cast<std::int32_t>(col);
            tile.row = static_cast<std::int32_t>(row);
            // Tile bounds in WebMercator metres.
            const double tileMinXM = minXM + static_cast<double>(col) * tileM;
            const double tileMaxXM = std::min(minXM + static_cast<double>(col + 1) * tileM, maxXM);
            const double tileMinYM = minYM + static_cast<double>(row) * tileM;
            const double tileMaxYM = std::min(minYM + static_cast<double>(row + 1) * tileM, maxYM);
            // Convert back to WGS84 degrees.
            tile.bounds.west = metersToLon(tileMinXM);
            tile.bounds.east = metersToLon(tileMaxXM);
            tile.bounds.south = metersToLat(tileMinYM);
            tile.bounds.north = metersToLat(tileMaxYM);
            // Approximate area in square metres (tile size, clipped to area).
            const double tileWidthM = tileMaxXM - tileMinXM;
            const double tileHeightM = tileMaxYM - tileMinYM;
            tile.areaSqm = tileWidthM * tileHeightM;
            tiles.push_back(tile);
        }
    }
    return tiles;
}

void TerrainProviderRegistry::registerProvider(std::unique_ptr<TerrainDownloadProvider> provider) {
    providers_.push_back(std::move(provider));
}

std::vector<ProviderInfo> TerrainProviderRegistry::listProviders() const {
    std::vector<ProviderInfo> result;
    result.reserve(providers_.size());
    for (const auto& p : providers_) {
        result.push_back(p->info());
    }
    return result;
}

const TerrainDownloadProvider* TerrainProviderRegistry::find(const std::string& providerId) const {
    for (const auto& p : providers_) {
        if (p->info().providerId == providerId) {
            return p.get();
        }
    }
    return nullptr;
}

} // namespace infraforge::domain::terrain
