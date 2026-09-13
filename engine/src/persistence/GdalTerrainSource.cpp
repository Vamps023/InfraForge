#include "infraforge/persistence/GdalTerrainSource.hpp"

#include "infraforge/domain/terrain/TerrainTypes.hpp"
#include "infraforge/runtime/FileSystemUtf8.hpp"

#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <cpl_conv.h>
#include <cpl_error.h>
#include <cpl_port.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace infraforge::persistence {
namespace {

using domain::terrain::TerrainError;
using domain::terrain::TerrainErrorCode;

[[noreturn]] void throwTerrainError(TerrainErrorCode code, const std::string& message) {
    throw TerrainError(code, message);
}

// GDAL driver registration is idempotent and internally synchronized, but
// it is global process state — run it exactly once.
void ensureGdalRegistered() {
    static std::once_flag registered;
    std::call_once(registered, [] {
        // GDAL's own PROJ context (CRS export in probe) needs proj.db; point
        // it at the same share directory the GeoTransformService uses when
        // the environment supplies no location of its own.
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

// Bounded structural sanity check for TIFF-family sources before they are
// handed to the raster library: a file whose IFD or external values extend
// beyond the file's end makes libtiff dereference out-of-bounds offsets
// (observed as a hard crash), so truncated TIFFs are rejected here — this
// is corrupt-input rejection, not raster parsing; GDAL remains the only
// decoder. Non-TIFF files return immediately and go through GDAL directly.
void tiffStructuralSanityCheck(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    if (!input) {
        throwTerrainError(TerrainErrorCode::SourceUnreadable,
            "terrain source cannot be opened: " + file.string());
    }
    input.seekg(0, std::ios::end);
    const std::streamsize fileSize = input.tellg();
    input.seekg(0, std::ios::beg);

    std::array<unsigned char, 8> header{};
    input.read(reinterpret_cast<char*>(header.data()), 8);
    if (input.gcount() != 8) {
        throwTerrainError(TerrainErrorCode::CorruptSource,
            "terrain source is too small to be a raster: " + file.string());
    }

    bool littleEndian = false;
    if (header[0] == 'I' && header[1] == 'I' && header[2] == 42 && header[3] == 0) {
        littleEndian = true;
    } else if (header[0] == 'M' && header[1] == 'M' && header[2] == 0 && header[3] == 42) {
        littleEndian = false;
    } else {
        return; // not a classical TIFF; GDAL owns the format
    }
    const auto readU16 = [&](const std::uint64_t offset) {
        std::array<unsigned char, 2> bytes{};
        input.clear();
        input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        input.read(reinterpret_cast<char*>(bytes.data()), 2);
        if (input.gcount() != 2) {
            throwTerrainError(TerrainErrorCode::CorruptSource,
                "TIFF directory is truncated: " + file.string());
        }
        return littleEndian
            ? static_cast<std::uint64_t>(bytes[0]) | (static_cast<std::uint64_t>(bytes[1]) << 8)
            : (static_cast<std::uint64_t>(bytes[0]) << 8) | static_cast<std::uint64_t>(bytes[1]);
    };
    const auto readU32 = [&](const std::uint64_t offset) {
        std::array<unsigned char, 4> bytes{};
        input.clear();
        input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        input.read(reinterpret_cast<char*>(bytes.data()), 4);
        if (input.gcount() != 4) {
            throwTerrainError(TerrainErrorCode::CorruptSource,
                "TIFF directory is truncated: " + file.string());
        }
        std::uint64_t value = 0;
        if (littleEndian) {
            for (unsigned i = 0; i < 4; ++i) {
                value |= static_cast<std::uint64_t>(bytes[i]) << (8 * i);
            }
        } else {
            for (unsigned i = 0; i < 4; ++i) {
                value = (value << 8) | static_cast<std::uint64_t>(bytes[i]);
            }
        }
        return value;
    };

    const std::uint64_t ifdOffset = readU32(4);
    if (ifdOffset == 0) {
        return; // no IFD; let GDAL handle/reject
    }
    if (ifdOffset >= static_cast<std::uint64_t>(fileSize)) {
        throwTerrainError(TerrainErrorCode::CorruptSource,
            "TIFF directory offset points beyond the end of the file: " + file.string());
    }
    const std::uint64_t entryCount = readU16(ifdOffset);
    // Safe arithmetic: check for overflow in ifdEnd = ifdOffset + 2 + 12 * entryCount + 4.
    constexpr std::uint64_t kMaxEntries = (std::numeric_limits<std::uint64_t>::max() - 6) / 12;
    if (entryCount > kMaxEntries) {
        throwTerrainError(TerrainErrorCode::CorruptSource,
            "TIFF directory entry count is impossibly large: " + file.string());
    }
    const std::uint64_t ifdEnd = ifdOffset + 2 + 12 * entryCount + 4;
    if (ifdEnd > static_cast<std::uint64_t>(fileSize)) {
        throwTerrainError(TerrainErrorCode::CorruptSource,
            "TIFF directory extends beyond the end of the file: " + file.string());
    }
    for (std::uint64_t entry = 0; entry < entryCount; ++entry) {
        const std::uint64_t entryOffset = ifdOffset + 2 + 12 * entry;
        const std::uint64_t count = readU32(entryOffset + 4);
        // Values larger than 4 bytes live outside the entry; their offset
        // must point inside the file or the tag is corrupt.
        std::uint64_t typeSize = 1;
        switch (readU16(entryOffset + 2)) {
        case 3: typeSize = 2; break; // SHORT
        case 4: typeSize = 4; break; // LONG
        case 16: typeSize = 8; break; // LONG8 (BigTIFF, may appear in classic TIFF)
        case 17: typeSize = 8; break; // SLONG8
        case 1: case 2: case 6: case 7: typeSize = 1; break;
        case 5: case 10: typeSize = 8; break; // RATIONAL / SRATIONAL
        default: typeSize = 1; break;
        }
        // Safe arithmetic: check for overflow in valueBytes = count * typeSize.
        if (count > std::numeric_limits<std::uint64_t>::max() / typeSize) {
            throwTerrainError(TerrainErrorCode::CorruptSource,
                "TIFF tag value size overflows: " + file.string());
        }
        const std::uint64_t valueBytes = count * typeSize;
        if (valueBytes > 4) {
            const std::uint64_t valueOffset = readU32(entryOffset + 8);
            // Safe arithmetic: check for overflow in valueOffset + valueBytes.
            if (valueOffset > std::numeric_limits<std::uint64_t>::max() - valueBytes) {
                throwTerrainError(TerrainErrorCode::CorruptSource,
                    "TIFF tag value range overflows: " + file.string());
            }
            if (valueOffset + valueBytes > static_cast<std::uint64_t>(fileSize)) {
                throwTerrainError(TerrainErrorCode::CorruptSource,
                    "TIFF tag values extend beyond the end of the file: " + file.string());
            }
        }
    }
}

[[nodiscard]] std::string trimmed(std::string_view text) {
    const auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && isSpace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    while (end > begin && isSpace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return std::string{text.substr(begin, end - begin)};
}

// Normalizes the raster band's unit string. An absent unit means metres —
// the documented convention for DEMs without vertical metadata (the same
// convention the Geo domain applies to untyped source heights). A unit we
// cannot map is an explicit failure, never a silent reinterpretation.
struct ElevationUnit {
    std::string name;
    double toMetre{1.0};
};

[[nodiscard]] ElevationUnit normalizeElevationUnit(const char* rawUnit) {
    const std::string unit = trimmed(rawUnit != nullptr ? std::string_view{rawUnit} : std::string_view{});
    if (unit.empty() || unit == "m" || unit == "meter" || unit == "metre"
        || unit == "meters" || unit == "metres") {
        return {.name = "metre", .toMetre = 1.0};
    }
    if (unit == "ft" || unit == "foot" || unit == "feet" || unit == "international_foot") {
        return {.name = "international foot", .toMetre = 0.3048};
    }
    if (unit == "us-ft" || unit == "us_foot" || unit == "US survey foot") {
        return {.name = "US survey foot", .toMetre = 1200.0 / 3937.0};
    }
    throwTerrainError(TerrainErrorCode::UnsupportedRaster,
        "raster elevation unit \"" + unit + "\" is not a supported unit of measure");
}

[[nodiscard]] bool isSupportedSampleType(GDALDataType type) {
    switch (type) {
    case GDT_Byte:
    case GDT_Int8:
    case GDT_UInt16:
    case GDT_Int16:
    case GDT_UInt32:
    case GDT_Int32:
    case GDT_UInt64:
    case GDT_Int64:
    case GDT_Float16:
    case GDT_Float32:
    case GDT_Float64:
        return true;
    default:
        // Complex sample types have no DEM elevation semantics.
        return false;
    }
}

[[nodiscard]] std::string sampleTypeName(GDALDataType type) {
    return GDALGetDataTypeName(type);
}

// Resolves the source CRS definition text: authority form when the CRS
// carries an authority code, WKT2 otherwise. The definition is later
// resolved through the single Geo transform service (ADR-0007), never
// interpreted here.
[[nodiscard]] std::string crsDefinitionText(const OGRSpatialReference& srs) {
    const char* authority = srs.GetAuthorityName(nullptr);
    const char* code = srs.GetAuthorityCode(nullptr);
    if (authority != nullptr && code != nullptr && *authority != '\0' && *code != '\0') {
        return std::string{authority} + ":" + code;
    }
    char* wkt = nullptr;
    const char* wktOptions[] = {"FORMAT=WKT2_2019", nullptr};
    const OGRErr exported = srs.exportToWkt(&wkt, wktOptions);
    if (exported != OGRERR_NONE || wkt == nullptr) {
        CPLFree(wkt);
        throwTerrainError(TerrainErrorCode::UnsupportedRaster,
            "raster CRS is present but cannot be exported for resolution");
    }
    std::string definition{wkt};
    CPLFree(wkt);
    return definition;
}

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
// The structured-exception guard exists because malformed TIFFs have been
// observed to crash inside libtiff's directory reader instead of failing
// cleanly. The guarded helper must stay free of C++ objects with
// destructors (MSVC forbids mixing them with __try).
GDALDataset* openGdalDatasetDirect(const char* pathText) {
    return GDALDataset::Open(
        pathText, GDAL_OF_RASTER | GDAL_OF_VERBOSE_ERROR, nullptr, nullptr, nullptr);
}

struct GdalOpenAttempt {
    GDALDataset* dataset{nullptr};
    unsigned long exceptionCode{0};
};

GdalOpenAttempt openGdalDatasetGuarded(const char* pathText) {
    GdalOpenAttempt attempt;
    __try {
        attempt.dataset = openGdalDatasetDirect(pathText);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        attempt.dataset = nullptr;
        attempt.exceptionCode = GetExceptionCode();
    }
    return attempt;
}
#endif

[[nodiscard]] GDALDataset* openRaster(const std::filesystem::path& file) {
    ensureGdalRegistered();
    const std::string pathText = runtime::utf8String(file);
    tiffStructuralSanityCheck(file);
#ifdef _WIN32
    const GdalOpenAttempt attempt = openGdalDatasetGuarded(pathText.c_str());
    GDALDataset* dataset = attempt.dataset;
#else
    GDALDataset* dataset = GDALDataset::Open(
        pathText.c_str(), GDAL_OF_RASTER | GDAL_OF_VERBOSE_ERROR, nullptr, nullptr, nullptr);
#endif
    if (dataset == nullptr) {
#ifdef _WIN32
        if (attempt.exceptionCode != 0) {
            throwTerrainError(TerrainErrorCode::CorruptSource,
                "raster crashed the format reader and is treated as corrupt: " + pathText);
        }
#endif
        const char* detail = CPLGetLastErrorMsg();
        throwTerrainError(TerrainErrorCode::CorruptSource,
            "raster could not be opened: " + pathText
                + (detail != nullptr && *detail != '\0' ? " (" + std::string{detail} + ")" : ""));
    }
    return dataset;
}

} // namespace

ports::TerrainSourceInfo GdalTerrainSource::probe(const std::filesystem::path& file) const {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec)) {
        throwTerrainError(TerrainErrorCode::SourceUnreadable,
            "terrain source is not a readable file: " + file.string());
    }

    GDALDataset* dataset = openRaster(file);
    struct DatasetGuard {
        GDALDataset* dataset;
        ~DatasetGuard() { if (dataset != nullptr) GDALClose(dataset); }
    } guard{dataset};

    ports::TerrainSourceInfo info;
    info.format = dataset->GetDriverName();
    info.width = dataset->GetRasterXSize();
    info.height = dataset->GetRasterYSize();
    if (info.width <= 0 || info.height <= 0) {
        throwTerrainError(TerrainErrorCode::UnsupportedRaster, "raster dimensions must be positive");
    }

    if (dataset->GetRasterCount() < 1) {
        throwTerrainError(TerrainErrorCode::UnsupportedRaster, "raster has no sample bands");
    }
    GDALRasterBand* band = dataset->GetRasterBand(1);
    if (!isSupportedSampleType(band->GetRasterDataType())) {
        throwTerrainError(TerrainErrorCode::UnsupportedRaster,
            "raster sample type " + sampleTypeName(band->GetRasterDataType())
                + " is not a supported elevation sample type");
    }
    info.sampleTypeName = sampleTypeName(band->GetRasterDataType());

    double geotransform[6]{0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
    if (dataset->GetGeoTransform(geotransform) != CE_None) {
        throwTerrainError(TerrainErrorCode::MissingCrs,
            "raster carries no geotransform: source coverage cannot be located");
    }
    if (geotransform[2] != 0.0 || geotransform[4] != 0.0) {
        throwTerrainError(TerrainErrorCode::UnsupportedRaster,
            "rotated geotransforms are not supported; only north-up rasters are accepted");
    }
    if (geotransform[5] >= 0.0) {
        throwTerrainError(TerrainErrorCode::UnsupportedRaster,
            "raster is not north-up (positive y row step)");
    }
    if (geotransform[1] <= 0.0) {
        throwTerrainError(TerrainErrorCode::UnsupportedRaster,
            "raster is not east-up (non-positive x pixel step); only north-up east-up rasters are accepted");
    }
    if (!std::isfinite(geotransform[0]) || !std::isfinite(geotransform[3])
        || !(geotransform[1] > 0.0) || !(geotransform[5] < 0.0)
        || !std::isfinite(geotransform[1]) || !std::isfinite(geotransform[5])) {
        throwTerrainError(TerrainErrorCode::UnsupportedRaster, "raster geotransform is degenerate");
    }
    info.originX = geotransform[0];
    info.originY = geotransform[3];
    info.pixelSizeX = geotransform[1];
    info.pixelSizeY = -geotransform[5];

    const OGRSpatialReference* spatialRef = dataset->GetSpatialRef();
    if (spatialRef == nullptr || spatialRef->IsEmpty() != 0) {
        throwTerrainError(TerrainErrorCode::MissingCrs,
            "raster declares no CRS; a missing CRS is never assumed (ADR-0007)");
    }
    info.crsDefinition = crsDefinitionText(*spatialRef);

    const ElevationUnit unit = normalizeElevationUnit(band->GetUnitType());
    info.elevationUnit = unit.name;
    info.elevationUnitToMetre = unit.toMetre;

    int hasNodata = FALSE;
    const double nodata = band->GetNoDataValue(&hasNodata);
    info.hasNodata = hasNodata == TRUE;
    info.nodataValue = nodata;

    std::error_code sizeError;
    const auto fileBytes = std::filesystem::file_size(file, sizeError);
    info.fileBytes = sizeError ? 0 : static_cast<std::uint64_t>(fileBytes);
    return info;
}

ports::TerrainElevationBlock GdalTerrainSource::readBlock(
    const std::filesystem::path& file, std::int64_t offsetX, std::int64_t offsetY,
    std::int64_t width, std::int64_t height) const {
    GDALDataset* dataset = openRaster(file);
    struct DatasetGuard {
        GDALDataset* dataset;
        ~DatasetGuard() { if (dataset != nullptr) GDALClose(dataset); }
    } guard{dataset};

    GDALRasterBand* band = dataset->GetRasterBand(1);
    if (band == nullptr) {
        throwTerrainError(TerrainErrorCode::CorruptSource, "raster lost its sample band");
    }

    // Clamp to the raster so edge windows never read out of bounds.
    const std::int64_t rasterWidth = dataset->GetRasterXSize();
    const std::int64_t rasterHeight = dataset->GetRasterYSize();
    std::int64_t x = std::clamp(offsetX, std::int64_t{0}, std::max(std::int64_t{0}, rasterWidth - 1));
    std::int64_t y = std::clamp(offsetY, std::int64_t{0}, std::max(std::int64_t{0}, rasterHeight - 1));
    std::int64_t w = std::clamp(width, std::int64_t{1}, rasterWidth - x);
    std::int64_t h = std::clamp(height, std::int64_t{1}, rasterHeight - y);

    ports::TerrainElevationBlock block;
    block.offsetX = x;
    block.offsetY = y;
    block.width = w;
    block.height = h;
    block.elevations.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0.0);

    const CPLErr status = band->RasterIO(
        GF_Read, static_cast<int>(x), static_cast<int>(y), static_cast<int>(w), static_cast<int>(h),
        block.elevations.data(), static_cast<int>(w), static_cast<int>(h), GDT_Float64, 0, 0, nullptr);
    if (status != CE_None) {
        const char* detail = CPLGetLastErrorMsg();
        throwTerrainError(TerrainErrorCode::CorruptSource,
            "raster read failed at offset (" + std::to_string(x) + ", " + std::to_string(y) + ")"
                + (detail != nullptr && *detail != '\0' ? ": " + std::string{detail} : ""));
    }

    int hasNodata = FALSE;
    const double nodata = band->GetNoDataValue(&hasNodata);
    if (hasNodata == TRUE) {
        for (double& value : block.elevations) {
            if (value == nodata) {
                value = std::numeric_limits<double>::quiet_NaN();
            }
        }
    }
    return block;
}

} // namespace infraforge::persistence
