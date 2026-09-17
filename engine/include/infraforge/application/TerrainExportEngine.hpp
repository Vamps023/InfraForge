#pragma once

#include "infraforge/domain/terrain/TerrainDataset.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace infraforge::application {

enum class ExportHeightmapFormat : std::uint8_t {
    GeoTiffFloat32,
    GeoTiffInt16,
    GeoTiffUInt16,
    Png16,
    RawR16,
    None
};

enum class ExportAlbedoFormat : std::uint8_t {
    PngRgb,
    GeoTiffRgb,
    None
};

struct TerrainExportOptions {
    std::string datasetUuid;
    std::filesystem::path sourceRasterPath;
    std::filesystem::path outputDirectory;
    ExportHeightmapFormat heightmapFormat{ExportHeightmapFormat::GeoTiffFloat32};
    ExportAlbedoFormat albedoFormat{ExportAlbedoFormat::None};
    std::uint32_t targetResolution{0}; // 0 = native, or 512, 1024, 2048, 4096
    std::string targetCrs{"auto"};     // "auto", "EPSG:4326", "EPSG:3857", etc.
    double minElevation{0.0};
    double maxElevation{1000.0};
    std::string displayName{"terrain"};
};

struct TerrainExportOutput {
    std::string datasetUuid;
    std::filesystem::path manifestPath;
    std::vector<std::filesystem::path> exportedFiles;
    std::uint64_t totalBytes{0};
};

using ExportProgressCallback = std::function<void(double progress, const std::string& message)>;
using ExportCancellationCallback = std::function<bool()>;

class TerrainExportEngine {
public:
    static TerrainExportOutput executeExport(
        const TerrainExportOptions& options,
        const ExportProgressCallback& progress = nullptr,
        const ExportCancellationCallback& cancel = nullptr);
};

} // namespace infraforge::application
