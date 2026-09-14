#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace infraforge::domain::terrain {

// Cancellation callback handed to provider fetch operations (BLOCKER 4).
// The provider calls this at bounded intervals during HTTP, decode, and
// raster write. If cancellation was requested, it throws a
// ProviderCancelled exception (or any exception the caller maps to
// cancellation). This keeps JobContext out of the domain/provider layer.
struct ProviderCancelled : std::runtime_error {
    ProviderCancelled() : std::runtime_error("provider operation cancelled") {}
};

using CancellationCallback = std::function<void()>;

// Safety limits for terrain selection and provider requests (BLOCKER 2).
// These are authoritative backend limits; the frontend may warn earlier
// but the backend always enforces them before allocation.
inline constexpr std::uint32_t kMaxTerrainSelectionTiles = 10000;
inline constexpr std::uint32_t kMaxTerrainProviderRequests = 5000;
// Maximum output raster pixel count for canonical assembly (BLOCKER 3).
// 100M pixels * 4 bytes/pixel (Float32) = 400 MB peak for a single strip
// buffer; strip processing keeps actual memory far below this.
inline constexpr std::int64_t kMaxCanonicalRasterPixels = 100'000'000;
// Strip height for bounded-memory raster assembly (BLOCKER 3).
inline constexpr std::int64_t kAssemblyStripRows = 256;

// Geographic bounds in WGS84 (EPSG:4326) degrees.
struct GeoBounds {
    double west{0.0};
    double south{0.0};
    double east{0.0};
    double north{0.0};

    [[nodiscard]] constexpr bool isEmpty() const noexcept {
        return east <= west || north <= south;
    }
    [[nodiscard]] constexpr double widthDeg() const noexcept { return east - west; }
    [[nodiscard]] constexpr double heightDeg() const noexcept { return north - south; }
};

// Metadata about a terrain DEM provider, for listing and selection.
struct ProviderInfo {
    std::string providerId;
    std::string displayName;
    std::string attribution;
    bool requiresAuth{false};
    double maxResolutionMpp{0.0};
    GeoBounds coverage; // Empty = global
};

// One application-level terrain selection tile (user-facing project area
// unit, e.g. 4 km x 4 km). Distinct from provider request tiles.
struct SelectionTile {
    std::int32_t col{0};
    std::int32_t row{0};
    GeoBounds bounds;
    double areaSqm{0.0};
};

// One provider request needed to cover selected application tiles.
struct ProviderRequest {
    std::string requestId;
    GeoBounds bounds;
    std::uint64_t estimatedBytes{0};
};

// Typed provider failure classification.
enum class ProviderErrorCode : std::uint8_t {
    AuthenticationFailed,
    RateLimited,
    SourceUnavailable,
    NetworkTimeout,
    InvalidProviderResponse,
    UnsupportedCoverage,
    CorruptTerrainResponse,
    Cancelled,
};

class ProviderError : public std::runtime_error {
public:
    ProviderError(ProviderErrorCode code, const std::string& message)
        : std::runtime_error(message), code_(code) {}
    [[nodiscard]] ProviderErrorCode code() const noexcept { return code_; }
private:
    ProviderErrorCode code_;
};

// Deterministic download plan produced before any network acquisition.
struct DownloadPlan {
    std::string providerId;
    std::vector<SelectionTile> selectionTiles;
    std::vector<std::int32_t> selectedIndices;
    std::vector<ProviderRequest> providerRequests;
    std::uint32_t requestCount{0};
    std::uint32_t deduplicatedRequestCount{0};
    double effectiveResolutionMpp{0.0};
    std::uint64_t estimatedBytes{0};
    std::vector<std::string> warnings;
    bool fullCoverage{true};
    std::uint32_t totalTileCount{0};
    std::uint32_t selectedTileCount{0};
    double selectedAreaSqm{0.0};
};

// Port interface for a terrain DEM provider adapter. The engine owns
// provider planning, network acquisition, decoding, and canonical raster
// assembly. Implementations must not hard-code credentials or log secrets.
//
// A provider adapter owns:
// - provider id/name
// - coverage
// - native CRS/scheme
// - resolution/zoom rules
// - deterministic request construction
// - authentication requirements
// - response decoding/elevation semantics
// - attribution/provenance metadata
// - retryable vs permanent failure classification
class TerrainDownloadProvider {
public:
    virtual ~TerrainDownloadProvider() = default;

    [[nodiscard]] virtual const ProviderInfo& info() const noexcept = 0;

    // Compute the unique provider requests needed to cover the selected
    // application tiles. Deduplicates requests shared by adjacent tiles.
    // Does NOT perform any network I/O.
    [[nodiscard]] virtual std::vector<ProviderRequest> planRequests(
        const std::vector<SelectionTile>& selectedTiles) const = 0;

    // Fetch one provider request and write the decoded elevation raster to
    // a temporary file. Returns the path to the decoded GeoTIFF.
    // Throws ProviderError on failure. The cancellation callback is invoked
    // at bounded intervals (BLOCKER 4); if cancelled, it should throw
    // ProviderCancelled or a ProviderError(Cancelled).
    virtual std::filesystem::path fetchRequest(
        const ProviderRequest& request,
        const std::filesystem::path& tempDir,
        const std::string& credentialHint,
        const CancellationCallback& cancel) const = 0;

    // Whether this provider supports selective (per-tile) requests, or only
    // bounding-box requests. If bounding-box only, planRequests may group
    // conservatively and the plan must surface unavoidable overfetch.
    [[nodiscard]] virtual bool supportsSelectiveRequests() const noexcept = 0;
};

// Registry of available providers. The engine queries this to list sources
// and to resolve a provider by id for planning/download.
class TerrainProviderRegistry {
public:
    void registerProvider(std::unique_ptr<TerrainDownloadProvider> provider);
    [[nodiscard]] std::vector<ProviderInfo> listProviders() const;
    [[nodiscard]] const TerrainDownloadProvider* find(const std::string& providerId) const;
private:
    std::vector<std::unique_ptr<TerrainDownloadProvider>> providers_;
};

// Compute a deterministic application selection grid over a geographic area.
// tileSizeMetres must be one of 1000, 2000, 4000, 8000, 16000.
// The grid is computed in a projected metric space (WebMercator approximation
// for area calculation) and bounds are in WGS84 degrees.
[[nodiscard]] std::vector<SelectionTile> computeSelectionGrid(
    const GeoBounds& area, std::uint32_t tileSizeMetres);

} // namespace infraforge::domain::terrain
