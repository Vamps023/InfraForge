#pragma once

#include <filesystem>
#include <cmath>
#include <mutex>
#include <string>
#include <vector>

#include <gdal_priv.h>
#include <ogr_spatialref.h>

#ifdef _WIN32
#include <cstdlib>
#endif

namespace infraforge::testhelpers {
namespace {

// GDAL resolves CRS definitions through its own PROJ context, which needs
// the proj.db location; point it at the same share directory the engine's
// GeoTransformService uses (compile-time vcpkg layout).
void ensureProjDataForGdal() {
    static std::once_flag once;
    std::call_once(once, [] {
#ifdef INFRAFORGE_PROJ_DATA_DIR
#ifdef _WIN32
        (void)_putenv_s("PROJ_DATA", INFRAFORGE_PROJ_DATA_DIR);
        (void)_putenv_s("PROJ_LIB", INFRAFORGE_PROJ_DATA_DIR);
#else
        (void)setenv("PROJ_DATA", INFRAFORGE_PROJ_DATA_DIR, 1);
        (void)setenv("PROJ_LIB", INFRAFORGE_PROJ_DATA_DIR, 1);
#endif
#endif
    });
}

} // namespace

// Deterministic GeoTIFF DEM fixtures written with the real GDAL driver
// (docs: prefer programmatically generated fixtures with the actual raster
// library). One definition point for the fixture geometry:
//
//   CRS        EPSG:32633 (UTM 33N, metre)
//   origin     (500000, 4650000 + height * 10) — top-left pixel corner
//   size       32 x 32 pixels, 10 m cells  -> 320 m x 320 m coverage
//   heights    z(row, col) = 100 + 0.5 * col + 0.25 * row   (metres)
//   NoData     -9999 at cell (row 5, col 5) when withNodata
//
// The project fixture used by the terrain tests anchors at (500000,
// 4650000) in the same CRS, so the source->project transform is the
// identity and expected canonical values equal the source values.
struct TerrainDemSpec {
    int width{32};
    int height{32};
    double originX{500000.0};
    double originY{4650320.0}; // top-left corner
    double cellSize{10.0};
    int nodataRow{5};
    int nodataCol{5};
    bool withNodata{true};
    std::string crs{"EPSG:32633"};
    std::string elevationUnit{"metre"};
    double sampleScale{1.0};
    double sampleOffset{0.0};
    bool reliefSurface{false};
};

[[nodiscard]] inline double demHeightAt(int row, int col) {
    // Definition point of the fixture elevation surface (metres).
    return 100.0 + 0.5 * col + 0.25 * row;
}

[[nodiscard]] inline double reliefHeightAt(const int row, const int col, const TerrainDemSpec& spec) {
    const double x = (static_cast<double>(col) / static_cast<double>(spec.width - 1)) * 2.0 - 1.0;
    const double y = (static_cast<double>(row) / static_cast<double>(spec.height - 1)) * 2.0 - 1.0;
    const double hill = 220.0 * std::exp(-7.0 * ((x + 0.28) * (x + 0.28) + (y + 0.12) * (y + 0.12)));
    const double valley = 90.0 * std::exp(-10.0 * ((x - 0.38) * (x - 0.38) + (y - 0.22) * (y - 0.22)));
    const double ridge = 55.0 * std::exp(-18.0 * (y + 0.42) * (y + 0.42));
    return 500.0 + hill - valley + ridge;
}

[[nodiscard]] inline TerrainDemSpec knownGoodReliefDemSpec() {
    TerrainDemSpec spec;
    spec.width = 129;
    spec.height = 129;
    spec.originY = 4651290.0;
    spec.withNodata = false;
    spec.reliefSurface = true;
    return spec;
}

inline void writeDemGeoTiff(const std::filesystem::path& file, const TerrainDemSpec& spec) {
    ensureProjDataForGdal();
    GDALAllRegister();
    GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (driver == nullptr) {
        throw std::runtime_error("GDAL GTiff driver is unavailable in the test environment");
    }
    GDALDataset* dataset = driver->Create(file.string().c_str(), spec.width, spec.height, 1, GDT_Float32, nullptr);
    if (dataset == nullptr) {
        throw std::runtime_error("cannot create test DEM: " + file.string());
    }
    double geotransform[6] = {
        spec.originX, spec.cellSize, 0.0,
        spec.originY, 0.0, -spec.cellSize};
    dataset->SetGeoTransform(geotransform);

    OGRSpatialReference srs;
    srs.SetFromUserInput(spec.crs.c_str());
    char* wkt = nullptr;
    srs.exportToWkt(&wkt);
    dataset->SetProjection(wkt);
    CPLFree(wkt);

    GDALRasterBand* band = dataset->GetRasterBand(1);
    // The fixture is a trustworthy physical DEM, so declare its vertical
    // sample unit explicitly. Tests for missing-unit behavior use dedicated
    // fixtures and must not depend on an implicit convention.
    if (!spec.elevationUnit.empty()) band->SetUnitType(spec.elevationUnit.c_str());
    if (spec.sampleScale != 1.0) band->SetScale(spec.sampleScale);
    if (spec.sampleOffset != 0.0) band->SetOffset(spec.sampleOffset);
    if (spec.withNodata) {
        band->SetNoDataValue(-9999.0);
    }
    std::vector<float> row(static_cast<std::size_t>(spec.width), 0.0F);
    for (int r = 0; r < spec.height; ++r) {
        for (int c = 0; c < spec.width; ++c) {
            const bool nodata = spec.withNodata && r == spec.nodataRow && c == spec.nodataCol;
            row[static_cast<std::size_t>(c)] = nodata ? -9999.0F
                : static_cast<float>(spec.reliefSurface ? reliefHeightAt(r, c, spec) : demHeightAt(r, c));
        }
        const CPLErr status = band->RasterIO(
            GF_Write, 0, r, spec.width, 1, row.data(), spec.width, 1, GDT_Float32, 0, 0, nullptr);
        if (status != CE_None) {
            GDALClose(dataset);
            throw std::runtime_error("cannot write test DEM row: " + file.string());
        }
    }
    GDALClose(dataset);
}

} // namespace infraforge::testhelpers
