#include "infraforge/domain/terrain/EsriImageryProvider.hpp"

#include <gdal.h>
#include <gdal_priv.h>
#include <ogr_spatialref.h>

#include <algorithm>
#include <cmath>
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

} // namespace

EsriImageryProvider::EsriImageryProvider(std::shared_ptr<ports::HttpClient> httpClient)
    : httpClient_(std::move(httpClient)) {
    info_.providerId = "esri-world-imagery";
    info_.displayName = "Esri World Imagery (Satellite)";
    info_.attribution = "© Esri, Maxar, Earthstar Geographics, and the GIS User Community";
    info_.requiresAuth = false;
    info_.maxResolutionMpp = 0.5;
}

const ProviderInfo& EsriImageryProvider::info() const noexcept {
    return info_;
}

std::filesystem::path EsriImageryProvider::fetchImageryForBounds(
    const GeoBounds& bounds,
    const std::filesystem::path& tempDir,
    const CancellationCallback& cancel) const {
    if (cancel && cancel()) {
        throw ProviderCancelled();
    }

    // Determine suitable zoom level (z = 13 provides good balance for typical working areas)
    const int z = 13;
    const auto sw = toWebMercator(bounds.west, bounds.south);
    const auto ne = toWebMercator(bounds.east, bounds.north);
    const double tileSize = (2.0 * kOriginShift) / static_cast<double>(1 << z);

    const int minTileX = std::clamp(static_cast<int>(std::floor((sw.x + kOriginShift) / tileSize)), 0, (1 << z) - 1);
    const int maxTileX = std::clamp(static_cast<int>(std::floor((ne.x + kOriginShift) / tileSize)), 0, (1 << z) - 1);
    const int minTileY = std::clamp(static_cast<int>(std::floor((kOriginShift - ne.y) / tileSize)), 0, (1 << z) - 1);
    const int maxTileY = std::clamp(static_cast<int>(std::floor((kOriginShift - sw.y) / tileSize)), 0, (1 << z) - 1);

    const int tilesAcross = maxTileX - minTileX + 1;
    const int tilesDown = maxTileY - minTileY + 1;
    const int tilePixels = 256;
    const int outWidth = tilesAcross * tilePixels;
    const int outHeight = tilesDown * tilePixels;

    GDALAllRegister();
    GDALDriver* memDriver = GetGDALDriverManager()->GetDriverByName("MEM");
    if (!memDriver) {
        throw ProviderError(ProviderErrorCode::CorruptTerrainResponse, "GDAL MEM driver unavailable");
    }

    GDALDataset* memDs = memDriver->Create("", outWidth, outHeight, 3, GDT_Byte, nullptr);
    if (!memDs) {
        throw ProviderError(ProviderErrorCode::CorruptTerrainResponse, "Failed to create in-memory composite imagery");
    }

    for (int ty = minTileY; ty <= maxTileY; ++ty) {
        for (int tx = minTileX; tx <= maxTileX; ++tx) {
            if (cancel && cancel()) {
                GDALClose(memDs);
                throw ProviderCancelled();
            }

            std::string url = "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/"
                + std::to_string(z) + "/" + std::to_string(ty) + "/" + std::to_string(tx);

            try {
                const auto res = httpClient_->get(url, cancel);
                if (res.statusCode == 200 && !res.body.empty()) {
                    // Open in-memory tile with GDAL
                    std::string memFilename = "/vsimem/tile_" + std::to_string(tx) + "_" + std::to_string(ty) + ".jpg";
                    VSIFCloseL(VSIFileFromMemBuffer(memFilename.c_str(), reinterpret_cast<GByte*>(const_cast<char*>(res.body.data())), static_cast<vsi_l_offset>(res.body.size()), FALSE));
                    GDALDataset* tileDs = static_cast<GDALDataset*>(GDALOpen(memFilename.c_str(), GA_ReadOnly));
                    if (tileDs) {
                        const int destX = (tx - minTileX) * tilePixels;
                        const int destY = (ty - minTileY) * tilePixels;
                        for (int bandIdx = 1; bandIdx <= std::min(3, tileDs->GetRasterCount()); ++bandIdx) {
                            std::vector<GByte> bandData(tilePixels * tilePixels);
                            tileDs->GetRasterBand(bandIdx)->RasterIO(
                                GF_Read, 0, 0, tilePixels, tilePixels, bandData.data(),
                                tilePixels, tilePixels, GDT_Byte, 0, 0);
                            memDs->GetRasterBand(bandIdx)->RasterIO(
                                GF_Write, destX, destY, tilePixels, tilePixels, bandData.data(),
                                tilePixels, tilePixels, GDT_Byte, 0, 0);
                        }
                        GDALClose(tileDs);
                    }
                    VSIUnlink(memFilename.c_str());
                }
            } catch (...) {
                // Continue fetching other tiles on transient individual tile error
            }
        }
    }

    const double minX = -kOriginShift + static_cast<double>(minTileX) * tileSize;
    const double maxY = kOriginShift - static_cast<double>(minTileY) * tileSize;
    const double pixelSize = tileSize / static_cast<double>(tilePixels);
    double geoTransform[6] = {minX, pixelSize, 0.0, maxY, 0.0, -pixelSize};
    memDs->SetGeoTransform(geoTransform);

    OGRSpatialReference srs;
    srs.importFromEPSG(3857);
    memDs->SetSpatialRef(&srs);

    // Save to output GeoTIFF
    GDALDriver* tiffDriver = GetGDALDriverManager()->GetDriverByName("GTiff");
    const auto outputPath = tempDir / "satellite_imagery.tif";
    GDALDataset* outDs = tiffDriver->CreateCopy(outputPath.string().c_str(), memDs, FALSE, nullptr, nullptr, nullptr);
    if (outDs) {
        GDALClose(outDs);
    }
    GDALClose(memDs);

    return outputPath;
}

} // namespace infraforge::domain::terrain
