#pragma once

#include "infraforge/domain/terrain/TerrainDownloadProvider.hpp"
#include "infraforge/ports/HttpClient.hpp"

#include <memory>
#include <string>

namespace infraforge::domain::terrain {

// Provider adapter for OpenTopography Global DEM REST API.
// Queries portal.opentopography.org for SRTM, Copernicus GLO-30, ALOS World 3D,
// NASADEM, or USGS 3DEP rasters in native GeoTIFF format.
class OpenTopoTerrainProvider final : public TerrainDownloadProvider {
public:
    explicit OpenTopoTerrainProvider(
        std::shared_ptr<ports::HttpClient> httpClient,
        std::string demType = "SRTMGL1",
        std::string providerId = "opentopo-srtm30",
        std::string displayName = "OpenTopography SRTM GL1 30m");

    ~OpenTopoTerrainProvider() override = default;

    [[nodiscard]] const ProviderInfo& info() const noexcept override;

    [[nodiscard]] std::vector<ProviderRequest> planRequests(
        const std::vector<SelectionTile>& selectedTiles) const override;

    [[nodiscard]] double effectiveResolutionMpp(
        const std::vector<SelectionTile>& selectedTiles) const override;

    std::filesystem::path fetchRequest(
        const ProviderRequest& request,
        const std::filesystem::path& tempDir,
        const std::string& credentialHint,
        const CancellationCallback& cancel) const override;

    [[nodiscard]] bool supportsSelectiveRequests() const noexcept override;

private:
    std::shared_ptr<ports::HttpClient> httpClient_;
    std::string demType_;
    ProviderInfo info_;
};

} // namespace infraforge::domain::terrain
