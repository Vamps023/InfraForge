#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace infraforge::ports {

// Metadata of a candidate DEM source, probed before any project mutation.
// Everything here is detected fact from the raster file itself.
struct TerrainSourceInfo {
    // Raster format short name ("GTiff" for GeoTIFF).
    std::string format;
    // Source CRS definition text (authority form when available, WKT2
    // otherwise). Empty means the file declares no CRS — the caller treats
    // that as an explicit MissingCrs failure, never an assumption.
    std::string crsDefinition;
    // Raster size in pixels; both positive for an accepted source.
    std::int64_t width{0};
    std::int64_t height{0};
    // GDAL north-up geotransform: origin is the top-left pixel corner,
    // sizes are positive magnitudes (the y row step negates).
    double originX{0.0};
    double originY{0.0};
    double pixelSizeX{0.0};
    double pixelSizeY{0.0};
    // Resolved elevation unit name ("metre", "international foot",
    // "US survey foot"). Empty sample-type units normalize to metre, the
    // documented convention for DEMs without vertical metadata.
    std::string elevationUnit;
    double elevationUnitToMetre{1.0};
    // Sample type name as reported by the raster library ("Float32",
    // "Int16", ...); used for the supported-type gate and diagnostics.
    std::string sampleTypeName;
    bool hasNodata{false};
    double nodataValue{0.0};
    std::uint64_t fileBytes{0};
};

// One block of double-precision elevations read from a raster. NoData
// cells carry a quiet NaN — conversion semantics are decided by the
// sampler, never by the reader.
struct TerrainElevationBlock {
    std::int64_t offsetX{0};
    std::int64_t offsetY{0};
    std::int64_t width{0};
    std::int64_t height{0};
    std::vector<double> elevations; // row-major, width*height
};

// Inward interface for reading external DEM files (implemented by the GDAL
// adapter). The domain and application layers consume this port; the
// raster library never leaks past it. Implementations may open one handle
// per call and must be safe to use from worker threads.
class TerrainSourceReader {
public:
    virtual ~TerrainSourceReader() = default;

    // Validates and describes a raster source. Throws domain::terrain
    // TerrainError for unreadable/corrupt/unsupported sources and CRS
    // problems that are detectable without the project georeference.
    [[nodiscard]] virtual TerrainSourceInfo probe(const std::filesystem::path& file) const = 0;

    // Reads one raster window as doubles. Offsets/dimensions are clamped to
    // the raster; throws TerrainError(CorruptSource) when the driver
    // reports an incomplete read, TerrainError(SourceUnreadable) when the
    // file can no longer be opened.
    [[nodiscard]] virtual TerrainElevationBlock readBlock(
        const std::filesystem::path& file, std::int64_t offsetX, std::int64_t offsetY,
        std::int64_t width, std::int64_t height) const = 0;
};

} // namespace infraforge::ports
