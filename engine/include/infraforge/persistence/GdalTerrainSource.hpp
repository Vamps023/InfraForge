#pragma once

#include "infraforge/ports/TerrainSource.hpp"

namespace infraforge::persistence {

// GDAL-backed implementation of the raster source port (vcpkg `gdal`,
// GeoTIFF via the core GTiff driver). This is the only translation unit
// boundary where the raster library exists; domain and application code
// see the port. One GDAL dataset handle per call — safe on worker threads.
class GdalTerrainSource final : public ports::TerrainSourceReader {
public:
    GdalTerrainSource() = default;
    ~GdalTerrainSource() override = default;

    GdalTerrainSource(const GdalTerrainSource&) = delete;
    GdalTerrainSource& operator=(const GdalTerrainSource&) = delete;

    [[nodiscard]] ports::TerrainSourceInfo probe(const std::filesystem::path& file) const override;
    [[nodiscard]] ports::TerrainElevationBlock readBlock(
        const std::filesystem::path& file, std::int64_t offsetX, std::int64_t offsetY,
        std::int64_t width, std::int64_t height) const override;
};

} // namespace infraforge::persistence
