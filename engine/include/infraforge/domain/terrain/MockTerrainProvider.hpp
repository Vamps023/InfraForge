#pragma once

#include "infraforge/domain/terrain/TerrainDownloadProvider.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace infraforge::domain::terrain {

// Deterministic in-process mock provider for unit tests. Does NOT make any
// network requests. Generates a synthetic GeoTIFF-like raster file with
// deterministic elevation values based on the request bounds. This is for
// testing the download workflow (planning, dedup, cancellation, progress)
// without relying on public internet access.
//
// The mock provider uses a simple XYZ tile scheme at zoom level 10, which
// gives 1024x1024 WebMercator tiles. Each selected application tile maps to
// one or more provider tiles depending on size.
class MockTerrainProvider final : public TerrainDownloadProvider {
public:
    explicit MockTerrainProvider(std::uint32_t zoom = 10) : zoom_(zoom) {
        info_.providerId = "mock-terrain";
        info_.displayName = "Mock Terrain (test)";
        info_.attribution = "Test data — not for production";
        info_.requiresAuth = false;
        info_.maxResolutionMpp = 150.0;
        info_.coverage = {}; // global
    }

    [[nodiscard]] const ProviderInfo& info() const noexcept override { return info_; }

    [[nodiscard]] std::vector<ProviderRequest> planRequests(
        const std::vector<SelectionTile>& selectedTiles) const override;

    std::filesystem::path fetchRequest(
        const ProviderRequest& request,
        const std::filesystem::path& tempDir,
        const std::string& credentialHint,
        const CancellationCallback& cancel) const override;

    [[nodiscard]] bool supportsSelectiveRequests() const noexcept override { return true; }

    // Test hook: set to true to make fetchRequest fail with a specific error.
    void setFailMode(ProviderErrorCode code) { failMode_ = code; }
    [[nodiscard]] bool shouldFail() const noexcept { return failMode_.has_value(); }

private:
    ProviderInfo info_{};
    std::uint32_t zoom_{10};
    std::optional<ProviderErrorCode> failMode_;

    // Compute the XYZ tile range covering the given bounds at zoom_.
    [[nodiscard]] std::pair<std::int32_t, std::int32_t> lonToTileXRange(
        double west, double east) const;
    [[nodiscard]] std::pair<std::int32_t, std::int32_t> latToTileYRange(
        double south, double north) const;
};

} // namespace infraforge::domain::terrain
