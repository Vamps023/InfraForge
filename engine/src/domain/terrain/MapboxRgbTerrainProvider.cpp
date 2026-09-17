#include "infraforge/domain/terrain/MapboxRgbTerrainProvider.hpp"

#include <gdal.h>
#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <png.h>

#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

namespace infraforge::domain::terrain {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthRadius = 6378137.0;
constexpr double kOriginShift = kPi * kEarthRadius;

struct WebMercatorCoord { double x; double y; };
WebMercatorCoord toWebMercator(double lonDeg, double latDeg) {
    const double lonRad = lonDeg * kPi / 180.0;
    const double latRad = latDeg * kPi / 180.0;
    return {
        kEarthRadius * lonRad,
        kEarthRadius * std::asinh(std::tan(latRad))
    };
}

struct TileBounds { double minX, minY, maxX, maxY; };
TileBounds tileBoundsWebMercator(int z, int x, int y) {
    const double tileSize = (2.0 * kOriginShift) / static_cast<double>(1 << z);
    const double minX = -kOriginShift + static_cast<double>(x) * tileSize;
    const double maxX = minX + tileSize;
    const double maxY = kOriginShift - static_cast<double>(y) * tileSize;
    const double minY = maxY - tileSize;
    return {minX, minY, maxX, maxY};
}

struct PngMemoryReader {
    const unsigned char* data;
    std::size_t size;
    std::size_t offset;
};

void pngReadFromMemory(png_structp pngPtr, png_bytep outBytes, png_size_t byteCountToRead) {
    auto* reader = static_cast<PngMemoryReader*>(png_get_io_ptr(pngPtr));
    if (reader->offset + byteCountToRead > reader->size) {
        png_error(pngPtr, "unexpected end of PNG data");
        return;
    }
    std::memcpy(outBytes, reader->data + reader->offset, byteCountToRead);
    reader->offset += byteCountToRead;
}

} // namespace

MapboxRgbTerrainProvider::MapboxRgbTerrainProvider(
    std::shared_ptr<ports::HttpClient> httpClient)
    : httpClient_(std::move(httpClient)) {
    info_.providerId = "mapbox-terrain-rgb";
    info_.displayName = "Mapbox Terrain-RGB";
    info_.attribution = "© Mapbox, © OpenStreetMap contributors";
    info_.requiresAuth = true;
    info_.maxResolutionMpp = 10.0;
}

const ProviderInfo& MapboxRgbTerrainProvider::info() const noexcept {
    return info_;
}

bool MapboxRgbTerrainProvider::supportsSelectiveRequests() const noexcept {
    return true;
}

double MapboxRgbTerrainProvider::effectiveResolutionMpp(
    const std::vector<SelectionTile>& selectedTiles) const {
    if (selectedTiles.empty()) {
        return 0.0;
    }
    return 10.0;
}

std::vector<ProviderRequest> MapboxRgbTerrainProvider::planRequests(
    const std::vector<SelectionTile>& selectedTiles) const {
    if (selectedTiles.empty()) {
        return {};
    }
    // Zoom 12 is standard for Mapbox terrain tiles
    const int z = 12;
    std::vector<ProviderRequest> requests;
    std::set<std::string> seen;

    for (const auto& tile : selectedTiles) {
        const auto sw = toWebMercator(tile.bounds.west, tile.bounds.south);
        const auto ne = toWebMercator(tile.bounds.east, tile.bounds.north);
        const double tileSize = (2.0 * kOriginShift) / static_cast<double>(1 << z);
        const int minTileX = std::clamp(static_cast<int>(std::floor((sw.x + kOriginShift) / tileSize)), 0, (1 << z) - 1);
        const int maxTileX = std::clamp(static_cast<int>(std::floor((ne.x + kOriginShift) / tileSize)), 0, (1 << z) - 1);
        const int minTileY = std::clamp(static_cast<int>(std::floor((kOriginShift - ne.y) / tileSize)), 0, (1 << z) - 1);
        const int maxTileY = std::clamp(static_cast<int>(std::floor((kOriginShift - sw.y) / tileSize)), 0, (1 << z) - 1);

        for (int ty = minTileY; ty <= maxTileY; ++ty) {
            for (int tx = minTileX; tx <= maxTileX; ++tx) {
                const std::string id = std::to_string(z) + "/" + std::to_string(tx) + "/" + std::to_string(ty);
                if (seen.insert(id).second) {
                    ProviderRequest req;
                    req.requestId = id;
                    const auto b = tileBoundsWebMercator(z, tx, ty);
                    req.bounds.west = (b.minX / kOriginShift) * 180.0;
                    req.bounds.east = (b.maxX / kOriginShift) * 180.0;
                    req.bounds.south = (2.0 * std::atan(std::exp(b.minY / kEarthRadius)) - kPi / 2.0) * 180.0 / kPi;
                    req.bounds.north = (2.0 * std::atan(std::exp(b.maxY / kEarthRadius)) - kPi / 2.0) * 180.0 / kPi;
                    req.estimatedBytes = 0;
                    requests.push_back(std::move(req));
                }
            }
        }
    }
    return requests;
}

std::filesystem::path MapboxRgbTerrainProvider::fetchRequest(
    const ProviderRequest& request,
    const std::filesystem::path& tempDir,
    const std::string& credentialHint,
    const CancellationCallback& cancel) const {
    if (cancel && cancel()) {
        throw ProviderCancelled();
    }

    std::string token = credentialHint;
    if (token.empty()) {
        throw ProviderError(ProviderErrorCode::AuthenticationFailed, "Mapbox access token is required");
    }

    std::string url = "https://api.mapbox.com/v4/mapbox.terrain-rgb/" + request.requestId + ".pngraw?access_token=" + token;
    const auto response = httpClient_->get(url, cancel);
    if (response.transportError == ports::TransportError::Cancelled) {
        throw ProviderCancelled();
    }
    if (response.statusCode == 401 || response.statusCode == 403) {
        throw ProviderError(ProviderErrorCode::AuthenticationFailed, "Invalid Mapbox access token");
    }
    if (response.statusCode == 429) {
        throw ProviderError(ProviderErrorCode::RateLimited, "Mapbox rate limit exceeded");
    }
    if (response.statusCode != 200 || response.body.empty()) {
        throw ProviderError(ProviderErrorCode::SourceUnavailable, "Mapbox returned HTTP " + std::to_string(response.statusCode));
    }

    // Decode PNG
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) {
        throw ProviderError(ProviderErrorCode::CorruptTerrainResponse, "failed to create libpng read struct");
    }
    png_infop pngInfo = png_create_info_struct(png);
    if (!pngInfo) {
        png_destroy_read_struct(&png, nullptr, nullptr);
        throw ProviderError(ProviderErrorCode::CorruptTerrainResponse, "failed to create libpng info struct");
    }

    PngMemoryReader reader{reinterpret_cast<const unsigned char*>(response.body.data()), response.body.size(), 0};
    png_set_read_fn(png, &reader, pngReadFromMemory);
    png_read_info(png, pngInfo);

    const png_uint_32 width = png_get_image_width(png, pngInfo);
    const png_uint_32 height = png_get_image_height(png, pngInfo);
    const png_byte colorType = png_get_color_type(png, pngInfo);
    const png_byte bitDepth = png_get_bit_depth(png, pngInfo);

    if (colorType == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (bitDepth < 8) png_set_packing(png);
    if (png_get_valid(png, pngInfo, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    png_read_update_info(png, pngInfo);

    const png_size_t rowBytes = png_get_rowbytes(png, pngInfo);
    std::vector<png_byte> imageData(rowBytes * height);
    std::vector<png_bytep> rowPointers(height);
    for (png_uint_32 y = 0; y < height; ++y) {
        rowPointers[y] = imageData.data() + y * rowBytes;
    }
    png_read_image(png, rowPointers.data());
    png_destroy_read_struct(&png, &pngInfo, nullptr);

    // Mapbox Terrain-RGB formula: height = -10000.0 + ((R * 256 * 256 + G * 256 + B) * 0.1)
    std::vector<float> elevations(width * height);
    const int channels = static_cast<int>(rowBytes / width);
    for (png_uint_32 y = 0; y < height; ++y) {
        for (png_uint_32 x = 0; x < width; ++x) {
            const png_byte* px = &imageData[y * rowBytes + x * channels];
            const double r = px[0];
            const double g = px[1];
            const double b = px[2];
            elevations[y * width + x] = static_cast<float>(-10000.0 + ((r * 65536.0 + g * 256.0 + b) * 0.1));
        }
    }

    // Write GeoTIFF
    GDALAllRegister();
    GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!driver) {
        throw ProviderError(ProviderErrorCode::CorruptTerrainResponse, "GDAL GTiff driver unavailable");
    }

    std::string safeId = request.requestId;
    std::replace(safeId.begin(), safeId.end(), '/', '_');
    const auto outputPath = tempDir / ("mapbox_" + safeId + ".tif");

    GDALDataset* outDs = driver->Create(outputPath.string().c_str(), static_cast<int>(width), static_cast<int>(height), 1, GDT_Float32, nullptr);
    if (!outDs) {
        throw ProviderError(ProviderErrorCode::CorruptTerrainResponse, "failed to create GDAL GeoTIFF dataset");
    }

    int zVal = 0, xVal = 0, yVal = 0;
    char slash = 0;
    std::istringstream(request.requestId) >> zVal >> slash >> xVal >> slash >> yVal;
    const auto b = tileBoundsWebMercator(zVal, xVal, yVal);
    const double pixelSizeX = (b.maxX - b.minX) / static_cast<double>(width);
    const double pixelSizeY = (b.maxY - b.minY) / static_cast<double>(height);
    double geoTransform[6] = {b.minX, pixelSizeX, 0.0, b.maxY, 0.0, -pixelSizeY};
    outDs->SetGeoTransform(geoTransform);

    OGRSpatialReference srs;
    srs.importFromEPSG(3857);
    outDs->SetSpatialRef(&srs);

    GDALRasterBand* band = outDs->GetRasterBand(1);
    const CPLErr err = band->RasterIO(GF_Write, 0, 0, static_cast<int>(width), static_cast<int>(height),
                                      elevations.data(), static_cast<int>(width), static_cast<int>(height),
                                      GDT_Float32, 0, 0);
    GDALClose(outDs);

    if (err != CE_None) {
        throw ProviderError(ProviderErrorCode::CorruptTerrainResponse, "failed to write raster data to GeoTIFF");
    }

    return outputPath;
}

} // namespace infraforge::domain::terrain
