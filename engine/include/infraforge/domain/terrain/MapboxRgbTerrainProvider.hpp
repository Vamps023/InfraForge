#pragma once

#include "infraforge/domain/terrain/TerrainDownloadProvider.hpp"
#include "infraforge/ports/HttpClient.hpp"

#include <memory>
#include <string>

namespace infraforge::domain::terrain {

// Production terrain DEM provider using Mapbox Terrain-RGB tiles.
// Endpoint: https://api.mapbox.com/v4/mapbox.terrain-rgb/{z}/{x}/{y}.pngraw?access_token={token}
// Encoding: height = -10000.0 + ((R * 256 * 256 + G * 256 + B) * 0.1)
// CRS: Web Mercator (EPSG:3857)
// Coverage: Global
class MapboxRgbTerrainProvider final : public TerrainDownloadProvider {
public:
    explicit MapboxRgbTerrainProvider(
        std::shared_ptr<ports::HttpClient> httpClient);

    ~MapboxRgbTerrainProvider() override = default;

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
    ProviderInfo info_;
};

} // namespace infraforge::domain::terrain
