#include "infraforge/domain/terrain/MockTerrainProvider.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>

namespace infraforge::domain::terrain {

namespace {

constexpr double kPi = 3.14159265358979323846;

constexpr double kWebMercatorMaxLat = 85.05112878;

[[nodiscard]] std::int32_t lonToTileX(double lon, std::uint32_t zoom) {
    const double n = std::pow(2.0, static_cast<double>(zoom));
    return static_cast<std::int32_t>(std::floor((lon + 180.0) / 360.0 * n));
}

[[nodiscard]] std::int32_t latToTileY(double lat, std::uint32_t zoom) {
    const double clamped = std::clamp(lat, -kWebMercatorMaxLat, kWebMercatorMaxLat);
    const double rad = clamped * kPi / 180.0;
    const double n = std::pow(2.0, static_cast<double>(zoom));
    return static_cast<std::int32_t>(std::floor((1.0 - std::log(std::tan(rad) + 1.0 / std::cos(rad)) / kPi) / 2.0 * n));
}

[[nodiscard]] double tileXToLon(std::int32_t x, std::uint32_t zoom) {
    const double n = std::pow(2.0, static_cast<double>(zoom));
    return static_cast<double>(x) / n * 360.0 - 180.0;
}

[[nodiscard]] double tileYToLat(std::int32_t y, std::uint32_t zoom) {
    const double n = std::pow(2.0, static_cast<double>(zoom));
    const double ratio = kPi - 2.0 * kPi * static_cast<double>(y) / n;
    return 180.0 / kPi * std::atan(0.5 * (std::exp(ratio) - std::exp(-ratio)));
}

} // namespace

std::pair<std::int32_t, std::int32_t> MockTerrainProvider::lonToTileXRange(
    double west, double east) const {
    const std::int32_t minX = lonToTileX(west, zoom_);
    const std::int32_t maxX = lonToTileX(east, zoom_);
    return {minX, maxX};
}

std::pair<std::int32_t, std::int32_t> MockTerrainProvider::latToTileYRange(
    double south, double north) const {
    const std::int32_t maxY = latToTileY(south, zoom_); // south = higher Y
    const std::int32_t minY = latToTileY(north, zoom_); // north = lower Y
    return {minY, maxY};
}

std::vector<ProviderRequest> MockTerrainProvider::planRequests(
    const std::vector<SelectionTile>& selectedTiles) const {
    // Collect unique XYZ tiles covering all selected application tiles.
    struct TileKey {
        std::int32_t x, y;
        bool operator==(const TileKey& o) const { return x == o.x && y == o.y; }
        bool operator<(const TileKey& o) const { return x < o.x || (x == o.x && y < o.y); }
    };
    std::set<TileKey> uniqueTiles;
    for (const SelectionTile& tile : selectedTiles) {
        auto [minX, maxX] = lonToTileXRange(tile.bounds.west, tile.bounds.east);
        auto [minY, maxY] = latToTileYRange(tile.bounds.south, tile.bounds.north);
        for (std::int32_t x = minX; x <= maxX; ++x) {
            for (std::int32_t y = minY; y <= maxY; ++y) {
                uniqueTiles.insert({x, y});
            }
        }
    }

    std::vector<ProviderRequest> requests;
    requests.reserve(uniqueTiles.size());
    for (const TileKey& key : uniqueTiles) {
        ProviderRequest req;
        std::ostringstream oss;
        oss << zoom_ << "/" << key.x << "/" << key.y;
        req.requestId = oss.str();
        req.bounds.west = tileXToLon(key.x, zoom_);
        req.bounds.east = tileXToLon(key.x + 1, zoom_);
        req.bounds.north = tileYToLat(key.y, zoom_);
        req.bounds.south = tileYToLat(key.y + 1, zoom_);
        // Estimated bytes: 256x256 float32 = 256KB per tile.
        req.estimatedBytes = 256u * 256u * 4u;
        requests.push_back(req);
    }
    return requests;
}

std::filesystem::path MockTerrainProvider::fetchRequest(
    const ProviderRequest& request,
    const std::filesystem::path& tempDir,
    const std::string& /*credentialHint*/) const {
    if (failMode_.has_value()) {
        throw ProviderError(*failMode_, "mock provider configured to fail");
    }

    // Write a simple deterministic binary raster file (not a real GeoTIFF,
    // but enough to test the workflow). Format: 4-byte magic "MDEM",
    // 4-byte width, 4-byte height, then float32 elevation values.
    // The elevation is deterministic from the request bounds.
    constexpr std::uint32_t kDim = 32;
    // Sanitize the request ID into a valid filename (replace / with _).
    std::string safeId = request.requestId;
    std::replace(safeId.begin(), safeId.end(), '/', '_');
    const std::string fileName = "mock_" + safeId + ".mdat";
    const std::filesystem::path filePath = tempDir / fileName;

    std::ofstream out(filePath, std::ios::binary);
    if (!out) {
        throw ProviderError(ProviderErrorCode::SourceUnavailable,
            "cannot create mock terrain file");
    }
    const char magic[4] = {'M', 'D', 'E', 'M'};
    out.write(magic, 4);
    const std::uint32_t width = kDim;
    const std::uint32_t height = kDim;
    out.write(reinterpret_cast<const char*>(&width), 4);
    out.write(reinterpret_cast<const char*>(&height), 4);

    // Deterministic elevation: based on the center latitude.
    const double centerLat = (request.bounds.north + request.bounds.south) * 0.5;
    const double baseElevation = std::abs(centerLat) * 100.0; // simple ramp
    for (std::uint32_t row = 0; row < kDim; ++row) {
        for (std::uint32_t col = 0; col < kDim; ++col) {
            float value = static_cast<float>(baseElevation + row * 0.5 + col * 0.25);
            out.write(reinterpret_cast<const char*>(&value), 4);
        }
    }
    out.close();
    return filePath;
}

} // namespace infraforge::domain::terrain
