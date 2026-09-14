#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace infraforge::domain::world {
struct ChunkCoord;
}

namespace infraforge::domain::terrain {

// Terrain-domain failure taxonomy (docs/05_DOMAINS/TERRAIN.md). CRS and
// coordinate-transform failures are not duplicated here: a source CRS that
// exists but cannot be resolved, or a transform the geospatial engine
// cannot build, surfaces through the Geo domain's GeoError (ADR-0007).
// Everything raster-specific is typed here and never swallowed.
enum class TerrainErrorCode : std::uint8_t {
    // Path does not exist / cannot be opened as a raster.
    SourceUnreadable,
    // Raster opened but is truncated or violates its own format (GDAL
    // consistency check failure).
    CorruptSource,
    // Raster declares no CRS. Never silently assumed (ADR-0007).
    MissingCrs,
    // Raster configuration the importer does not support: unsupported
    // sample type, rotated geotransform, non-first-band DEM, unknown
    // elevation unit, degenerate dimensions.
    UnsupportedRaster,
    // Coverage bounds could not be produced as finite canonical values.
    InvalidCoverage,
    // Project-owned raster storage is missing or failed its integrity check.
    SourceDataMissing,
    // The raster contains NoData cells (informational, never fatal — the
    // cells are excluded from sampling and rendering).
    NodataCells,
    SuspiciousEncoding,
    // Derived tile generation failed or produced an invalid tile.
    TileGenerationFailed,
    // Malformed command argument (empty name, unknown dataset, ...).
    InvalidArgument,
    // ---- Remote provider failure taxonomy (BLOCKER 5) ----
    // Provider authentication failed (HTTP 401/403).
    ProviderAuthenticationFailed,
    // Provider rate-limited the request (HTTP 429).
    ProviderRateLimited,
    // Network timeout while contacting the provider.
    ProviderNetworkTimeout,
    // Provider source is unavailable (HTTP 5xx, DNS, connection refused).
    ProviderUnavailable,
    // Provider does not cover the requested area.
    ProviderUnsupportedCoverage,
    // Provider returned an invalid/malformed response (bad PNG, wrong bands).
    ProviderInvalidResponse,
    // Provider response was corrupt (decoded but inconsistent).
    ProviderCorruptResponse,
    // Selection grid or provider request count exceeds safety limits.
    SelectionTooLarge,
};

[[nodiscard]] std::string_view terrainErrorCodeName(TerrainErrorCode code) noexcept;
[[nodiscard]] std::optional<TerrainErrorCode> terrainErrorCodeFromName(std::string_view name) noexcept;

class TerrainError : public std::runtime_error {
public:
    TerrainError(TerrainErrorCode code, std::string message)
        : std::runtime_error(std::move(message)),
          code_(code) {}

    [[nodiscard]] TerrainErrorCode code() const noexcept { return code_; }

private:
    TerrainErrorCode code_;
};

// A sampling outcome in canonical project-global space. NoData is a
// first-class result, not an error and not a silently substituted height.
enum class TerrainSampleStatus : std::uint8_t {
    Height,
    NoData,
    OutsideCoverage,
};

struct TerrainSample {
    TerrainSampleStatus status{TerrainSampleStatus::OutsideCoverage};
    // Canonical linear-unit elevation; meaningful only for Height.
    double height{0.0};
};

// Identifies one derived terrain tile: the world chunk it covers plus the
// render LOD level inside that chunk. Tile identity is derived state —
// never canonical identity (ADR-0008: chunk membership is not identity).
struct TerrainTileKey {
    std::int64_t chunkX{0};
    std::int64_t chunkY{0};
    std::uint32_t lod{0};

    friend bool operator==(const TerrainTileKey&, const TerrainTileKey&) = default;
    // Deterministic row-major, then LOD ordering (cache iteration order).
    [[nodiscard]] friend bool operator<(const TerrainTileKey& a, const TerrainTileKey& b) noexcept {
        if (a.chunkX != b.chunkX) {
            return a.chunkX < b.chunkX;
        }
        if (a.chunkY != b.chunkY) {
            return a.chunkY < b.chunkY;
        }
        return a.lod < b.lod;
    }
};

} // namespace infraforge::domain::terrain
