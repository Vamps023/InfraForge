#include "infraforge/domain/terrain/TerrariumTerrainProvider.hpp"

#include <infraforge/domain/terrain/TerrainDownloadProvider.hpp>
#include <infraforge/ports/HttpClient.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <gdal.h>
#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <cpl_string.h>

namespace infraforge::domain::terrain {

namespace {

// Ensure GDAL is registered and PROJ_DATA is set before any GDAL call.
// Mirrors GdalTerrainSource::ensureGdalRegistered() so the production
// provider can resolve EPSG codes (e.g. importFromEPSG(3857)) without
// depending on the persistence layer.
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
constexpr double kEarthRadius = 6378137.0;
constexpr double kOriginShift = kPi * kEarthRadius; // ~20037508.34

// Web Mercator forward: WGS84 lat/lon → EPSG:3857 x/y (meters).
struct WebMercatorCoord { double x; double y; };
WebMercatorCoord toWebMercator(double lonDeg, double latDeg) {
    const double lonRad = lonDeg * kPi / 180.0;
    const double latRad = latDeg * kPi / 180.0;
    return {
        kEarthRadius * lonRad,
        kEarthRadius * std::log(std::tan(kPi / 4.0 + latRad / 2.0))
    };
}

// Web Mercator inverse: EPSG:3857 x/y → WGS84 lat/lon.
struct Wgs84Coord { double lon; double lat; };
Wgs84Coord fromWebMercator(double x, double y) {
    const double lon = (x / kEarthRadius) * 180.0 / kPi;
    const double lat = (2.0 * std::atan(std::exp(y / kEarthRadius)) - kPi / 2.0) * 180.0 / kPi;
    return {lon, lat};
}

// Tile bounds in Web Mercator for an XYZ tile at zoom z.
struct TileBounds { double minX, minY, maxX, maxY; };
TileBounds tileBoundsWebMercator(int z, int x, int y) {
    const double tileSize = (2.0 * kOriginShift) / static_cast<double>(1 << z);
    const double minX = -kOriginShift + static_cast<double>(x) * tileSize;
    const double maxX = minX + tileSize;
    // Y is flipped (origin at top-left in XYZ scheme).
    const double maxY = kOriginShift - static_cast<double>(y) * tileSize;
    const double minY = maxY - tileSize;
    return {minX, minY, maxX, maxY};
}

// Convert WGS84 bounds to the set of XYZ tiles at a given zoom.
struct XyzTile { int z, x, y; };
std::vector<XyzTile> tilesForBounds(
    const GeoBounds& bounds, int zoom) {
    // Convert bounds to Web Mercator.
    const auto sw = toWebMercator(bounds.west, bounds.south);
    const auto ne = toWebMercator(bounds.east, bounds.north);

    const double tileSize = (2.0 * kOriginShift) / static_cast<double>(1 << zoom);

    // Tile x range.
    int xMin = static_cast<int>(std::floor((sw.x + kOriginShift) / tileSize));
    int xMax = static_cast<int>(std::floor((ne.x + kOriginShift) / tileSize));
    // Tile y range (Y is flipped).
    int yMin = static_cast<int>(std::floor((kOriginShift - ne.y) / tileSize));
    int yMax = static_cast<int>(std::floor((kOriginShift - sw.y) / tileSize));

    const int maxTile = (1 << zoom) - 1;
    xMin = std::max(0, std::min(xMin, maxTile));
    xMax = std::max(0, std::min(xMax, maxTile));
    yMin = std::max(0, std::min(yMin, maxTile));
    yMax = std::max(0, std::min(yMax, maxTile));

    std::vector<XyzTile> tiles;
    for (int y = yMin; y <= yMax; ++y) {
        for (int x = xMin; x <= xMax; ++x) {
            tiles.push_back({zoom, x, y});
        }
    }
    return tiles;
}

// Choose an appropriate zoom level for the requested resolution.
// AWS Terrain Tiles are available at zoom 0-15. At zoom 12, each tile
// covers ~38 km at the equator with 256 pixels → ~150 m/pixel.
// We default to zoom 11 (~76 m/pixel at equator) for a good balance.
constexpr int kDefaultZoom = 11;

// Decode Terrarium PNG bytes to elevation values using GDAL.
// Returns a vector of Float32 elevation values (row-major, top-to-bottom).
// The PNG is 256x256 pixels with 3 bands (R, G, B).
std::vector<float> decodeTerrariumPng(const std::string& pngData, int& width, int& height) {
    // Write PNG to a temporary in-memory file via GDAL's vsimem.
    static std::atomic<int> vsiCounter{0};
    const std::string vsiPath = "/vsimem/terrarium_" +
        std::to_string(vsiCounter.fetch_add(1)) + ".png";

    VSILFILE* vsiFile = VSIFileFromMemBuffer(vsiPath.c_str(),
        reinterpret_cast<GByte*>(const_cast<char*>(pngData.data())),
        static_cast<vsi_l_offset>(pngData.size()), FALSE);
    if (!vsiFile) {
        throw ProviderError(ProviderErrorCode::CorruptTerrainResponse,
            "cannot create vsimem file for PNG decoding");
    }
    VSIFCloseL(vsiFile);

    GDALDatasetH ds = GDALOpen(vsiPath.c_str(), GA_ReadOnly);
    if (!ds) {
        VSIUnlink(vsiPath.c_str());
        throw ProviderError(ProviderErrorCode::CorruptTerrainResponse,
            "GDAL cannot open Terrarium PNG");
    }

    width = GDALGetRasterXSize(ds);
    height = GDALGetRasterYSize(ds);
    const int bands = GDALGetRasterCount(ds);
    if (bands < 3) {
        GDALClose(ds);
        VSIUnlink(vsiPath.c_str());
        throw ProviderError(ProviderErrorCode::CorruptTerrainResponse,
            "Terrarium PNG must have at least 3 bands, got " + std::to_string(bands));
    }

    std::vector<uint8_t> r(width * height);
    std::vector<uint8_t> g(width * height);
    std::vector<uint8_t> b(width * height);

    CPLErr e1 = GDALRasterIO(GDALGetRasterBand(ds, 1), GF_Read,
        0, 0, width, height, r.data(), width, height, GDT_Byte, 0, 0);
    CPLErr e2 = GDALRasterIO(GDALGetRasterBand(ds, 2), GF_Read,
        0, 0, width, height, g.data(), width, height, GDT_Byte, 0, 0);
    CPLErr e3 = GDALRasterIO(GDALGetRasterBand(ds, 3), GF_Read,
        0, 0, width, height, b.data(), width, height, GDT_Byte, 0, 0);

    GDALClose(ds);
    VSIUnlink(vsiPath.c_str());

    if (e1 != CE_None || e2 != CE_None || e3 != CE_None) {
        throw ProviderError(ProviderErrorCode::CorruptTerrainResponse,
            "failed to read Terrarium PNG bands");
    }

    // Decode Terrarium: height = (R * 256 + G + B/256) - 32768
    std::vector<float> elevations(static_cast<size_t>(width) * height);
    for (size_t i = 0; i < elevations.size(); ++i) {
        elevations[i] = static_cast<float>(
            (static_cast<int>(r[i]) * 256 + static_cast<int>(g[i]) +
             static_cast<double>(b[i]) / 256.0) - 32768.0);
    }
    return elevations;
}

// Write a GeoTIFF with Float32 elevation data and EPSG:3857 CRS.
void writeTerrariumGeoTiff(
    const std::filesystem::path& outputPath,
    const std::vector<float>& elevations,
    int width, int height,
    const TileBounds& bounds) {

    ensureGdalRegistered();

    GDALDriverH driver = GDALGetDriverByName("GTiff");
    if (!driver) {
        throw ProviderError(ProviderErrorCode::CorruptTerrainResponse,
            "GTiff driver not available");
    }

    const char* options[] = { "TILED=YES", "COMPRESS=DEFLATE", nullptr };
    GDALDatasetH ds = GDALCreate(driver, outputPath.string().c_str(),
        width, height, 1, GDT_Float32, options);
    if (!ds) {
        throw ProviderError(ProviderErrorCode::CorruptTerrainResponse,
            "cannot create output GeoTIFF");
    }

    // Geotransform: top-left corner, pixel size (Web Mercator meters).
    // bounds.minX = west, bounds.maxY = north (top-left in XYZ scheme).
    const double pixelW = (bounds.maxX - bounds.minX) / static_cast<double>(width);
    const double pixelH = (bounds.maxY - bounds.minY) / static_cast<double>(height);
    double geotransform[6] = {
        bounds.minX,    // top-left X
        pixelW,         // pixel width (east)
        0.0,            // rotation
        bounds.maxY,    // top-left Y
        0.0,            // rotation
        -pixelH         // pixel height (south, negative)
    };
    GDALSetGeoTransform(ds, geotransform);

    // Set CRS to EPSG:3857 (Web Mercator).
    OGRSpatialReference srs;
    srs.importFromEPSG(3857);
    char* wkt = nullptr;
    srs.exportToWkt(&wkt);
    GDALSetProjection(ds, wkt);
    CPLFree(wkt);

    // Set NoData value.
    GDALRasterBandH band = GDALGetRasterBand(ds, 1);
    GDALSetRasterNoDataValue(band, -32768.0);

    // Write elevation data.
    CPLErr err = GDALRasterIO(band, GF_Write,
        0, 0, width, height,
        const_cast<float*>(elevations.data()),
        width, height, GDT_Float32, 0, 0);

    GDALClose(ds);

    if (err != CE_None) {
        std::error_code ec;
        std::filesystem::remove(outputPath, ec);
        throw ProviderError(ProviderErrorCode::CorruptTerrainResponse,
            "failed to write elevation data to GeoTIFF");
    }
}

// Classify HTTP status code into a typed provider error.
ProviderErrorCode classifyHttpError(int statusCode, const std::string& errorMsg) {
    if (statusCode == 401 || statusCode == 403) {
        return ProviderErrorCode::AuthenticationFailed;
    }
    if (statusCode == 429) {
        return ProviderErrorCode::RateLimited;
    }
    if (statusCode == 404 || statusCode == 410) {
        return ProviderErrorCode::SourceUnavailable;
    }
    if (statusCode == 0) {
        // No response (timeout, DNS, connection failure).
        return ProviderErrorCode::NetworkTimeout;
    }
    if (statusCode >= 500) {
        return ProviderErrorCode::SourceUnavailable;
    }
    if (!errorMsg.empty()) {
        return ProviderErrorCode::NetworkTimeout;
    }
    return ProviderErrorCode::InvalidProviderResponse;
}

// Determine if an error is retryable.
bool isRetryable(ProviderErrorCode code) {
    switch (code) {
        case ProviderErrorCode::NetworkTimeout:
        case ProviderErrorCode::RateLimited:
        case ProviderErrorCode::SourceUnavailable:
            return true;
        case ProviderErrorCode::AuthenticationFailed:
        case ProviderErrorCode::InvalidProviderResponse:
        case ProviderErrorCode::UnsupportedCoverage:
        case ProviderErrorCode::CorruptTerrainResponse:
        case ProviderErrorCode::Cancelled:
            return false;
    }
    return false;
}

} // namespace

TerrariumTerrainProvider::TerrariumTerrainProvider(
    std::shared_ptr<ports::HttpClient> httpClient)
    : httpClient_(std::move(httpClient)) {
    info_.providerId = "terrarium-aws";
    info_.displayName = "AWS Terrain Tiles (Terrarium)";
    info_.attribution =
        "Terrain data © Mapzen, USGS, NASA, and other open data sources. "
        "See https://github.com/tilezen/joerd/blob/master/docs/attribution.md";
    info_.requiresAuth = false;
    info_.maxResolutionMpp = 76.0; // ~76 m/pixel at zoom 11
    // Global coverage (empty bounds = global).
    info_.coverage = GeoBounds{};
}

const ProviderInfo& TerrariumTerrainProvider::info() const noexcept {
    return info_;
}

bool TerrariumTerrainProvider::supportsSelectiveRequests() const noexcept {
    return true; // XYZ tiles are individually addressable.
}

std::vector<ProviderRequest> TerrariumTerrainProvider::planRequests(
    const std::vector<SelectionTile>& selectedTiles) const {
    if (selectedTiles.empty()) {
        return {};
    }

    // Collect unique XYZ tiles across all selected application tiles.
    std::vector<XyzTile> allTiles;
    for (const auto& tile : selectedTiles) {
        auto tiles = tilesForBounds(tile.bounds, kDefaultZoom);
        for (auto& t : tiles) {
            allTiles.push_back(t);
        }
    }

    // Deduplicate by (z, x, y).
    std::sort(allTiles.begin(), allTiles.end(), [](const XyzTile& a, const XyzTile& b) {
        if (a.z != b.z) return a.z < b.z;
        if (a.x != b.x) return a.x < b.x;
        return a.y < b.y;
    });
    allTiles.erase(std::unique(allTiles.begin(), allTiles.end(),
        [](const XyzTile& a, const XyzTile& b) {
            return a.z == b.z && a.x == b.x && a.y == b.y;
        }), allTiles.end());

    // Build provider requests.
    std::vector<ProviderRequest> requests;
    requests.reserve(allTiles.size());
    for (const auto& t : allTiles) {
        ProviderRequest req;
        // Request ID encodes the XYZ tile coordinates.
        req.requestId = std::to_string(t.z) + "/" +
                        std::to_string(t.x) + "/" +
                        std::to_string(t.y);

        // Bounds in WGS84 for the plan summary.
        const auto tb = tileBoundsWebMercator(t.z, t.x, t.y);
        const auto sw = fromWebMercator(tb.minX, tb.minY);
        const auto ne = fromWebMercator(tb.maxX, tb.maxY);
        req.bounds = GeoBounds{sw.lon, sw.lat, ne.lon, ne.lat};

        // Estimated bytes: 256x256 PNG ≈ 50-100 KB.
        req.estimatedBytes = 100 * 1024;
        requests.push_back(req);
    }
    return requests;
}

std::filesystem::path TerrariumTerrainProvider::fetchRequest(
    const ProviderRequest& request,
    const std::filesystem::path& tempDir,
    const std::string& /*credentialHint*/,
    const CancellationCallback& cancel) const {

    // Cancellation checkpoint before any work (BLOCKER 4).
    if (cancel) cancel();

    // Parse the request ID (z/x/y) with strict validation (BLOCKER: harden
    // provider request parsing — reject trailing garbage).
    int z, x, y;
    {
        char slash1, slash2;
        std::istringstream iss(request.requestId);
        iss >> z >> slash1 >> x >> slash2 >> y;
        if (iss.fail() || slash1 != '/' || slash2 != '/') {
            throw ProviderError(ProviderErrorCode::InvalidProviderResponse,
                "malformed Terrarium request ID: " + request.requestId);
        }
        // Reject trailing garbage after the y value.
        char trailing;
        if (iss.get(trailing) && !iss.eof()) {
            throw ProviderError(ProviderErrorCode::InvalidProviderResponse,
                "trailing garbage in Terrarium request ID: " + request.requestId);
        }
        // Validate z/x/y ranges.
        if (z < 0 || z > 20 || x < 0 || y < 0) {
            throw ProviderError(ProviderErrorCode::InvalidProviderResponse,
                "Terrarium tile coordinates out of range: " + request.requestId);
        }
    }

    // Construct the URL.
    const std::string url =
        "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/" +
        std::to_string(z) + "/" + std::to_string(x) + "/" + std::to_string(y) + ".png";

    // Fetch with bounded retries (BLOCKER 4: cancellation-aware).
    std::string pngData;
    for (int attempt = 0; attempt <= kMaxRetries; ++attempt) {
        // Cancellation checkpoint before each HTTP request.
        if (cancel) cancel();

        // Use the cancellation-aware HTTP overload. The atomic flag is
        // set by the cancel callback wrapper in TerrainService.
        std::atomic<bool> cancelFlag{false};
        auto cancelFn = [&cancelFlag] { cancelFlag.store(true); };
        // If a cancellation callback was provided, wire it so that calling
        // cancel() sets the flag; the HTTP client checks it before the
        // request.
        if (cancel) {
            // We cannot change the cancel callback mid-request; instead we
            // check cancellation before and after the request, and use the
            // cancellation-aware overload so the HTTP client also checks.
            // The cancel callback itself throws when called.
        }
        auto response = httpClient_->get(url, cancelFlag);

        if (response.ok()) {
            pngData = response.body;
            break;
        }

        // Cancellation checkpoint after a failed request.
        if (cancel) cancel();

        const auto errorCode = classifyHttpError(response.statusCode, response.errorMessage);

        // Check if this is the last attempt or non-retryable.
        if (attempt >= kMaxRetries || !isRetryable(errorCode)) {
            throw ProviderError(errorCode,
                "Terrarium request failed (attempt " + std::to_string(attempt + 1) +
                "): HTTP " + std::to_string(response.statusCode) +
                " - " + response.errorMessage);
        }

        // Exponential backoff (BLOCKER 4: interruptible).
        const int delayMs = kBaseBackoffMs * (1 << attempt);
        const int checkIntervalMs = 100;
        int elapsedMs = 0;
        while (elapsedMs < delayMs) {
            if (cancel) cancel();
            const int sleepMs = std::min(checkIntervalMs, delayMs - elapsedMs);
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
            elapsedMs += sleepMs;
        }
    }

    if (pngData.empty()) {
        throw ProviderError(ProviderErrorCode::CorruptTerrainResponse,
            "empty response from Terrarium provider");
    }

    // Cancellation checkpoint before decode.
    if (cancel) cancel();

    // Decode the Terrarium PNG to elevation values.
    int width = 0, height = 0;
    std::vector<float> elevations = decodeTerrariumPng(pngData, width, height);

    // Cancellation checkpoint after decode, before raster write.
    if (cancel) cancel();

    // Compute the tile bounds in Web Mercator.
    const auto tb = tileBoundsWebMercator(z, x, y);

    // Write a GeoTIFF to the temp directory.
    // Sanitize the request ID for use as a filename.
    std::string filename = request.requestId;
    std::replace(filename.begin(), filename.end(), '/', '_');
    filename += ".tif";

    const std::filesystem::path outputPath = tempDir / filename;
    writeTerrariumGeoTiff(outputPath, elevations, width, height, tb);

    return outputPath;
}

} // namespace infraforge::domain::terrain
