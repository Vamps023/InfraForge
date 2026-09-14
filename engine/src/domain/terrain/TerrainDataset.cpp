#include "infraforge/domain/terrain/TerrainDataset.hpp"

#include "infraforge/domain/terrain/TerrainTypes.hpp"
#include "infraforge/runtime/Uuid.hpp"

#include <array>
#include <cmath>
#include <string_view>

namespace infraforge::domain::terrain {
namespace {

std::string_view errorCodeName(TerrainErrorCode code) noexcept {
    switch (code) {
    case TerrainErrorCode::SourceUnreadable:
        return "source_unreadable";
    case TerrainErrorCode::CorruptSource:
        return "corrupt_source";
    case TerrainErrorCode::MissingCrs:
        return "missing_crs";
    case TerrainErrorCode::UnsupportedRaster:
        return "unsupported_raster";
    case TerrainErrorCode::InvalidCoverage:
        return "invalid_coverage";
    case TerrainErrorCode::SourceDataMissing:
        return "source_data_missing";
    case TerrainErrorCode::NodataCells:
        return "nodata_cells";
    case TerrainErrorCode::TileGenerationFailed:
        return "tile_generation_failed";
    case TerrainErrorCode::InvalidArgument:
        return "invalid_argument";
    case TerrainErrorCode::ProviderAuthenticationFailed:
        return "provider_authentication_failed";
    case TerrainErrorCode::ProviderRateLimited:
        return "provider_rate_limited";
    case TerrainErrorCode::ProviderNetworkTimeout:
        return "provider_network_timeout";
    case TerrainErrorCode::ProviderUnavailable:
        return "provider_unavailable";
    case TerrainErrorCode::ProviderUnsupportedCoverage:
        return "provider_unsupported_coverage";
    case TerrainErrorCode::ProviderInvalidResponse:
        return "provider_invalid_response";
    case TerrainErrorCode::ProviderCorruptResponse:
        return "provider_corrupt_response";
    case TerrainErrorCode::SelectionTooLarge:
        return "selection_too_large";
    }
    return "unknown";
}

} // namespace

std::string_view terrainErrorCodeName(TerrainErrorCode code) noexcept {
    return errorCodeName(code);
}

std::optional<TerrainErrorCode> terrainErrorCodeFromName(std::string_view name) noexcept {
    for (std::size_t index = 0; index <= static_cast<std::size_t>(TerrainErrorCode::InvalidArgument);
         ++index) {
        const auto code = static_cast<TerrainErrorCode>(index);
        if (errorCodeName(code) == name) {
            return code;
        }
    }
    return std::nullopt;
}

std::optional<std::string> validateTerrainDataset(const TerrainDataset& dataset) {
    if (dataset.id.isNull()) {
        return std::string{"terrain dataset id is null"};
    }
    if (const std::string uuidText = uuidTextFromEntityId(dataset.id); !runtime::isValidUuidText(uuidText)) {
        return std::string{"terrain dataset id is not uuid text"};
    }
    if (dataset.displayName.empty() || dataset.displayName.size() > kMaxTerrainDisplayNameLength) {
        return std::string{"terrain dataset display name must be 1.."
            + std::to_string(kMaxTerrainDisplayNameLength) + " characters"};
    }
    if (dataset.storagePath.empty() || dataset.storagePath.starts_with('/')
        || dataset.storagePath.contains("..")) {
        return std::string{"terrain storage path must be a relative project path without '..'"};
    }
    if (dataset.sourceFormat.empty()) {
        return std::string{"terrain source format is empty"};
    }
    if (dataset.sourceCrs.empty()) {
        return std::string{"terrain source CRS is empty — a missing CRS never reaches canonical state"};
    }
    if (dataset.rasterWidth <= 0 || dataset.rasterHeight <= 0) {
        return std::string{"terrain raster dimensions must be positive"};
    }
    if (!(dataset.cellSizeX > 0.0) || !(dataset.cellSizeY > 0.0)
        || !std::isfinite(dataset.cellSizeX) || !std::isfinite(dataset.cellSizeY)) {
        return std::string{"terrain cell size must be finite and positive"};
    }
    if (!std::isfinite(dataset.originX) || !std::isfinite(dataset.originY)) {
        return std::string{"terrain raster origin must be finite"};
    }
    if (dataset.elevationUnit.empty() || !(dataset.elevationUnitToMetre > 0.0)
        || !std::isfinite(dataset.elevationUnitToMetre)) {
        return std::string{"terrain elevation unit must resolve to a positive metre factor"};
    }
    if (!dataset.bounds.isFinite()) {
        return std::string{"terrain canonical bounds must be finite"};
    }
    if (dataset.bounds.isEmpty()) {
        return std::string{"terrain canonical bounds must not be empty"};
    }
    // Coverage pieces validation (BLOCKER 6): if present, each must be
    // finite and non-empty, and the count must be within bounds.
    if (dataset.coveragePieces.size() > kMaxCoveragePieces) {
        return std::string{"terrain coverage piece count exceeds maximum"};
    }
    for (const auto& piece : dataset.coveragePieces) {
        if (!piece.isFinite() || piece.isEmpty()) {
            return std::string{"terrain coverage piece must be finite and non-empty"};
        }
    }
    // If coverage pieces exist, the enclosing bounds must contain all of them.
    for (const auto& piece : dataset.coveragePieces) {
        if (!dataset.bounds.contains(piece)) {
            return std::string{"terrain coverage piece is outside the enclosing bounds"};
        }
    }
    if (dataset.sourceSha256.size() != 64) {
        return std::string{"terrain source digest must be 64 hex characters"};
    }
    if (dataset.sourceBytes == 0) {
        return std::string{"terrain source storage must not be empty"};
    }
    if (dataset.revision < 1) {
        return std::string{"terrain dataset revision must be >= 1"};
    }
    return std::nullopt;
}

world::EntityId entityIdFromUuidText(std::string_view uuidText) {
    const std::array<std::uint8_t, 16> bytes = runtime::uuidTextToBytes(uuidText);
    std::uint64_t high = 0;
    std::uint64_t low = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        high = (high << 8) | bytes[i];
    }
    for (std::size_t i = 8; i < 16; ++i) {
        low = (low << 8) | bytes[i];
    }
    return world::EntityId{.high = high, .low = low};
}

std::string uuidTextFromEntityId(world::EntityId id) {
    std::array<std::uint8_t, 16> bytes{};
    for (std::size_t i = 0; i < 8; ++i) {
        bytes[i] = static_cast<std::uint8_t>(id.high >> (56 - 8 * i));
    }
    for (std::size_t i = 0; i < 8; ++i) {
        bytes[8 + i] = static_cast<std::uint8_t>(id.low >> (56 - 8 * i));
    }
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string text;
    text.reserve(36);
    for (std::size_t i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            text.push_back('-');
        }
        text.push_back(kDigits[bytes[i] >> 4]);
        text.push_back(kDigits[bytes[i] & 0x0f]);
    }
    return text;
}

} // namespace infraforge::domain::terrain
