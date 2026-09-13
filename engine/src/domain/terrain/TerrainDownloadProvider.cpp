#include "infraforge/domain/terrain/TerrainDownloadProvider.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace infraforge::domain::terrain {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Earth radius in metres (WGS84).
constexpr double kEarthRadiusMetres = 6378137.0;
// WebMercator latitude clamp to avoid singularity.
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

    // Project the area to WebMercator metres for deterministic grid computation.
    const double minXM = lonToMeters(area.west);
    const double maxXM = lonToMeters(area.east);
    const double minYM = latToMeters(area.south);
    const double maxYM = latToMeters(area.north);

    const double widthM = maxXM - minXM;
    const double heightM = maxYM - minYM;
    if (widthM <= 0.0 || heightM <= 0.0) {
        return {};
    }

    const double tileM = static_cast<double>(tileSizeMetres);
    const std::int32_t cols = static_cast<std::int32_t>(std::ceil(widthM / tileM));
    const std::int32_t rows = static_cast<std::int32_t>(std::ceil(heightM / tileM));
    if (cols <= 0 || rows <= 0) {
        return {};
    }

    std::vector<SelectionTile> tiles;
    tiles.reserve(static_cast<std::size_t>(cols) * static_cast<std::size_t>(rows));
    for (std::int32_t row = 0; row < rows; ++row) {
        for (std::int32_t col = 0; col < cols; ++col) {
            SelectionTile tile;
            tile.col = col;
            tile.row = row;
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
