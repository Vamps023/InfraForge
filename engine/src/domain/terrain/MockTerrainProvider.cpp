#include "infraforge/domain/terrain/MockTerrainProvider.hpp"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <set>
#include <sstream>
#include <vector>

#include <gdal.h>
#include <gdal_priv.h>
#include <ogr_spatialref.h>

namespace infraforge::domain::terrain {

namespace {

// Ensure GDAL is registered and PROJ_DATA is set before any GDAL call.
// Mirrors GdalTerrainSource::ensureGdalRegistered() so the mock provider
// can resolve EPSG codes (e.g. importFromEPSG(3857)) without depending on
// the persistence layer.
void ensureGdalRegistered() {
    static std::once_flag registered;
    std::call_once(registered, [] {
#ifdef INFRAFORGE_PROJ_DATA_DIR
#ifdef _WIN32
        (void)_putenv_s("PROJ_DATA", INFRAFORGE_PROJ_DATA_DIR);
#else
        (void)setenv("PROJ_DATA", INFRAFORGE_PROJ_DATA_DIR, 0);
#endif
#endif
        GDALAllRegister();
    });
}

constexpr double kPi = 3.14159265358979323846;

constexpr double kWebMercatorMaxLat = 85.05112878;

constexpr double kEarthRadius = 6378137.0;

// Web Mercator forward: WGS84 lat/lon -> EPSG:3857 x/y (meters).
struct WebMercatorCoord { double x; double y; };
WebMercatorCoord toWebMercator(double lonDeg, double latDeg) {
    const double lonRad = lonDeg * kPi / 180.0;
    const double latRad = latDeg * kPi / 180.0;
    return {
        kEarthRadius * lonRad,
        kEarthRadius * std::log(std::tan(kPi / 4.0 + latRad / 2.0))
    };
}

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
    const std::string& /*credentialHint*/,
    const CancellationCallback& cancel) const {
    if (failMode_.has_value()) {
        throw ProviderError(*failMode_, "mock provider configured to fail");
    }

    // Cancellation checkpoint before decode/write (BLOCKER 4).
    if (cancel && cancel()) {
        throw ProviderError(ProviderErrorCode::Cancelled, "mock provider cancelled");
    }

    ensureGdalRegistered();

    // Write a real GeoTIFF with EPSG:3857 CRS and proper geotransform so the
    // canonical assembly pipeline (assembleCanonicalGeoTiff) can read it via
    // GDAL. This replaces the old MDEM binary placeholder (BLOCKER 2).
    constexpr int kDim = 32;

    // Sanitize the request ID into a valid filename (replace / with _).
    std::string safeId = request.requestId;
    std::replace(safeId.begin(), safeId.end(), '/', '_');
    const std::string fileName = "mock_" + safeId + ".tif";
    const std::filesystem::path filePath = tempDir / fileName;

    // Convert WGS84 bounds to Web Mercator (EPSG:3857) for the geotransform.
    const auto sw = toWebMercator(request.bounds.west, request.bounds.south);
    const auto ne = toWebMercator(request.bounds.east, request.bounds.north);
    const double minX = sw.x;
    const double maxY = ne.y;
    const double pixelW = (ne.x - sw.x) / static_cast<double>(kDim);
    const double pixelH = (ne.y - sw.y) / static_cast<double>(kDim);

    GDALDriverH driver = GDALGetDriverByName("GTiff");
    if (!driver) {
        throw ProviderError(ProviderErrorCode::SourceUnavailable,
            "GDAL GTiff driver not available for mock provider");
    }
    GDALDatasetH ds = GDALCreate(driver, filePath.string().c_str(),
        kDim, kDim, 1, GDT_Float32, nullptr);
    if (!ds) {
        throw ProviderError(ProviderErrorCode::SourceUnavailable,
            "cannot create mock terrain GeoTIFF");
    }

    double geotransform[6] = {minX, pixelW, 0.0, maxY, 0.0, -pixelH};
    GDALSetGeoTransform(ds, geotransform);

    OGRSpatialReference srs;
    srs.importFromEPSG(3857);
    char* wkt = nullptr;
    srs.exportToWkt(&wkt);
    GDALSetProjection(ds, wkt);
    CPLFree(wkt);

    GDALRasterBandH band = GDALGetRasterBand(ds, 1);
    GDALSetRasterNoDataValue(band, -9999.0);

    // Deterministic elevation: based on the center latitude.
    const double centerLat = (request.bounds.north + request.bounds.south) * 0.5;
    const double baseElevation = std::abs(centerLat) * 100.0;
    std::vector<float> rowData(kDim, 0.0F);
    for (int row = 0; row < kDim; ++row) {
        for (int col = 0; col < kDim; ++col) {
            rowData[static_cast<std::size_t>(col)] =
                static_cast<float>(baseElevation + row * 0.5 + col * 0.25);
        }
        CPLErr err = GDALRasterIO(band, GF_Write, 0, row, kDim, 1,
            rowData.data(), kDim, 1, GDT_Float32, 0, 0);
        if (err != CE_None) {
            GDALClose(ds);
            throw ProviderError(ProviderErrorCode::SourceUnavailable,
                "cannot write mock terrain GeoTIFF row");
        }
    }
    GDALClose(ds);
    return filePath;
}

} // namespace infraforge::domain::terrain
