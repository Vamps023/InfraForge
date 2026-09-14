#include "infraforge/application/TerrainService.hpp"

#include "infraforge/application/CommandFailure.hpp"
#include "infraforge/application/TerrainTileGenerator.hpp"
#include "infraforge/domain/geo/GeoTransformService.hpp"
#include "infraforge/domain/terrain/TerrariumTerrainProvider.hpp"
#include "infraforge/domain/terrain/TerrainTileFile.hpp"
#include "infraforge/domain/world/Invalidation.hpp"
#include "infraforge/ports/IxHttpClient.hpp"
#include "infraforge/runtime/FileSystemUtf8.hpp"
#include "infraforge/runtime/Logging.hpp"
#include "infraforge/runtime/Sha256.hpp"
#include "infraforge/runtime/Timestamp.hpp"
#include "infraforge/runtime/Uuid.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>

#include <gdal.h>
#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <cpl_string.h>

namespace infraforge::application {
namespace {

using domain::terrain::TerrainError;
using domain::terrain::TerrainErrorCode;
using domain::world::ChunkCoord;
using domain::world::InvalidationClass;
using domain::world::InvalidationMask;

// Copy chunk for streaming the source file into project-owned storage.
constexpr std::size_t kImportCopyChunkBytes = 4u * 1024u * 1024u;
// Elevation scan strip height (raster rows) for range/NoData measurement.
constexpr std::int64_t kElevationScanRows = 256;
// Upper bound of eagerly generated tiles per import; beyond it the import
// completes with a documented deferral diagnostic and
// terrain.regenerate_tiles covers the remainder in bounded batches.
constexpr std::uint64_t kMaxEagerTilesPerImport = 4096;

// Phase weights for coherent normalized import progress (BLOCKER 12).
// The import has three phases with different work units (bytes, rows);
// normalized progress in [0,1] avoids backwards jumps when units change.
// Phase 1 (copy source):   [0.00, 0.40]
// Phase 2 (validate):      [0.40, 0.45]
// Phase 3 (coverage+scan): [0.45, 1.00]
constexpr double kPhase1Start = 0.0;
constexpr double kPhase1End = 0.40;
constexpr double kPhase2Start = 0.40;
constexpr double kPhase2End = 0.45;
constexpr double kPhase3Start = 0.45;
constexpr double kPhase3End = 1.0;

// Download progress phase weights (BLOCKER 9).
// Planning:              [0.00, 0.05]
// Provider acquisition:  [0.05, 0.55]
// Decode/validation:     [0.55, 0.70]
// Assembly:              [0.70, 0.85]
// Canonical validation: [0.85, 0.92]
// Canonical commit:      [0.92, 0.95]
// Tile generation:       [0.95, 1.00]
constexpr double kDlPlanEnd = 0.05;
constexpr double kDlFetchStart = 0.05;
constexpr double kDlFetchEnd = 0.55;
constexpr double kDlDecodeEnd = 0.70;
constexpr double kDlAssembleEnd = 0.85;
constexpr double kDlValidateEnd = 0.92;
constexpr double kDlCommitEnd = 0.95;

[[noreturn]] void failImport(TerrainErrorCode code, const std::string& message) {
    throw TerrainError(code, message);
}

struct ConfirmedElevationUnit {
    std::string name;
    double toMetre;
};

[[nodiscard]] ConfirmedElevationUnit confirmedElevationUnit(const std::string& value) {
    if (value == "metre") return {"metre", 1.0};
    if (value == "international foot") return {"international foot", 0.3048};
    if (value == "US survey foot") return {"US survey foot", 1200.0 / 3937.0};
    failImport(TerrainErrorCode::InvalidArgument,
        "unknown source elevation unit requires an explicit supported unit selection");
}

// BLOCKER 5: Map a typed ProviderErrorCode to the corresponding
// TerrainErrorCode so the typed failure survives through JobRecord →
// protocol event → frontend diagnostics. Cancellation stays cancellation.
TerrainErrorCode mapProviderError(domain::terrain::ProviderErrorCode code) {
    using PE = domain::terrain::ProviderErrorCode;
    switch (code) {
    case PE::AuthenticationFailed:
        return TerrainErrorCode::ProviderAuthenticationFailed;
    case PE::RateLimited:
        return TerrainErrorCode::ProviderRateLimited;
    case PE::NetworkTimeout:
        return TerrainErrorCode::ProviderNetworkTimeout;
    case PE::SourceUnavailable:
        return TerrainErrorCode::ProviderUnavailable;
    case PE::UnsupportedCoverage:
        return TerrainErrorCode::ProviderUnsupportedCoverage;
    case PE::InvalidProviderResponse:
        return TerrainErrorCode::ProviderInvalidResponse;
    case PE::CorruptTerrainResponse:
        return TerrainErrorCode::ProviderCorruptResponse;
    case PE::Cancelled:
        return TerrainErrorCode::InvalidArgument; // cancellation handled separately
    }
    return TerrainErrorCode::SourceUnreadable;
}

// Web Mercator forward: WGS84 lat/lon -> EPSG:3857 x/y (meters).
// Used to transform selected coverage bounds for raster clipping.
constexpr double kWebMercatorPi = 3.14159265358979323846;
constexpr double kWebMercatorEarthRadius = 6378137.0;
struct WebMercatorPoint { double x, y; };
WebMercatorPoint toWebMercatorMeters(double lonDeg, double latDeg) {
    const double lonRad = lonDeg * kWebMercatorPi / 180.0;
    const double latRad = latDeg * kWebMercatorPi / 180.0;
    return {
        kWebMercatorEarthRadius * lonRad,
        kWebMercatorEarthRadius * std::log(std::tan(kWebMercatorPi / 4.0 + latRad / 2.0))
    };
}

// RAII scope guard for temporary directory cleanup (BLOCKER 11).
class TempDirGuard {
public:
    explicit TempDirGuard(std::filesystem::path dir) : dir_(std::move(dir)) {}
    ~TempDirGuard() {
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }
    TempDirGuard(const TempDirGuard&) = delete;
    TempDirGuard& operator=(const TempDirGuard&) = delete;
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return dir_; }
    void release() { dir_.clear(); }
private:
    std::filesystem::path dir_;
};

// Checkpoint helper for assembly cancellation (BLOCKER 4). The callback
// returns true if cancellation was requested; we throw JobCancelled so
// the worker exits cleanly and the job system maps it to Cancelled.
void checkCancel(const domain::terrain::CancellationCallback& cancel) {
    if (cancel && cancel()) {
        throw JobCancelled{};
    }
}

// Assemble multiple temporary GeoTIFFs into a single canonical GeoTIFF
// (BLOCKER 2). Uses GDAL to read each input tile, compute the union bounds,
// and write a single mosaicked GeoTIFF with NoData in gaps (BLOCKER 4).
// If selectedCoverage is non-empty, pixels outside the coverage rectangles
// are set to NoData (clip ONLY to selected application coverage, BLOCKER 2).
struct AssembledRaster {
    std::filesystem::path path;
    double minX, minY, maxX, maxY;  // Web Mercator bounds
    int width, height;
    std::string crs;  // e.g. "EPSG:3857"
    double nodata;
    bool hasNodata;
};

// Coverage rectangle in EPSG:3857 (Web Mercator) metres.
struct CoverageRect { double minX, minY, maxX, maxY; };

AssembledRaster assembleCanonicalGeoTiff(
    const std::vector<std::filesystem::path>& inputTiles,
    const std::filesystem::path& outputPath,
    const std::vector<CoverageRect>& selectedCoverage = {},
    const domain::terrain::CancellationCallback& cancel = {}) {

    if (inputTiles.empty()) {
        failImport(TerrainErrorCode::InvalidArgument,
            "no provider tiles to assemble");
    }

    // Cancellation checkpoint before opening tiles.
    checkCancel(cancel);

    // Read all input tiles to compute union bounds and pixel size.
    struct TileInfo {
        std::filesystem::path path;
        double geotransform[6];
        int width, height;
        std::string crs;
        double nodata;
        bool hasNodata;
    };

    std::vector<TileInfo> tiles;
    tiles.reserve(inputTiles.size());

    double unionMinX = std::numeric_limits<double>::max();
    double unionMinY = std::numeric_limits<double>::max();
    double unionMaxX = std::numeric_limits<double>::lowest();
    double unionMaxY = std::numeric_limits<double>::lowest();
    double pixelW = 0.0, pixelH = 0.0;
    std::string crs;
    double nodata = -32768.0;
    bool hasNodata = false;

    for (const auto& tilePath : inputTiles) {
        GDALDatasetH ds = GDALOpen(tilePath.string().c_str(), GA_ReadOnly);
        if (!ds) {
            failImport(TerrainErrorCode::SourceUnreadable,
                "cannot open provider tile: " + tilePath.string());
        }

        TileInfo info;
        info.path = tilePath;
        info.width = GDALGetRasterXSize(ds);
        info.height = GDALGetRasterYSize(ds);
        GDALGetGeoTransform(ds, info.geotransform);
        info.hasNodata = false;
        info.nodata = 0.0;

        // Get CRS.
        const char* projWkt = GDALGetProjectionRef(ds);
        if (projWkt && *projWkt) {
            info.crs = projWkt;
            if (crs.empty()) crs = projWkt;
        }

        // Get NoData.
        GDALRasterBandH band = GDALGetRasterBand(ds, 1);
        int hasNd = 0;
        double nd = GDALGetRasterNoDataValue(band, &hasNd);
        if (hasNd) {
            info.hasNodata = true;
            info.nodata = nd;
            hasNodata = true;
            nodata = nd;
        }

        // Compute tile bounds from geotransform.
        // geotransform = [originX, pixelW, 0, originY, 0, pixelH]
        // originY is the top (north), pixelH is negative (south).
        double tileMinX = info.geotransform[0];
        double tileMaxX = info.geotransform[0] + info.geotransform[1] * info.width;
        double tileMaxY = info.geotransform[3];
        double tileMinY = info.geotransform[3] + info.geotransform[5] * info.height;

        unionMinX = std::min(unionMinX, tileMinX);
        unionMinY = std::min(unionMinY, tileMinY);
        unionMaxX = std::max(unionMaxX, tileMaxX);
        unionMaxY = std::max(unionMaxY, tileMaxY);

        if (pixelW == 0.0) pixelW = info.geotransform[1];
        if (pixelH == 0.0) pixelH = std::abs(info.geotransform[5]);

        GDALClose(ds);
        tiles.push_back(info);
    }

    if (pixelW <= 0.0 || pixelH <= 0.0 || !std::isfinite(pixelW) || !std::isfinite(pixelH)) {
        failImport(TerrainErrorCode::SourceUnreadable,
            "invalid pixel size in provider tiles");
    }

    // BLOCKER 3: Validate output dimensions before allocation.
    // Use int64_t for intermediate calculations to prevent overflow.
    const double outWidthD = std::round((unionMaxX - unionMinX) / pixelW);
    const double outHeightD = std::round((unionMaxY - unionMinY) / pixelH);
    if (!std::isfinite(outWidthD) || !std::isfinite(outHeightD) ||
        outWidthD <= 0.0 || outHeightD <= 0.0) {
        failImport(TerrainErrorCode::SourceUnreadable,
            "invalid output raster dimensions");
    }
    if (outWidthD > static_cast<double>(std::numeric_limits<int>::max()) ||
        outHeightD > static_cast<double>(std::numeric_limits<int>::max())) {
        failImport(TerrainErrorCode::SourceUnreadable,
            "output raster dimensions exceed int32 range");
    }
    const int outWidth = static_cast<int>(outWidthD);
    const int outHeight = static_cast<int>(outHeightD);

    // BLOCKER 3: Check pixel count and estimated byte size against safety limits.
    const std::int64_t pixelCount = static_cast<std::int64_t>(outWidth) * outHeight;
    if (pixelCount > domain::terrain::kMaxCanonicalRasterPixels) {
        failImport(TerrainErrorCode::SourceUnreadable,
            "output raster pixel count (" + std::to_string(pixelCount)
            + ") exceeds safety limit (" + std::to_string(domain::terrain::kMaxCanonicalRasterPixels) + ")");
    }
    // Estimated bytes: Float32 = 4 bytes/pixel.
    const std::int64_t estBytes = pixelCount * 4;
    if (estBytes > static_cast<std::int64_t>(4) * 1024 * 1024 * 1024) { // 4 GB
        failImport(TerrainErrorCode::SourceUnreadable,
            "output raster estimated size exceeds 4 GB safety limit");
    }

    // Create the output GeoTIFF.
    GDALDriverH driver = GDALGetDriverByName("GTiff");
    if (!driver) {
        failImport(TerrainErrorCode::SourceUnreadable, "GTiff driver not available");
    }

    const char* options[] = { "TILED=YES", "COMPRESS=DEFLATE", "BLOCKXSIZE=256", "BLOCKYSIZE=256", nullptr };
    GDALDatasetH outDs = GDALCreate(driver, outputPath.string().c_str(),
        outWidth, outHeight, 1, GDT_Float32, const_cast<char**>(options));
    if (!outDs) {
        failImport(TerrainErrorCode::SourceUnreadable, "cannot create canonical GeoTIFF");
    }

    // Set geotransform.
    double outGeotransform[6] = {
        unionMinX, pixelW, 0.0,
        unionMaxY, 0.0, -pixelH
    };
    if (GDALSetGeoTransform(outDs, outGeotransform) != CE_None) {
        GDALClose(outDs);
        failImport(TerrainErrorCode::SourceUnreadable, "cannot set output geotransform");
    }

    // Set CRS.
    if (!crs.empty()) {
        if (GDALSetProjection(outDs, crs.c_str()) != CE_None) {
            GDALClose(outDs);
            failImport(TerrainErrorCode::SourceUnreadable, "cannot set output CRS");
        }
    }

    // Set NoData.
    GDALRasterBandH outBand = GDALGetRasterBand(outDs, 1);
    if (hasNodata) {
        GDALSetRasterNoDataValue(outBand, nodata);
    } else {
        GDALSetRasterNoDataValue(outBand, -32768.0);
        nodata = -32768.0;
        hasNodata = true;
    }

    // BLOCKER 3: Initialize the output with NoData using strip-based writes
    // (bounded memory — no full-raster allocation).
    {
        const std::int64_t stripRows = domain::terrain::kAssemblyStripRows;
        std::vector<float> nodataStrip(static_cast<std::size_t>(outWidth) * stripRows,
            static_cast<float>(nodata));
        for (std::int64_t row = 0; row < outHeight; row += stripRows) {
            checkCancel(cancel);
            const int stripH = static_cast<int>(
                std::min(stripRows, static_cast<std::int64_t>(outHeight) - row));
            CPLErr err = GDALRasterIO(outBand, GF_Write,
                0, static_cast<int>(row), outWidth, stripH,
                nodataStrip.data(), outWidth, stripH, GDT_Float32, 0, 0);
            if (err != CE_None) {
                GDALClose(outDs);
                failImport(TerrainErrorCode::SourceUnreadable,
                    "cannot initialize output raster strip at row " + std::to_string(row));
            }
        }
    }

    // Copy each tile's data into the output (tile-by-tile is already bounded).
    for (const auto& tile : tiles) {
        checkCancel(cancel);
        GDALDatasetH ds = GDALOpen(tile.path.string().c_str(), GA_ReadOnly);
        if (!ds) continue;

        GDALRasterBandH band = GDALGetRasterBand(ds, 1);

        // Compute the offset in the output raster.
        const double tileMinX = tile.geotransform[0];
        const double tileMaxY = tile.geotransform[3];
        const int offsetX = static_cast<int>(std::round((tileMinX - unionMinX) / pixelW));
        const int offsetY = static_cast<int>(std::round((unionMaxY - tileMaxY) / pixelH));

        // Read and write the tile data.
        std::vector<float> tileData(static_cast<size_t>(tile.width) * tile.height);
        CPLErr re = GDALRasterIO(band, GF_Read,
            0, 0, tile.width, tile.height,
            tileData.data(), tile.width, tile.height, GDT_Float32, 0, 0);
        if (re != CE_None) {
            GDALClose(ds);
            GDALClose(outDs);
            failImport(TerrainErrorCode::SourceUnreadable,
                "cannot read provider tile: " + tile.path.string());
        }
        CPLErr we = GDALRasterIO(outBand, GF_Write,
            offsetX, offsetY, tile.width, tile.height,
            tileData.data(), tile.width, tile.height, GDT_Float32, 0, 0);
        if (we != CE_None) {
            GDALClose(ds);
            GDALClose(outDs);
            failImport(TerrainErrorCode::SourceUnreadable,
                "cannot write provider tile to output: " + tile.path.string());
        }

        GDALClose(ds);
    }

    // BLOCKER 3: Clip to selected coverage using strip-based processing
    // (bounded memory). Pixels outside the coverage rectangles are set to
    // NoData so unselected gaps have no terrain data (BLOCKER 6).
    // Pre-compute Y-sorted coverage rects for efficient per-strip filtering.
    if (!selectedCoverage.empty()) {
        const std::int64_t stripRows = domain::terrain::kAssemblyStripRows;
        std::vector<float> stripBuf(static_cast<std::size_t>(outWidth) * stripRows);
        for (std::int64_t row = 0; row < outHeight; row += stripRows) {
            checkCancel(cancel);
            const int stripH = static_cast<int>(
                std::min(stripRows, static_cast<std::int64_t>(outHeight) - row));
            CPLErr readErr = GDALRasterIO(outBand, GF_Read,
                0, static_cast<int>(row), outWidth, stripH,
                stripBuf.data(), outWidth, stripH, GDT_Float32, 0, 0);
            if (readErr != CE_None) {
                GDALClose(outDs);
                failImport(TerrainErrorCode::SourceUnreadable,
                    "cannot read strip for coverage clipping at row " + std::to_string(row));
            }
            for (int r = 0; r < stripH; ++r) {
                const double py = unionMaxY -
                    (static_cast<double>(row + r) + 0.5) * pixelH;
                for (int col = 0; col < outWidth; ++col) {
                    const double px = unionMinX +
                        (static_cast<double>(col) + 0.5) * pixelW;
                    bool inside = false;
                    for (const auto& rect : selectedCoverage) {
                        if (px >= rect.minX && px <= rect.maxX &&
                            py >= rect.minY && py <= rect.maxY) {
                            inside = true;
                            break;
                        }
                    }
                    if (!inside) {
                        stripBuf[static_cast<std::size_t>(r) * outWidth + col] =
                            static_cast<float>(nodata);
                    }
                }
            }
            CPLErr writeErr = GDALRasterIO(outBand, GF_Write,
                0, static_cast<int>(row), outWidth, stripH,
                stripBuf.data(), outWidth, stripH, GDT_Float32, 0, 0);
            if (writeErr != CE_None) {
                GDALClose(outDs);
                failImport(TerrainErrorCode::SourceUnreadable,
                    "cannot write clipped strip at row " + std::to_string(row));
            }
        }
    }

    GDALClose(outDs);

    // Resolve CRS to authority code if possible.
    std::string crsCode;
    if (!crs.empty()) {
        OGRSpatialReference srs;
        srs.importFromWkt(crs.c_str());
        const char* authName = srs.GetAuthorityName(nullptr);
        const char* authCode = srs.GetAuthorityCode(nullptr);
        if (authName && authCode) {
            crsCode = std::string(authName) + ":" + std::string(authCode);
        } else {
            crsCode = crs;  // Fall back to WKT
        }
    }

    return AssembledRaster{
        outputPath, unionMinX, unionMinY, unionMaxX, unionMaxY,
        outWidth, outHeight, crsCode, nodata, hasNodata
    };
}

[[nodiscard]] std::filesystem::path renameReplace(const std::filesystem::path& from,
    const std::filesystem::path& to) {
    std::error_code error;
    std::filesystem::rename(from, to, error);
    if (error) {
        // Windows rename cannot overwrite an existing target.
        std::filesystem::remove(to, error);
        std::filesystem::rename(from, to, error);
        if (error) {
            failImport(TerrainErrorCode::SourceDataMissing,
                "cannot publish project-owned raster: " + error.message());
        }
    }
    return to;
}

} // namespace

TerrainService::TerrainService(ports::ProjectStore& store,
    const domain::geo::GeoTransformService& transforms, ports::TerrainSourceReader& reader,
    WorldState& world, JobSystem& jobs,
    domain::terrain::TerrainProviderRegistry providers, EventSink eventSink)
    : store_(store),
      transforms_(transforms),
      reader_(reader),
      world_(world),
      jobs_(jobs),
      eventSink_(std::move(eventSink)),
      providers_(std::move(providers)) {
    // Providers are injected by the caller. Production uses
    // makeProductionTerrainProviders(); tests inject their own registry
    // (which may include MockTerrainProvider). This keeps mock/test
    // providers out of production `terrain.list_sources` (BLOCKER 1).
}

domain::terrain::TerrainProviderRegistry production::makeProductionTerrainProviders() {
    domain::terrain::TerrainProviderRegistry registry;
    auto httpClient = std::make_shared<ports::IxHttpClient>();
    registry.registerProvider(
        std::make_unique<domain::terrain::TerrariumTerrainProvider>(httpClient));
    return registry;
}

void TerrainService::onProjectOpened() {
    const auto& record = store_.current();
    project_ = transforms_.resolveProjectGeoreference(record.georeference);
    revision_ = record.revision;
    world_.resetForProject(*project_);
    datasets_.clear();

    // Registry replay in creation order reproduces the spatial index's
    // terrain-class content generations deterministically, so derived tile
    // caches recorded against lastAffectingRevision(chunk, Terrain) remain
    // current across reopen (docs/05_DOMAINS/TERRAIN.md).
    for (domain::terrain::TerrainDataset& dataset : store_.terrainDatasets()) {
        std::error_code error;
        const auto storedFile = projectDirectory_() / std::filesystem::path{dataset.storagePath};
        if (!std::filesystem::is_regular_file(storedFile, error)) {
            dataset.diagnostics.push_back({TerrainErrorCode::SourceDataMissing,
                "project-owned raster storage is missing: " + dataset.storagePath});
            runtime::logError("terrain", "terrain.storage_missing",
                {{"dataset", domain::terrain::uuidTextFromEntityId(dataset.id)}});
        }
        // Registry replay mutation: its chunk-diff value is intentionally
        // unused — generation replay is the effect that matters here.
        (void)world_.insert(dataset.id, dataset.bounds, InvalidationMask::of(InvalidationClass::Terrain));
        datasets_.push_back(std::move(dataset));
    }
}

void TerrainService::onProjectClosed() {
    cancelTrackedJobs();
    project_.reset();
    datasets_.clear();
    revision_ = 0;
}

TerrainProbeResult TerrainService::probeSource(const std::filesystem::path& file) const {
    TerrainProbeResult result;
    result.source = reader_.probe(file);
    if (result.source.crsDefinition.empty()) {
        // Defensive: the reader port rejects CRS-less rasters already.
        failImport(TerrainErrorCode::MissingCrs,
            "source raster declares no CRS; a missing CRS is never assumed");
    }
    const auto resolved = transforms_.describeCrs(result.source.crsDefinition);
    result.crsName = resolved.name;
    result.crsKind = std::string{domain::geo::crsKindName(resolved.kind)};
    result.crsAuthority = resolved.authority;
    result.crsCode = resolved.code;
    return result;
}

JobRecord TerrainService::startImport(const TerrainImportSpec& spec) {
    if (!store_.isOpen() || !project_.has_value()) {
        throw CommandFailure(CommandFailureCode::ProjectNotOpen, "terrain import requires an open project");
    }
    if (spec.displayName.empty() || spec.displayName.size() > domain::terrain::kMaxTerrainDisplayNameLength) {
        throw CommandFailure(CommandFailureCode::InvalidArgument,
            "terrain display name must be 1.." + std::to_string(domain::terrain::kMaxTerrainDisplayNameLength)
                + " characters");
    }

    // Full source validation up front so CRS/raster problems surface as a
    // typed command error before any job starts or byte is copied.
    const TerrainProbeResult probe = probeSource(spec.sourcePath);
    std::optional<ConfirmedElevationUnit> overrideUnit;
    if (probe.source.elevationUnit == "unknown") {
        if (spec.elevationUnitOverride.empty()) {
            failImport(TerrainErrorCode::InvalidArgument,
                "source does not declare an elevation unit; explicitly choose how sample values are interpreted");
        }
        overrideUnit = confirmedElevationUnit(spec.elevationUnitOverride);
    }

    const std::string datasetUuid = runtime::generateUuidV4();
    const std::filesystem::path projectDirectory = projectDirectory_();
    // Aggregate construction: ImportPayload is not default-constructible
    // because the chunk grid requires its configured cell size.
    auto payload = std::shared_ptr<ImportPayload>(new ImportPayload{
        .dataset = {},
        .projectUuid = store_.current().uuid,
        .projectDirectory = projectDirectory,
        .tempFile = projectDirectory / "terrain" / "elevation" / (datasetUuid + ".tif.importing"),
        .grid = world_.grid(),
        .project = *project_,
        .elevationUnitOverride = spec.elevationUnitOverride,
    });

    domain::terrain::TerrainDataset& dataset = payload->dataset;
    dataset.id = domain::terrain::entityIdFromUuidText(datasetUuid);
    dataset.displayName = spec.displayName;
    dataset.storagePath = "terrain/elevation/" + datasetUuid + ".tif";
    dataset.sourceFormat = probe.source.format;
    dataset.sourceCrs = probe.source.crsDefinition;
    dataset.rasterWidth = probe.source.width;
    dataset.rasterHeight = probe.source.height;
    dataset.originX = probe.source.originX;
    dataset.originY = probe.source.originY;
    dataset.cellSizeX = probe.source.pixelSizeX;
    dataset.cellSizeY = probe.source.pixelSizeY;
    dataset.horizontalUnitName = probe.source.horizontalUnitName;
    dataset.horizontalUnitSymbol = probe.source.horizontalUnitSymbol;
    dataset.horizontalUnitIsAngular = probe.source.horizontalUnitIsAngular;
    dataset.elevationUnit = overrideUnit ? overrideUnit->name : probe.source.elevationUnit;
    dataset.elevationUnitToMetre = overrideUnit ? overrideUnit->toMetre : probe.source.elevationUnitToMetre;
    dataset.elevationUnitSource = overrideUnit ? "user override" : probe.source.elevationUnitSource;
    dataset.sampleScale = probe.source.sampleScale;
    dataset.sampleOffset = probe.source.sampleOffset;
    dataset.hasNodata = probe.source.hasNodata;
    dataset.nodataValue = probe.source.nodataValue;
    dataset.sourceBytes = probe.source.fileBytes;
    dataset.createdAt = runtime::utcTimestampNow();
    dataset.modifiedAt = dataset.createdAt;

    const std::filesystem::path sourcePath = spec.sourcePath;
    const std::uint64_t totalBytes = probe.source.fileBytes;
    const auto body = [this, payload, sourcePath, totalBytes](JobContext& context) -> JobSystem::Payload {
        // Phase 1: stream the source into project-owned storage, hashing
        // while copying. Progress units are real bytes.
        {
            std::ifstream input(sourcePath, std::ios::binary);
            if (!input) {
                failImport(TerrainErrorCode::SourceUnreadable,
                    "terrain source cannot be opened: " + sourcePath.string());
            }
            std::ofstream output(payload->tempFile, std::ios::binary | std::ios::trunc);
            if (!output) {
                failImport(TerrainErrorCode::SourceDataMissing,
                    "project-owned terrain storage cannot be created: " + payload->tempFile.string());
            }
            runtime::Sha256 hash;
            std::vector<char> buffer(kImportCopyChunkBytes);
            std::uint64_t copied = 0;
            while (input) {
                context.throwIfCancelled();
                input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                const std::streamsize read = input.gcount();
                if (read <= 0) {
                    break;
                }
                output.write(buffer.data(), read);
                if (!output) {
                    failImport(TerrainErrorCode::SourceDataMissing,
                        "project-owned terrain storage write failed: " + payload->tempFile.string());
                }
                hash.update(buffer.data(), static_cast<std::size_t>(read));
                copied += static_cast<std::uint64_t>(read);
                const double copyRatio = totalBytes > 0
                    ? static_cast<double>(copied) / static_cast<double>(totalBytes)
                    : 0.0;
                context.reportNormalizedProgress(
                    kPhase1Start + copyRatio * (kPhase1End - kPhase1Start), "copying source raster");
            }
            if (input.bad()) {
                failImport(TerrainErrorCode::CorruptSource,
                    "terrain source read failed while copying: " + sourcePath.string());
            }
            if (totalBytes != 0 && copied != totalBytes) {
                failImport(TerrainErrorCode::CorruptSource,
                    "terrain source changed size while being read (expected "
                        + std::to_string(totalBytes) + " bytes, read " + std::to_string(copied) + ")");
            }
            payload->dataset.sourceSha256 = runtime::sha256Hex(hash.finish());
        }

        // Phase 2: validate the stored copy as a raster and make it the
        // authoritative source of canonical metadata (HIGH 5 TOCTOU fix).
        // The initial probe (before the job) may have observed a different
        // source state; after the async copy, the stored copy is the only
        // truth. All canonical metadata is re-derived from the stored copy.
        context.reportNormalizedProgress(kPhase2Start, "validating stored copy");
        context.throwIfCancelled();
        const ports::TerrainSourceInfo stored = reader_.probe(payload->tempFile);
        if (stored.width <= 0 || stored.height <= 0 || stored.crsDefinition.empty()) {
            failImport(TerrainErrorCode::CorruptSource,
                "stored terrain copy is not a valid raster");
        }
        // Re-derive canonical metadata from the stored copy. If the source
        // changed between the initial probe and the copy, the stored copy
        // wins — the dataset agrees with what is actually persisted.
        payload->dataset.sourceFormat = stored.format;
        payload->dataset.sourceCrs = stored.crsDefinition;
        payload->dataset.rasterWidth = stored.width;
        payload->dataset.rasterHeight = stored.height;
        payload->dataset.originX = stored.originX;
        payload->dataset.originY = stored.originY;
        payload->dataset.cellSizeX = stored.pixelSizeX;
        payload->dataset.cellSizeY = stored.pixelSizeY;
        payload->dataset.horizontalUnitName = stored.horizontalUnitName;
        payload->dataset.horizontalUnitSymbol = stored.horizontalUnitSymbol;
        payload->dataset.horizontalUnitIsAngular = stored.horizontalUnitIsAngular;
        if (stored.elevationUnit == "unknown") {
            const auto selected = confirmedElevationUnit(payload->elevationUnitOverride);
            payload->dataset.elevationUnit = selected.name;
            payload->dataset.elevationUnitToMetre = selected.toMetre;
            payload->dataset.elevationUnitSource = "user override";
        } else {
            payload->dataset.elevationUnit = stored.elevationUnit;
            payload->dataset.elevationUnitToMetre = stored.elevationUnitToMetre;
            payload->dataset.elevationUnitSource = stored.elevationUnitSource;
        }
        payload->dataset.sampleScale = stored.sampleScale;
        payload->dataset.sampleOffset = stored.sampleOffset;
        payload->dataset.hasNodata = stored.hasNodata;
        payload->dataset.nodataValue = stored.nodataValue;
        payload->dataset.sourceBytes = stored.fileBytes;

        // Worker-confined transform service (PROJ objects are thread-bound);
        // all project geometry comes from the immutable payload snapshot,
        // never from executor-owned service members.
        const domain::geo::GeoTransformService workerTransforms;
        const domain::geo::ProjectGeoreference& project = payload->project;
        const domain::geo::SourceSpatialReference sourceSrs{
            .horizontalCrs = payload->dataset.sourceCrs, .verticalCrs = ""};

        // Phase 3: coverage transform + elevation scan. Progress units are
        // scanned raster rows; NoData cells are counted, never resampled.
        const std::uint64_t totalScanRows = static_cast<std::uint64_t>(payload->dataset.rasterHeight);
        bool anyValid = false;
        double minZSource = std::numeric_limits<double>::infinity();
        double maxZSource = -std::numeric_limits<double>::infinity();
        std::uint64_t nodataCells = 0;

        domain::world::SpatialBounds coverage = domain::world::SpatialBounds::empty();
        for (std::int64_t corner = 0; corner < 4; ++corner) {
            const double sourceX = payload->dataset.originX
                + (corner % 2 == 1 ? static_cast<double>(payload->dataset.rasterWidth) * payload->dataset.cellSizeX : 0.0);
            const double sourceY = payload->dataset.originY
                - (corner / 2 == 1 ? static_cast<double>(payload->dataset.rasterHeight) * payload->dataset.cellSizeY : 0.0);
            const auto position = workerTransforms.sourceToProjectGlobal(
                project, sourceSrs, domain::geo::GeoCoordinate{.x = sourceX, .y = sourceY, .z = 0.0});
            if (!domain::geo::isFinite(position)) {
                failImport(TerrainErrorCode::InvalidCoverage,
                    "source coverage corner transforms to a non-finite canonical position");
            }
            coverage.expandTo(position);
        }
        if (coverage.isEmpty()) {
            failImport(TerrainErrorCode::InvalidCoverage, "source coverage is empty after transform");
        }
        payload->dataset.bounds = coverage;
        // BLOCKER 6: Local-file imports have a single full coverage piece
        // equal to the enclosing bounds. This keeps the canonical coverage
        // representation consistent between local and remote imports.
        payload->dataset.coveragePieces = { coverage };

        for (std::int64_t row = 0; row < payload->dataset.rasterHeight; row += kElevationScanRows) {
            context.throwIfCancelled();
            const ports::TerrainElevationBlock block = reader_.readBlock(payload->tempFile, 0, row,
                payload->dataset.rasterWidth,
                std::min<std::int64_t>(kElevationScanRows, payload->dataset.rasterHeight - row));
            for (const double value : block.elevations) {
                if (std::isnan(value)) {
                    ++nodataCells;
                    continue;
                }
                anyValid = true;
                minZSource = std::min(minZSource, value);
                maxZSource = std::max(maxZSource, value);
            }
            const double scanProgress =
                static_cast<double>(std::min(row + kElevationScanRows, payload->dataset.rasterHeight))
                / static_cast<double>(totalScanRows);
            context.reportNormalizedProgress(
                kPhase3Start + scanProgress * (kPhase3End - kPhase3Start), "measuring elevation range");
        }
        if (!anyValid) {
            failImport(TerrainErrorCode::UnsupportedRaster, "raster contains no valid elevation cells");
        }
        payload->dataset.minZ = minZSource * payload->dataset.elevationUnitToMetre
            / project.linearUnit.toMetre;
        payload->dataset.maxZ = maxZSource * payload->dataset.elevationUnitToMetre
            / project.linearUnit.toMetre;
        if (stored.sampleTypeName == "UInt16" && stored.elevationUnit == "unknown"
            && stored.sampleScale == 1.0 && stored.sampleOffset == 0.0
            && !stored.hasNodata && maxZSource >= 65500.0) {
            payload->dataset.diagnostics.push_back({TerrainErrorCode::SuspiciousEncoding,
                "This raster may be an encoded/normalized heightmap rather than elevations in physical units. Verify elevation unit and scale before import."});
        }
        if (nodataCells > 0) {
            payload->dataset.hasNodata = true;
            payload->dataset.diagnostics.push_back({TerrainErrorCode::NodataCells,
                std::to_string(nodataCells) + " NoData cells are excluded from sampling and rendering"});
        }
        return payload;
    };

    const auto onProgress = [this](const JobRecord& record) { emitJobUpdate(record); };
    const auto onComplete = [this, payload](const JobOutcome& outcome) {
        untrackJob(outcome.record.jobId);
        if (outcome.record.state != JobState::Completed) {
            // Failed or cancelled before commit: clean the temporary copy so
            // no partial storage remains and no canonical state exists.
            // The registry already has the terminal state (Failed/Cancelled)
            // because non-Completed outcomes finalize immediately.
            std::error_code error;
            std::filesystem::remove(payload->tempFile, error);
            emitJobUpdate(outcome.record);
            return;
        }
        // Body succeeded; the job is finalizing (registry state = Running).
        // The canonical commit below determines the authoritative terminal
        // state. Only after commit success does the job become Completed.
        try {
            if (!store_.isOpen() || store_.current().uuid != payload->projectUuid) {
                throw CommandFailure(CommandFailureCode::ProjectNotOpen,
                    "the project closed or changed while the terrain import was running");
            }
            commitImportedDataset(*std::static_pointer_cast<ImportPayload>(outcome.payload));
            jobs_.markCompleted(outcome.record.jobId);
            // Emit the completed job update after the registry is authoritative.
            JobRecord completed = outcome.record;
            completed.state = JobState::Completed;
            emitJobUpdate(completed);
        } catch (const std::exception& error) {
            // Canonical commit failed: the temp file is already cleaned inside
            // commitImportedDataset's compensation. Mark the job FAILED in the
            // authoritative registry so job.list agrees with the event.
            jobs_.markFailed(outcome.record.jobId,
                std::string{"terrain import commit failed: "} + error.what());
            JobRecord failed = outcome.record;
            failed.state = JobState::Failed;
            failed.message = std::string{"terrain import commit failed: "} + error.what();
            emitJobUpdate(failed);
        }
    };

    const JobRecord record = jobs_.submit("terrain.import", "copying source raster",
        body, onProgress, onComplete, /*requiresFinalization=*/true);
    trackJob(record.jobId);
    return record;
}

void TerrainService::commitImportedDataset(ImportPayload payload) {
    // Publish the project-owned raster at its canonical storage location
    // before the database transaction (manifest-first compensation: a crash
    // before commit leaves an unreferenced file, never a canonical record
    // without storage).
    const std::filesystem::path canonicalPath =
        payload.projectDirectory / std::filesystem::path{payload.dataset.storagePath};
    (void)renameReplace(payload.tempFile, canonicalPath);

    // The canonical commit boundary: DB row + world index + in-memory
    // projection must all succeed together. If any fails after the DB row
    // is committed, the DB row is rolled back (removeTerrainDataset) and
    // the raster is removed. After this boundary, the dataset is canonical
    // and post-commit failures (events, tile generation) never delete the
    // raster or roll back the DB.
    const std::string datasetUuid = domain::terrain::uuidTextFromEntityId(payload.dataset.id);

    ports::TerrainDatasetInsertResult inserted;
    try {
        inserted = store_.insertTerrainDataset(payload.dataset);
        // world_.insert does not throw (validated bounds math), but if it
        // ever does, we must roll back the DB row.
        const domain::world::IndexMutation mutation = world_.insert(
            payload.dataset.id, payload.dataset.bounds, InvalidationMask::of(InvalidationClass::Terrain));
        domain::world::ChunkDirtySet dirty;
        dirty.absorb(mutation);
        datasets_.push_back(payload.dataset);
        revision_ = inserted.record.revision;

        runtime::logInfo("terrain", "terrain.dataset_imported",
            {{"dataset", datasetUuid},
                {"dirtyChunks", std::to_string(dirty.size())}});
    } catch (...) {
        // Canonical commit failed. Roll back the DB row (if it was
        // committed) and remove the raster. The UUID-derived storage path
        // guarantees no collision with a previous file.
        try {
            store_.removeTerrainDataset(datasetUuid);
        } catch (const std::exception& rollbackError) {
            runtime::logError("terrain", "terrain.rollback_db_failed",
                {{"dataset", datasetUuid}, {"error", rollbackError.what()}});
        }
        std::error_code cleanupError;
        std::filesystem::remove(canonicalPath, cleanupError);
        if (cleanupError) {
            runtime::logError("terrain", "terrain.compensation_cleanup_failed",
                {{"path", runtime::utf8String(canonicalPath)},
                    {"error", cleanupError.message()}});
        }
        throw;
    }

    // Post-commit: events and derived tile generation. Failures here are
    // logged/reported but do NOT delete the canonical raster or roll back
    // the DB — the dataset is already canonical and durable.
    try {
        if (eventSink_) {
            eventSink_({.job = std::nullopt, .datasetAdded = payload.dataset, .revision = revision_});
        }
        submitTileGenerationJob(std::move(payload));
    } catch (const std::exception& postCommitError) {
        // The dataset is canonical; tile generation is derived cache work.
        // Log the failure but do not invalidate the committed import.
        runtime::logError("terrain", "terrain.post_commit_failed",
            {{"dataset", datasetUuid}, {"error", postCommitError.what()}});
    }
}

void TerrainService::submitTileGenerationJob(ImportPayload payload) {
    const std::string datasetUuid = domain::terrain::uuidTextFromEntityId(payload.dataset.id);
    std::vector<ChunkCoord> chunks = expectedChunks(payload.dataset);
    if (chunks.size() > kMaxEagerTilesPerImport) {
        // Eager generation is bounded; the remainder is covered by
        // terrain.regenerate_tiles in cancellable batches (documented in
        // docs/05_DOMAINS/TERRAIN.md).
        runtime::logWarn("terrain", "terrain.tiles_deferred",
            {{"dataset", datasetUuid},
                {"deferredChunks", std::to_string(chunks.size() - kMaxEagerTilesPerImport)}});
        chunks.resize(static_cast<std::size_t>(kMaxEagerTilesPerImport));
    }

    const auto body = [this, payload, chunks](JobContext& context) -> JobSystem::Payload {
        const TerrainTileGenerator::Summary summary = TerrainTileGenerator::generateTiles(
            {.projectDirectory = payload.projectDirectory,
                .dataset = payload.dataset,
                .grid = payload.grid,
                .project = payload.project,
                .sourceCrs = payload.dataset.sourceCrs},
            reader_, chunks,
            [&context] { context.throwIfCancelled(); },
            [&context](std::uint64_t done, std::uint64_t total) {
                context.reportProgress(done, total, "generating terrain tiles");
            });
        runtime::logInfo("terrain", "terrain.tiles_generated",
            {{"dataset", domain::terrain::uuidTextFromEntityId(payload.dataset.id)},
                {"generated", std::to_string(summary.tilesGenerated)},
                {"skipped", std::to_string(summary.tilesSkipped)}});
        return nullptr;
    };

    const auto onProgress = [this](const JobRecord& record) { emitJobUpdate(record); };
    const auto onComplete = [this](const JobOutcome& outcome) {
        untrackJob(outcome.record.jobId);
        emitJobUpdate(outcome.record);
        if (outcome.record.state != JobState::Completed) {
            runtime::logWarn("terrain", "terrain.tiles_job_not_completed",
                {{"job", outcome.record.jobId},
                    {"state", std::string{jobStateName(outcome.record.state)}}});
        }
    };

    const JobRecord record = jobs_.submit("terrain.tiles", "generating terrain tiles",
        body, onProgress, onComplete, /*requiresFinalization=*/false);
    trackJob(record.jobId);
}

JobRecord TerrainService::regenerateTiles(const std::string& datasetUuid) {
    if (!store_.isOpen() || !project_.has_value()) {
        throw CommandFailure(CommandFailureCode::ProjectNotOpen, "terrain requires an open project");
    }
    const auto dataset = findDataset(datasetUuid);
    if (!dataset.has_value()) {
        throw CommandFailure(CommandFailureCode::InvalidArgument,
            "unknown terrain dataset: " + datasetUuid);
    }

    std::vector<ChunkCoord> missing;
    for (const ChunkCoord chunk : expectedChunks(*dataset)) {
        const auto path = TerrainTileGenerator::tilePath(projectDirectory_(), datasetUuid, chunk.x, chunk.y);
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error)) {
            missing.push_back(chunk);
        }
    }
    if (missing.empty()) {
        throw CommandFailure(CommandFailureCode::InvalidArgument,
            "all derived tiles for dataset " + datasetUuid + " are present");
    }

    auto payload = std::shared_ptr<ImportPayload>(new ImportPayload{
        .dataset = *dataset,
        .projectUuid = store_.current().uuid,
        .projectDirectory = projectDirectory_(),
        .tempFile = {},
        .grid = world_.grid(),
        .project = *project_,
    });

    const auto body = [this, payload, missing](JobContext& context) -> JobSystem::Payload {
        (void)TerrainTileGenerator::generateTiles(
            {.projectDirectory = payload->projectDirectory,
                .dataset = payload->dataset,
                .grid = payload->grid,
                .project = payload->project,
                .sourceCrs = payload->dataset.sourceCrs},
            reader_, missing,
            [&context] { context.throwIfCancelled(); },
            [&context](std::uint64_t done, std::uint64_t total) {
                context.reportProgress(done, total, "generating terrain tiles");
            });
        return nullptr;
    };
    const auto onProgress = [this](const JobRecord& record) { emitJobUpdate(record); };
    const auto onComplete = [this](const JobOutcome& outcome) {
        untrackJob(outcome.record.jobId);
        emitJobUpdate(outcome.record);
    };
    const JobRecord record = jobs_.submit("terrain.tiles", "generating terrain tiles",
        body, onProgress, onComplete, /*requiresFinalization=*/false);
    trackJob(record.jobId);
    return record;
}

std::vector<domain::terrain::TerrainDataset> TerrainService::listDatasets() const {
    return datasets_;
}

TerrainDatasetDetails TerrainService::datasetDetails(const std::string& datasetUuid) const {
    const auto dataset = findDataset(datasetUuid);
    if (!dataset.has_value()) {
        throw CommandFailure(CommandFailureCode::InvalidArgument,
            "unknown terrain dataset: " + datasetUuid);
    }
    TerrainDatasetDetails details{.dataset = *dataset};
    for (const ChunkCoord chunk : expectedChunks(*dataset)) {
        ++details.expectedTiles;
        const auto path = TerrainTileGenerator::tilePath(projectDirectory_(), datasetUuid, chunk.x, chunk.y);
        std::error_code error;
        if (std::filesystem::is_regular_file(path, error)) {
            ++details.presentTiles;
        }
    }
    return details;
}

TerrainSceneProjection TerrainService::sceneProjection() const {
    if (!project_.has_value()) {
        throw CommandFailure(CommandFailureCode::ProjectNotOpen, "terrain requires an open project");
    }
    TerrainSceneProjection projection;
    projection.renderOrigin = project_->origin;
    projection.revision = revision_;
    for (const domain::terrain::TerrainDataset& dataset : datasets_) {
        for (const ChunkCoord chunk : expectedChunks(dataset)) {
            const auto path = TerrainTileGenerator::tilePath(projectDirectory_(),
                domain::terrain::uuidTextFromEntityId(dataset.id), chunk.x, chunk.y);
            std::error_code error;
            if (!std::filesystem::is_regular_file(path, error)) {
                ++projection.missingTiles;
                continue;
            }
            // BLOCKER 6: The tile rect is the intersection of the chunk
            // bounds with the dataset's actual coverage (union of pieces),
            // not just the enclosing bounding box. This ensures the scene
            // does not claim terrain in unselected gaps.
            const auto chunkBounds = world_.grid().chunkBounds(chunk);
            domain::world::SpatialBounds tileRect = domain::world::SpatialBounds::empty();
            if (dataset.coveragePieces.empty()) {
                tileRect = chunkBounds.intersectedWith(dataset.bounds);
            } else {
                for (const auto& piece : dataset.coveragePieces) {
                    const auto intersect = chunkBounds.intersectedWith(piece);
                    if (!intersect.isEmpty()) {
                        if (tileRect.isEmpty()) {
                            tileRect = intersect;
                        } else {
                            tileRect = tileRect.unitedWith(intersect);
                        }
                    }
                }
            }
            if (tileRect.isEmpty()) continue;
            projection.tiles.push_back({
                .datasetUuid = domain::terrain::uuidTextFromEntityId(dataset.id),
                .datasetRevision = dataset.revision,
                .chunkX = chunk.x,
                .chunkY = chunk.y,
                .absolutePath = runtime::utf8String(path),
                .minEasting = tileRect.minEasting,
                .minNorthing = tileRect.minNorthing,
                .maxEasting = tileRect.maxEasting,
                .maxNorthing = tileRect.maxNorthing,
            });
        }
    }
    return projection;
}

TerrainSampleResult TerrainService::sample(
    const std::string& datasetUuid, const double easting, const double northing) const {
    if (!store_.isOpen() || !project_.has_value()) {
        throw CommandFailure(CommandFailureCode::ProjectNotOpen, "terrain requires an open project");
    }
    if (!std::isfinite(easting) || !std::isfinite(northing)) {
        throw CommandFailure(CommandFailureCode::InvalidArgument, "sample coordinates must be finite");
    }

    std::optional<domain::terrain::TerrainDataset> dataset;
    if (!datasetUuid.empty()) {
        dataset = findDataset(datasetUuid);
        if (!dataset.has_value()) {
            throw CommandFailure(CommandFailureCode::InvalidArgument,
                "unknown terrain dataset: " + datasetUuid);
        }
    } else {
        // Deterministic overlap rule: the most recently imported covering
        // dataset wins (reverse creation order). BLOCKER 6: test actual
        // coverage pieces, not just the enclosing bounding box, so an
        // unselected gap between selected islands returns OutsideCoverage.
        for (auto iterator = datasets_.rbegin(); iterator != datasets_.rend(); ++iterator) {
            // First check the enclosing bounds for a fast reject.
            if (!iterator->bounds.contains(easting, northing)) continue;
            // If coverage pieces exist, the point must be inside at least
            // one piece to be considered covered.
            if (!iterator->coveragePieces.empty()) {
                bool inPiece = false;
                for (const auto& piece : iterator->coveragePieces) {
                    if (piece.contains(easting, northing)) {
                        inPiece = true;
                        break;
                    }
                }
                if (!inPiece) continue;
            }
            dataset = *iterator;
            break;
        }
        if (!dataset.has_value()) {
            return {.datasetUuid = "", .sample = {.status = domain::terrain::TerrainSampleStatus::OutsideCoverage}};
        }
    }

    // BLOCKER 6: Even when a specific dataset is requested, verify the point
    // is inside actual coverage pieces (not just the enclosing bounds).
    if (!dataset->coveragePieces.empty()) {
        bool inPiece = false;
        for (const auto& piece : dataset->coveragePieces) {
            if (piece.contains(easting, northing)) {
                inPiece = true;
                break;
            }
        }
        if (!inPiece) {
            return {.datasetUuid = domain::terrain::uuidTextFromEntityId(dataset->id),
                    .sample = {.status = domain::terrain::TerrainSampleStatus::OutsideCoverage}};
        }
    }

    domain::terrain::TerrainSampler::RasterGeometry geometry;
    geometry.width = dataset->rasterWidth;
    geometry.height = dataset->rasterHeight;
    geometry.originX = dataset->originX;
    geometry.originY = dataset->originY;
    geometry.cellSizeX = dataset->cellSizeX;
    geometry.cellSizeY = dataset->cellSizeY;
    geometry.elevationUnitToMetre = dataset->elevationUnitToMetre;

    const domain::terrain::TerrainSampler sampler(
        reader_, projectDirectory_() / std::filesystem::path{dataset->storagePath},
        geometry, *project_,
        domain::geo::SourceSpatialReference{.horizontalCrs = dataset->sourceCrs, .verticalCrs = ""},
        transforms_);
    return {.datasetUuid = domain::terrain::uuidTextFromEntityId(dataset->id), .sample = sampler.sample(easting, northing)};
}

std::filesystem::path TerrainService::projectDirectory_() const {
    return std::filesystem::path{runtime::pathFromUtf8(store_.current().directory)};
}

std::optional<domain::terrain::TerrainDataset> TerrainService::findDataset(
    const std::string& datasetUuid) const {
    for (const domain::terrain::TerrainDataset& dataset : datasets_) {
        if (domain::terrain::uuidTextFromEntityId(dataset.id) == datasetUuid) {
            return dataset;
        }
    }
    return std::nullopt;
}

std::vector<ChunkCoord> TerrainService::expectedChunks(
    const domain::terrain::TerrainDataset& dataset) const {
    // BLOCKER 6: Filter chunks by actual coverage pieces, not just the
    // enclosing bounding box. Chunks that fall entirely in an unselected
    // gap (no coverage piece) must not generate terrain tiles.
    const auto allChunks = world_.chunksIntersecting(dataset.bounds);
    if (dataset.coveragePieces.empty()) {
        return allChunks;
    }
    std::vector<ChunkCoord> filtered;
    filtered.reserve(allChunks.size());
    for (const auto& chunk : allChunks) {
        const auto chunkBounds = world_.grid().chunkBounds(chunk);
        // A chunk is included if it intersects at least one coverage piece.
        bool intersects = false;
        for (const auto& piece : dataset.coveragePieces) {
            if (chunkBounds.intersects(piece)) {
                intersects = true;
                break;
            }
        }
        if (intersects) {
            filtered.push_back(chunk);
        }
    }
    return filtered;
}

void TerrainService::emitJobUpdate(const JobRecord& record) {
    if (eventSink_) {
        eventSink_({.job = record, .datasetAdded = std::nullopt, .revision = revision_});
    }
}

void TerrainService::trackJob(const std::string& jobId) {
    activeJobIds_.push_back(jobId);
}

void TerrainService::untrackJob(const std::string& jobId) {
    std::erase(activeJobIds_, jobId);
}

void TerrainService::cancelTrackedJobs() {
    // Cooperative: each job stops at its next bounded checkpoint; canonical
    // state stays consistent because commits verify the project identity.
    for (const std::string& jobId : activeJobIds_) {
        (void)jobs_.requestCancel(jobId);
    }
}

// ---- Download Area workflow (Issue #6 BLOCKER 7) ----

std::vector<domain::terrain::ProviderInfo> TerrainService::listSources() const {
    return providers_.listProviders();
}

domain::terrain::DownloadPlan TerrainService::planDownload(
    const std::string& providerId,
    const domain::terrain::GeoBounds& area,
    std::uint32_t tileSizeMetres,
    const std::vector<std::int32_t>& selectedIndices) const {
    const domain::terrain::TerrainDownloadProvider* provider = providers_.find(providerId);
    if (!provider) {
        throw TerrainError(TerrainErrorCode::InvalidArgument,
            "unknown terrain provider: " + providerId);
    }

    // BLOCKER 15: Validate geographic inputs rigorously.
    auto validateDouble = [](double v, const char* name) {
        if (std::isnan(v) || std::isinf(v)) {
            throw TerrainError(TerrainErrorCode::InvalidArgument,
                std::string(name) + " is NaN or infinite");
        }
    };
    validateDouble(area.west, "area.west");
    validateDouble(area.east, "area.east");
    validateDouble(area.south, "area.south");
    validateDouble(area.north, "area.north");
    if (area.west >= area.east) {
        throw TerrainError(TerrainErrorCode::InvalidArgument,
            "area.west must be less than area.east");
    }
    if (area.south >= area.north) {
        throw TerrainError(TerrainErrorCode::InvalidArgument,
            "area.south must be less than area.north");
    }
    // Validate longitude/latitude ranges (WGS84). BLOCKER 15: Web Mercator
    // is only valid within ±85.05112878°; accepting ±90° would produce
    // non-finite values in the selection grid.
    if (area.west < -180.0 || area.east > 180.0) {
        throw TerrainError(TerrainErrorCode::InvalidArgument,
            "longitude must be in [-180, 180]");
    }
    if (area.south < -85.05112878 || area.north > 85.05112878) {
        throw TerrainError(TerrainErrorCode::InvalidArgument,
            "latitude must be within Web Mercator valid range [-85.05, 85.05]");
    }
    // Validate tile size.
    if (tileSizeMetres != 1000 && tileSizeMetres != 2000 &&
        tileSizeMetres != 4000 && tileSizeMetres != 8000 &&
        tileSizeMetres != 16000) {
        throw TerrainError(TerrainErrorCode::InvalidArgument,
            "tile size must be 1000, 2000, 4000, 8000, or 16000 metres");
    }
    // BLOCKER 1: Empty selection is allowed in planning. The plan returns the
    // deterministic grid with zero selected tiles and zero provider requests.
    // Only startDownload() requires at least one selected tile.
    // Check for duplicate indices (only if non-empty).
    if (!selectedIndices.empty()) {
        std::vector<std::int32_t> sortedIndices = selectedIndices;
        std::sort(sortedIndices.begin(), sortedIndices.end());
        for (std::size_t i = 1; i < sortedIndices.size(); ++i) {
            if (sortedIndices[i] == sortedIndices[i - 1]) {
                throw TerrainError(TerrainErrorCode::InvalidArgument,
                    "duplicate selected tile index: " + std::to_string(sortedIndices[i]));
            }
        }
    }

    // Compute the deterministic selection grid over the drawn area.
    // BLOCKER 2: computeSelectionGrid enforces limits and throws
    // std::invalid_argument on overflow/oversize; map it to a typed
    // TerrainError so the application layer and protocol carry the
    // SelectionTooLarge code.
    std::vector<domain::terrain::SelectionTile> allTiles;
    try {
        allTiles = domain::terrain::computeSelectionGrid(area, tileSizeMetres);
    } catch (const std::invalid_argument& e) {
        throw TerrainError(TerrainErrorCode::SelectionTooLarge, e.what());
    }

    // Validate selected indices are in range.
    for (std::int32_t idx : selectedIndices) {
        if (idx < 0 || static_cast<std::size_t>(idx) >= allTiles.size()) {
            throw TerrainError(TerrainErrorCode::InvalidArgument,
                "selected tile index out of range: " + std::to_string(idx));
        }
    }

    domain::terrain::DownloadPlan plan;
    plan.providerId = providerId;
    plan.selectionTiles = allTiles;
    plan.totalTileCount = static_cast<std::uint32_t>(allTiles.size());

    // Build the selected tiles list from the indices.
    std::vector<domain::terrain::SelectionTile> selectedTiles;
    for (std::int32_t idx : selectedIndices) {
        selectedTiles.push_back(allTiles[static_cast<std::size_t>(idx)]);
        plan.selectedIndices.push_back(idx);
    }
    plan.selectedTileCount = static_cast<std::uint32_t>(selectedTiles.size());

    // Compute approximate selected area.
    for (const auto& tile : selectedTiles) {
        plan.selectedAreaSqm += tile.areaSqm;
    }

    // Plan provider requests (deduplicated). Empty selection → no requests.
    if (!selectedTiles.empty()) {
        plan.providerRequests = provider->planRequests(selectedTiles);
    }
    plan.requestCount = static_cast<std::uint32_t>(plan.providerRequests.size());
    plan.deduplicatedRequestCount = plan.requestCount; // already deduplicated

    // BLOCKER 2: Validate provider request count against safety limit.
    if (plan.providerRequests.size() > domain::terrain::kMaxTerrainProviderRequests) {
        throw TerrainError(TerrainErrorCode::SelectionTooLarge,
            "provider request count (" + std::to_string(plan.providerRequests.size())
            + ") exceeds maximum (" + std::to_string(domain::terrain::kMaxTerrainProviderRequests) + ")");
    }

    // Estimate total bytes.
    for (const auto& req : plan.providerRequests) {
        plan.estimatedBytes += req.estimatedBytes;
    }

    // Effective resolution: ask the provider (Item 7). The provider owns
    // the resolution computation derived from selected tile coverage.
    // TerrainService must not hard-code Terrarium-specific zoom/pixel/CRS
    // assumptions.
    plan.effectiveResolutionMpp = provider->effectiveResolutionMpp(selectedTiles);

    // BLOCKER 14: Coverage checking — distinguish fully covered, partially
    // covered, and outside. Do not report fullCoverage = true merely because
    // some overlap exists. Only check when there are selected tiles.
    if (!selectedTiles.empty() && !provider->info().coverage.isEmpty()) {
        bool anyOutside = false;
        bool anyPartial = false;
        for (const auto& tile : selectedTiles) {
            const auto& cov = provider->info().coverage;
            // Check if tile is entirely outside coverage.
            if (tile.bounds.east <= cov.west ||
                tile.bounds.west >= cov.east ||
                tile.bounds.north <= cov.south ||
                tile.bounds.south >= cov.north) {
                anyOutside = true;
                continue;
            }
            // Check if tile is fully inside coverage.
            const bool fullyInside =
                tile.bounds.west >= cov.west &&
                tile.bounds.east <= cov.east &&
                tile.bounds.south >= cov.south &&
                tile.bounds.north <= cov.north;
            if (!fullyInside) {
                anyPartial = true;
            }
        }
        if (anyOutside) {
            plan.fullCoverage = false;
            plan.warnings.push_back(
                "selected area is partially outside provider coverage");
        }
        if (anyPartial) {
            plan.fullCoverage = false;
            plan.warnings.push_back(
                "selected area has tiles partially outside provider coverage; "
                "provider overfetch may be required");
        }
    }

    return plan;
}

JobRecord TerrainService::startDownload(
    const std::string& providerId,
    const domain::terrain::GeoBounds& area,
    std::uint32_t tileSizeMetres,
    const std::vector<std::int32_t>& selectedIndices,
    const std::string& displayName) {
    if (!project_.has_value()) {
        throw CommandFailure(CommandFailureCode::ProjectNotOpen, "no project is open");
    }
    // Validate display name (same constraints as local import).
    if (displayName.empty() || displayName.size() > domain::terrain::kMaxTerrainDisplayNameLength) {
        throw TerrainError(TerrainErrorCode::InvalidArgument,
            "terrain display name must be 1.." + std::to_string(domain::terrain::kMaxTerrainDisplayNameLength)
            + " characters");
    }
    if (selectedIndices.empty()) {
        throw TerrainError(TerrainErrorCode::InvalidArgument,
            "no tiles selected for download");
    }

    const domain::terrain::TerrainDownloadProvider* provider = providers_.find(providerId);
    if (!provider) {
        throw TerrainError(TerrainErrorCode::InvalidArgument,
            "unknown terrain provider: " + providerId);
    }

    // Compute the plan (validates inputs).
    domain::terrain::DownloadPlan plan = planDownload(
        providerId, area, tileSizeMetres, selectedIndices);

    // Build the selected tiles list for the worker.
    std::vector<domain::terrain::SelectionTile> selectedTiles;
    for (std::int32_t idx : selectedIndices) {
        selectedTiles.push_back(plan.selectionTiles[static_cast<std::size_t>(idx)]);
    }

    // Capture immutable inputs for the worker (BLOCKER 17: single clean
    // payload type, no duplicate declarations).
    // BLOCKER 7: Snapshot the grid on the executor thread before submitting
    // the job. The worker never accesses world_, project_, datasets_, or
    // mutable store session state.
    auto payload = std::shared_ptr<DownloadPayload>(new DownloadPayload{
        .jobId = "",
        .providerId = providerId,
        .selectedTiles = selectedTiles,
        .requests = plan.providerRequests,
        .displayName = displayName,
        .projectUuid = store_.current().uuid,
        .projectDirectory = store_.current().directory,
        .project = *project_,
        .grid = world_.grid(),
    });

    const std::string label = "terrain.download";

    const auto body = [this, payload](JobContext& context) -> JobSystem::Payload {
        const domain::terrain::TerrainDownloadProvider* provider =
            providers_.find(payload->providerId);
        if (!provider) {
            context.setFailureCode(terrainErrorCodeName(TerrainErrorCode::InvalidArgument));
            failImport(TerrainErrorCode::InvalidArgument,
                "provider not found: " + payload->providerId);
        }

        // RAII guard for temp directory cleanup (BLOCKER 11). The temp dir
        // holds provider tile files; it is removed regardless of how the
        // worker exits (success, failure, cancellation).
        const std::filesystem::path tempDir =
            payload->projectDirectory / "terrain" / "downloads" / runtime::generateUuidV4();
        std::filesystem::create_directories(tempDir);
        TempDirGuard tempGuard(tempDir);

        // BLOCKER 4: Cancellation callback handed to the provider and
        // assembly. Returns true if cancellation was requested; the
        // provider/assembly throws ProviderCancelled/JobCancelled at
        // checkpoints. The same callback is polled by the ixwebsocket
        // progress callback so in-flight HTTP requests abort promptly
        // (BLOCKER 2).
        auto cancel = [&context]() -> bool {
            return context.isCancelled();
        };

        // Phase 1: Planning already done. Report 0-5%.
        context.reportNormalizedProgress(0.0, "Planning download");

        // Phase 2: Provider acquisition (5-55%). Fetch each provider request
        // and decode it to a temporary GeoTIFF. Progress is by request count.
        std::vector<std::filesystem::path> downloadedFiles;
        downloadedFiles.reserve(payload->requests.size());
        const std::uint64_t totalRequests = payload->requests.size();
        const double fetchRange = kDlFetchEnd - kDlFetchStart;

        for (std::size_t i = 0; i < payload->requests.size(); ++i) {
            context.throwIfCancelled();
            const auto& request = payload->requests[i];
            try {
                const std::filesystem::path file =
                    provider->fetchRequest(request, tempDir, "", cancel);
                downloadedFiles.push_back(file);
            } catch (const domain::terrain::ProviderError& error) {
                // BLOCKER 5: Preserve the typed provider error code through
                // the job record and protocol event instead of collapsing
                // everything to SourceUnreadable.
                // Cancellation from the provider must map to JobCancelled
                // (not Failed) so the JobSystem marks it Cancelled.
                if (error.code() == domain::terrain::ProviderErrorCode::Cancelled) {
                    throw JobCancelled{};
                }
                const TerrainErrorCode mapped = mapProviderError(error.code());
                context.setFailureCode(terrainErrorCodeName(mapped));
                throw TerrainError(mapped,
                    "provider request failed: " + std::string(error.what()));
            }
            // Report progress in the fetch phase (5-55%) with real units.
            const double frac = static_cast<double>(i + 1) / static_cast<double>(totalRequests);
            context.reportNormalizedProgress(
                kDlFetchStart + frac * fetchRange,
                "Downloaded " + request.requestId +
                " (" + std::to_string(i + 1) + "/" + std::to_string(totalRequests) + ")");
        }

        context.throwIfCancelled();

        // Phase 3: Decode/validation (55-70%). The provider decodes PNG/raw
        // during fetchRequest; this phase validates that every fetched file
        // is a GDAL-readable GeoTIFF with a CRS.
        const double decodeRange = kDlDecodeEnd - kDlFetchEnd;
        for (std::size_t i = 0; i < downloadedFiles.size(); ++i) {
            context.throwIfCancelled();
            ports::TerrainSourceInfo tileInfo = reader_.probe(downloadedFiles[i]);
            if (tileInfo.crsDefinition.empty()) {
                context.setFailureCode(terrainErrorCodeName(TerrainErrorCode::MissingCrs));
                failImport(TerrainErrorCode::MissingCrs,
                    "provider tile has no CRS: " + downloadedFiles[i].string());
            }
            const double frac = static_cast<double>(i + 1) / static_cast<double>(totalRequests);
            context.reportNormalizedProgress(
                kDlFetchEnd + frac * decodeRange,
                "Validated tile " + std::to_string(i + 1) + "/" + std::to_string(totalRequests));
        }

        // Phase 4: Assembly (70-85%). Mosaic provider GeoTIFFs into a single
        // canonical GeoTIFF using GDAL (BLOCKER 2). Use the same project-
        // relative canonical directory as local import (BLOCKER 3).
        const std::string datasetId = runtime::generateUuidV4();
        const std::filesystem::path canonicalDir =
            payload->projectDirectory / "terrain" / "elevation";
        std::filesystem::create_directories(canonicalDir);

        // Temporary .importing file; commitImportedDataset renames it to
        // the final .tif path on canonical commit success (BLOCKER 3).
        const std::filesystem::path tempRaster =
            canonicalDir / (datasetId + ".tif.importing");

        // RAII guard: if the worker exits via exception after assembly,
        // the .importing file is removed (BLOCKER 11). Released before
        // return since onComplete takes ownership of the file.
        TempDirGuard rasterGuard(tempRaster);

        context.reportNormalizedProgress(kDlAssembleEnd - 0.05, "Assembling raster");

        // Compute the selected coverage in EPSG:3857 (Web Mercator) for
        // clipping the assembled raster to selected application coverage
        // (BLOCKER 2: clip ONLY to selected application coverage).
        std::vector<CoverageRect> selectedCoverage;
        selectedCoverage.reserve(payload->selectedTiles.size());
        for (const auto& tile : payload->selectedTiles) {
            const auto sw = toWebMercatorMeters(tile.bounds.west, tile.bounds.south);
            const auto ne = toWebMercatorMeters(tile.bounds.east, tile.bounds.north);
            selectedCoverage.push_back({sw.x, sw.y, ne.x, ne.y});
        }

        // BLOCKER 3/4: Strip-based assembly with cancellation checkpoints.
        try {
            (void)assembleCanonicalGeoTiff(downloadedFiles, tempRaster,
                selectedCoverage, cancel);
        } catch (const TerrainError&) {
            throw;
        } catch (const std::exception& e) {
            context.setFailureCode(terrainErrorCodeName(TerrainErrorCode::SourceUnreadable));
            throw TerrainError(TerrainErrorCode::SourceUnreadable,
                std::string("raster assembly failed: ") + e.what());
        }

        // Provider tile temp files are no longer needed after assembly.
        // The RAII guard cleans the temp dir on scope exit.
        std::error_code cleanupEc;
        std::filesystem::remove_all(tempDir, cleanupEc);

        context.reportNormalizedProgress(kDlAssembleEnd, "Assembly complete");

        // Phase 5: Canonical validation/hash (85-92%). Probe the assembled
        // GeoTIFF using the canonical GdalTerrainSource to derive REAL
        // metadata (BLOCKER 2). Do NOT fabricate metadata. Then transform
        // coverage from source CRS to project-global via GeoTransformService
        // (same as local import), scan for real elevation range/NoData, and
        // compute SHA-256 provenance.
        context.reportNormalizedProgress(kDlValidateEnd - 0.07, "Validating raster");
        context.throwIfCancelled();

        const ports::TerrainSourceInfo sourceInfo = reader_.probe(tempRaster);
        if (sourceInfo.width <= 0 || sourceInfo.height <= 0 || sourceInfo.crsDefinition.empty()) {
            context.setFailureCode(terrainErrorCodeName(TerrainErrorCode::CorruptSource));
            failImport(TerrainErrorCode::CorruptSource,
                "assembled terrain raster is not a valid raster");
        }

        // BLOCKER 7: Use the snapshotted grid from the payload, not world_.
        auto result = std::shared_ptr<ImportPayload>(new ImportPayload{
            .dataset = domain::terrain::TerrainDataset{},
            .projectUuid = payload->projectUuid,
            .projectDirectory = payload->projectDirectory,
            .tempFile = tempRaster,  // .importing file; commit renames it
            .grid = payload->grid,
            .project = payload->project,
        });

        domain::terrain::TerrainDataset& dataset = result->dataset;
        dataset.id = domain::terrain::entityIdFromUuidText(datasetId);
        dataset.displayName = payload->displayName;
        // Derive metadata from the probe — never fabricated.
        dataset.sourceFormat = sourceInfo.format;
        dataset.sourceCrs = sourceInfo.crsDefinition;
        dataset.rasterWidth = sourceInfo.width;
        dataset.rasterHeight = sourceInfo.height;
        dataset.originX = sourceInfo.originX;
        dataset.originY = sourceInfo.originY;
        dataset.cellSizeX = sourceInfo.pixelSizeX;
        dataset.cellSizeY = sourceInfo.pixelSizeY;
        dataset.horizontalUnitName = sourceInfo.horizontalUnitName;
        dataset.horizontalUnitSymbol = sourceInfo.horizontalUnitSymbol;
        dataset.horizontalUnitIsAngular = sourceInfo.horizontalUnitIsAngular;
        dataset.elevationUnit = sourceInfo.elevationUnit;
        dataset.elevationUnitToMetre = sourceInfo.elevationUnitToMetre;
        dataset.elevationUnitSource = sourceInfo.elevationUnitSource;
        dataset.sampleScale = sourceInfo.sampleScale;
        dataset.sampleOffset = sourceInfo.sampleOffset;
        dataset.hasNodata = sourceInfo.hasNodata;
        dataset.nodataValue = sourceInfo.nodataValue;
        dataset.sourceBytes = sourceInfo.fileBytes;
        dataset.storagePath = "terrain/elevation/" + datasetId + ".tif";
        dataset.sourceAttribution = provider->info().attribution;
        dataset.createdAt = runtime::utcTimestampNow();
        dataset.modifiedAt = dataset.createdAt;

        // Worker-confined transform service (PROJ objects are thread-bound).
        const domain::geo::GeoTransformService workerTransforms;
        const domain::geo::ProjectGeoreference& project = payload->project;
        const domain::geo::SourceSpatialReference sourceSrs{
            .horizontalCrs = dataset.sourceCrs, .verticalCrs = ""};

        // BLOCKER 6: Build canonical coverage pieces (one per selected tile)
        // in project-global coordinates. The enclosing bounds is the union
        // of all pieces. Unselected gaps between selected tiles are NOT in
        // any piece → OutsideCoverage. This is the canonical sparse coverage
        // representation that survives save/reopen.
        dataset.coveragePieces.reserve(payload->selectedTiles.size());
        domain::world::SpatialBounds coverage = domain::world::SpatialBounds::empty();
        for (const auto& tile : payload->selectedTiles) {
            domain::world::SpatialBounds piece = domain::world::SpatialBounds::empty();
            // Transform all four corners of the selected tile to project-global.
            for (std::int64_t corner = 0; corner < 4; ++corner) {
                const double lon = (corner % 2 == 1) ? tile.bounds.east : tile.bounds.west;
                const double lat = (corner / 2 == 1) ? tile.bounds.south : tile.bounds.north;
                const auto wm = toWebMercatorMeters(lon, lat);
                const auto position = workerTransforms.sourceToProjectGlobal(
                    project, sourceSrs,
                    domain::geo::GeoCoordinate{.x = wm.x, .y = wm.y, .z = 0.0});
                if (!domain::geo::isFinite(position)) {
                    context.setFailureCode(terrainErrorCodeName(TerrainErrorCode::InvalidCoverage));
                    failImport(TerrainErrorCode::InvalidCoverage,
                        "downloaded coverage corner transforms to a non-finite canonical position");
                }
                piece.expandTo(position);
                coverage.expandTo(position);
            }
            dataset.coveragePieces.push_back(piece);
        }
        if (coverage.isEmpty()) {
            context.setFailureCode(terrainErrorCodeName(TerrainErrorCode::InvalidCoverage));
            failImport(TerrainErrorCode::InvalidCoverage,
                "downloaded coverage is empty after transform");
        }
        dataset.bounds = coverage;

        // Elevation scan: read the assembled raster in bounded strips and
        // compute the real min/max Z and NoData cell count (same as local
        // import). Progress is by scanned rows.
        const std::uint64_t totalScanRows = static_cast<std::uint64_t>(dataset.rasterHeight);
        bool anyValid = false;
        double minZSource = std::numeric_limits<double>::infinity();
        double maxZSource = -std::numeric_limits<double>::infinity();
        std::uint64_t nodataCells = 0;
        const double scanRange = kDlValidateEnd - kDlAssembleEnd;

        for (std::int64_t row = 0; row < dataset.rasterHeight; row += kElevationScanRows) {
            context.throwIfCancelled();
            const ports::TerrainElevationBlock block = reader_.readBlock(tempRaster, 0, row,
                dataset.rasterWidth,
                std::min<std::int64_t>(kElevationScanRows, dataset.rasterHeight - row));
            for (const double value : block.elevations) {
                if (std::isnan(value)) {
                    ++nodataCells;
                    continue;
                }
                anyValid = true;
                minZSource = std::min(minZSource, value);
                maxZSource = std::max(maxZSource, value);
            }
            const double scanProgress =
                static_cast<double>(std::min(row + kElevationScanRows, dataset.rasterHeight))
                / static_cast<double>(totalScanRows);
            context.reportNormalizedProgress(
                kDlAssembleEnd + scanProgress * scanRange,
                "measuring elevation range (" + std::to_string(row + kElevationScanRows) +
                "/" + std::to_string(totalScanRows) + " rows)");
        }
        if (!anyValid) {
            context.setFailureCode(terrainErrorCodeName(TerrainErrorCode::UnsupportedRaster));
            failImport(TerrainErrorCode::UnsupportedRaster,
                "downloaded raster contains no valid elevation cells");
        }
        dataset.minZ = minZSource * dataset.elevationUnitToMetre / project.linearUnit.toMetre;
        dataset.maxZ = maxZSource * dataset.elevationUnitToMetre / project.linearUnit.toMetre;
        if (nodataCells > 0) {
            dataset.hasNodata = true;
            dataset.diagnostics.push_back({TerrainErrorCode::NodataCells,
                std::to_string(nodataCells) + " NoData cells are excluded from sampling and rendering"});
        }

        // SHA-256 provenance of the assembled raster (same as local import).
        dataset.sourceSha256 = runtime::sha256HexOfFile(tempRaster);

        context.reportNormalizedProgress(kDlValidateEnd, "Validation complete");

        // Phase 6: Canonical commit (92-95%) — handled by the completion
        // handler (commitImportedDataset), which renames the .importing
        // file to the final .tif path and commits the DB row.
        context.reportNormalizedProgress(kDlCommitEnd, "Committing");

        // Release the raster guard: onComplete takes ownership of the file.
        rasterGuard.release();
        return result;
    };

    const auto onProgress = [this](const JobRecord& record) { emitJobUpdate(record); };
    const auto onComplete = [this, payload](const JobOutcome& outcome) {
        untrackJob(outcome.record.jobId);
        if (outcome.record.state != JobState::Completed) {
            std::error_code error;
            if (auto* importPayload = static_cast<ImportPayload*>(outcome.payload.get())) {
                std::filesystem::remove(importPayload->tempFile, error);
            }
            emitJobUpdate(outcome.record);
            return;
        }
        try {
            if (!store_.isOpen() || store_.current().uuid != payload->projectUuid) {
                throw CommandFailure(CommandFailureCode::ProjectNotOpen,
                    "the project closed or changed while the terrain download was running");
            }
            commitImportedDataset(*std::static_pointer_cast<ImportPayload>(outcome.payload));
            jobs_.markCompleted(outcome.record.jobId);
            JobRecord completed = outcome.record;
            completed.state = JobState::Completed;
            emitJobUpdate(completed);
        } catch (const std::exception& error) {
            jobs_.markFailed(outcome.record.jobId,
                std::string{"terrain download commit failed: "} + error.what());
            JobRecord failed = outcome.record;
            failed.state = JobState::Failed;
            failed.message = std::string{"terrain download commit failed: "} + error.what();
            emitJobUpdate(failed);
        }
    };

    const JobRecord record = jobs_.submit(label, "downloading terrain",
        body, onProgress, onComplete, /*requiresFinalization=*/true);
    trackJob(record.jobId);
    return record;
}

} // namespace infraforge::application
