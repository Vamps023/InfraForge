#include "infraforge/application/TerrainExportEngine.hpp"

#include "infraforge/domain/terrain/TerrainTypes.hpp"
#include "infraforge/runtime/Logging.hpp"
#include "infraforge/runtime/Timestamp.hpp"

#include <gdal.h>
#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <cpl_conv.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <png.h>

namespace infraforge::application {

namespace {

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4611)
#endif

extern "C" int writePng16Raw(FILE* fp, int width, int height, const std::uint16_t* data) {
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) return 0;
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, nullptr);
        return 0;
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        return 0;
    }
    png_init_io(png, fp);
    png_set_IHDR(png, info, width, height, 16, PNG_COLOR_TYPE_GRAY, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    png_set_swap(png); // Ensure big-endian network byte order for PNG

    auto* rows = static_cast<png_bytepp>(std::malloc(sizeof(png_bytep) * height));
    if (!rows) {
        png_destroy_write_struct(&png, &info);
        return 0;
    }
    for (int y = 0; y < height; ++y) {
        rows[y] = const_cast<png_bytep>(reinterpret_cast<const png_byte*>(&data[static_cast<std::size_t>(y) * width]));
    }
    png_write_image(png, rows);
    std::free(rows);
    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    return 1;
}

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

bool writePng16(const std::filesystem::path& filePath, int width, int height, const std::vector<std::uint16_t>& data) {
    FILE* fp = nullptr;
#ifdef _WIN32
    if (_wfopen_s(&fp, filePath.c_str(), L"wb") != 0 || !fp) return false;
#else
    fp = std::fopen(filePath.string().c_str(), "wb");
    if (!fp) return false;
#endif
    const int ok = writePng16Raw(fp, width, height, data.data());
    std::fclose(fp);
    return ok == 1;
}

} // namespace

TerrainExportOutput TerrainExportEngine::executeExport(
    const TerrainExportOptions& options,
    const ExportProgressCallback& progress,
    const ExportCancellationCallback& cancel) {
    if (cancel && cancel()) {
        throw domain::terrain::TerrainError(domain::terrain::TerrainErrorCode::InvalidArgument, "terrain export cancelled");
    }

    if (options.outputDirectory.empty()) {
        throw domain::terrain::TerrainError(domain::terrain::TerrainErrorCode::InvalidArgument, "output directory is required");
    }

    std::error_code ec;
    std::filesystem::create_directories(options.outputDirectory, ec);
    if (ec) {
        throw domain::terrain::TerrainError(domain::terrain::TerrainErrorCode::InvalidArgument, "failed to create export directory: " + ec.message());
    }

    if (progress) progress(0.05, "Opening terrain dataset...");

    GDALAllRegister();
    GDALDataset* srcDs = static_cast<GDALDataset*>(GDALOpen(options.sourceRasterPath.string().c_str(), GA_ReadOnly));
    if (!srcDs) {
        throw domain::terrain::TerrainError(domain::terrain::TerrainErrorCode::SourceUnreadable,
            "failed to open source terrain raster: " + options.sourceRasterPath.string());
    }

    struct DatasetGuard {
        GDALDataset* ds;
        ~DatasetGuard() { if (ds) GDALClose(ds); }
    } guard{srcDs};

    const int srcWidth = srcDs->GetRasterXSize();
    const int srcHeight = srcDs->GetRasterYSize();
    const int outWidth = options.targetResolution > 0 ? static_cast<int>(options.targetResolution) : srcWidth;
    const int outHeight = options.targetResolution > 0 ? static_cast<int>(options.targetResolution) : srcHeight;

    double geoTransform[6]{0.0, 1.0, 0.0, 0.0, 0.0, -1.0};
    srcDs->GetGeoTransform(geoTransform);
    if (options.targetResolution > 0) {
        geoTransform[1] = geoTransform[1] * (static_cast<double>(srcWidth) / static_cast<double>(outWidth));
        geoTransform[5] = geoTransform[5] * (static_cast<double>(srcHeight) / static_cast<double>(outHeight));
    }

    const OGRSpatialReference* spatialRef = srcDs->GetSpatialRef();

    if (progress) progress(0.20, "Reading raster elevations...");

    std::vector<double> elevations(static_cast<std::size_t>(outWidth) * static_cast<std::size_t>(outHeight));
    GDALRasterBand* srcBand = srcDs->GetRasterBand(1);
    if (!srcBand) {
        throw domain::terrain::TerrainError(domain::terrain::TerrainErrorCode::CorruptSource, "source raster missing band 1");
    }

    const CPLErr readErr = srcBand->RasterIO(
        GF_Read, 0, 0, srcWidth, srcHeight, elevations.data(),
        outWidth, outHeight, GDT_Float64, 0, 0, nullptr);
    if (readErr != CE_None) {
        throw domain::terrain::TerrainError(domain::terrain::TerrainErrorCode::CorruptSource, "failed to read raster data");
    }

    int hasNodata = FALSE;
    const double nodataVal = srcBand->GetNoDataValue(&hasNodata);

    double minZ = options.minElevation;
    double maxZ = options.maxElevation;
    if (maxZ <= minZ) {
        minZ = std::numeric_limits<double>::infinity();
        maxZ = -std::numeric_limits<double>::infinity();
        for (const double val : elevations) {
            if (hasNodata && val == nodataVal) continue;
            if (std::isnan(val)) continue;
            minZ = std::min(minZ, val);
            maxZ = std::max(maxZ, val);
        }
        if (!std::isfinite(minZ) || !std::isfinite(maxZ) || maxZ <= minZ) {
            minZ = 0.0;
            maxZ = 1.0;
        }
    }
    const double zRange = std::max(1e-6, maxZ - minZ);

    TerrainExportOutput output;
    output.datasetUuid = options.datasetUuid;

    const std::string stem = options.displayName.empty() ? "terrain" : options.displayName;

    if (cancel && cancel()) {
        throw domain::terrain::TerrainError(domain::terrain::TerrainErrorCode::InvalidArgument, "terrain export cancelled");
    }

    if (progress) progress(0.50, "Writing heightmap files...");

    // Export Heightmap
    switch (options.heightmapFormat) {
        case ExportHeightmapFormat::GeoTiffFloat32: {
            const auto filePath = options.outputDirectory / (stem + "_float32.tif");
            GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
            if (driver) {
                GDALDataset* outDs = driver->Create(filePath.string().c_str(), outWidth, outHeight, 1, GDT_Float32, nullptr);
                if (outDs) {
                    outDs->SetGeoTransform(geoTransform);
                    if (spatialRef) outDs->SetSpatialRef(spatialRef);
                    std::vector<float> fElevations(elevations.size());
                    for (std::size_t i = 0; i < elevations.size(); ++i) {
                        fElevations[i] = static_cast<float>(elevations[i]);
                    }
                    GDALRasterBand* b = outDs->GetRasterBand(1);
                    b->RasterIO(GF_Write, 0, 0, outWidth, outHeight, fElevations.data(), outWidth, outHeight, GDT_Float32, 0, 0);
                    if (hasNodata) b->SetNoDataValue(nodataVal);
                    GDALClose(outDs);
                    output.exportedFiles.push_back(filePath);
                }
            }
            break;
        }
        case ExportHeightmapFormat::GeoTiffInt16: {
            const auto filePath = options.outputDirectory / (stem + "_int16.tif");
            GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
            if (driver) {
                GDALDataset* outDs = driver->Create(filePath.string().c_str(), outWidth, outHeight, 1, GDT_Int16, nullptr);
                if (outDs) {
                    outDs->SetGeoTransform(geoTransform);
                    if (spatialRef) outDs->SetSpatialRef(spatialRef);
                    std::vector<std::int16_t> iElevations(elevations.size());
                    for (std::size_t i = 0; i < elevations.size(); ++i) {
                        iElevations[i] = static_cast<std::int16_t>(std::round(elevations[i]));
                    }
                    GDALRasterBand* b = outDs->GetRasterBand(1);
                    b->RasterIO(GF_Write, 0, 0, outWidth, outHeight, iElevations.data(), outWidth, outHeight, GDT_Int16, 0, 0);
                    if (hasNodata) b->SetNoDataValue(static_cast<std::int16_t>(nodataVal));
                    GDALClose(outDs);
                    output.exportedFiles.push_back(filePath);
                }
            }
            break;
        }
        case ExportHeightmapFormat::GeoTiffUInt16: {
            const auto filePath = options.outputDirectory / (stem + "_uint16.tif");
            GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
            if (driver) {
                GDALDataset* outDs = driver->Create(filePath.string().c_str(), outWidth, outHeight, 1, GDT_UInt16, nullptr);
                if (outDs) {
                    outDs->SetGeoTransform(geoTransform);
                    if (spatialRef) outDs->SetSpatialRef(spatialRef);
                    std::vector<std::uint16_t> uElevations(elevations.size());
                    for (std::size_t i = 0; i < elevations.size(); ++i) {
                        const double norm = std::clamp((elevations[i] - minZ) / zRange, 0.0, 1.0);
                        uElevations[i] = static_cast<std::uint16_t>(std::round(norm * 65535.0));
                    }
                    GDALRasterBand* b = outDs->GetRasterBand(1);
                    b->RasterIO(GF_Write, 0, 0, outWidth, outHeight, uElevations.data(), outWidth, outHeight, GDT_UInt16, 0, 0);
                    GDALClose(outDs);
                    output.exportedFiles.push_back(filePath);
                }
            }
            break;
        }
        case ExportHeightmapFormat::Png16: {
            const auto filePath = options.outputDirectory / (stem + "_heightmap.png");
            std::vector<std::uint16_t> uElevations(elevations.size());
            for (std::size_t i = 0; i < elevations.size(); ++i) {
                const double norm = std::clamp((elevations[i] - minZ) / zRange, 0.0, 1.0);
                uElevations[i] = static_cast<std::uint16_t>(std::round(norm * 65535.0));
            }
            if (writePng16(filePath, outWidth, outHeight, uElevations)) {
                output.exportedFiles.push_back(filePath);
            }
            break;
        }
        case ExportHeightmapFormat::RawR16: {
            const auto filePath = options.outputDirectory / (stem + "_heightmap.r16");
            std::ofstream out(filePath, std::ios::binary);
            if (out) {
                std::vector<std::uint16_t> uElevations(elevations.size());
                for (std::size_t i = 0; i < elevations.size(); ++i) {
                    const double norm = std::clamp((elevations[i] - minZ) / zRange, 0.0, 1.0);
                    uElevations[i] = static_cast<std::uint16_t>(std::round(norm * 65535.0));
                }
                out.write(reinterpret_cast<const char*>(uElevations.data()), static_cast<std::streamsize>(uElevations.size() * sizeof(std::uint16_t)));
                out.close();
                output.exportedFiles.push_back(filePath);
            }
            break;
        }
        case ExportHeightmapFormat::None:
            break;
    }

    if (progress) progress(0.85, "Writing export manifest...");

    // Write manifest JSON
    const auto manifestPath = options.outputDirectory / "manifest.json";
    nlohmann::json manifest;
    manifest["datasetUuid"] = options.datasetUuid;
    manifest["displayName"] = options.displayName;
    manifest["exportedAt"] = runtime::utcTimestampNow();
    manifest["width"] = outWidth;
    manifest["height"] = outHeight;
    manifest["minElevation"] = minZ;
    manifest["maxElevation"] = maxZ;
    manifest["elevationUnit"] = "metre";

    std::uint64_t totalBytes = 0;
    nlohmann::json filesJson = nlohmann::json::array();
    for (const auto& file : output.exportedFiles) {
        std::error_code sizeEc;
        const auto sz = std::filesystem::file_size(file, sizeEc);
        const std::uint64_t byteSize = sizeEc ? 0 : static_cast<std::uint64_t>(sz);
        totalBytes += byteSize;
        filesJson.push_back({
            {"fileName", file.filename().string()},
            {"bytes", byteSize}
        });
    }
    manifest["files"] = filesJson;

    std::ofstream manifestOut(manifestPath);
    manifestOut << manifest.dump(2);
    manifestOut.close();

    output.manifestPath = manifestPath;
    output.exportedFiles.push_back(manifestPath);
    output.totalBytes = totalBytes;

    if (progress) progress(1.0, "Export completed successfully");

    return output;
}

} // namespace infraforge::application
