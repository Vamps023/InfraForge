#pragma once

#include "infraforge/domain/terrain/TerrainDownloadProvider.hpp"
#include "infraforge/ports/HttpClient.hpp"

#include <memory>
#include <string>

namespace infraforge::domain::terrain {

// Production terrain DEM provider using AWS Terrain Tiles (Mapzen Terrarium).
//
// Provider details:
//   Endpoint: https://s3.amazonaws.com/elevation-tiles-prod/terrarium/{z}/{x}/{y}.png
//   Encoding: Terrarium PNG (height = (R*256 + G + B/256) - 32768)
//   CRS: Web Mercator (EPSG:3857)
//   Tile size: 256x256 pixels
//   Coverage: Global
//   Authentication: None required (AWS Open Data, S3 public bucket)
//   Attribution: Mapzen + USGS + NASA + other open data sources
//   License: Various open data licenses (see attribution)
//
// The provider fetches PNG tiles via HTTP, decodes Terrarium-encoded
// elevation values, and writes temporary GeoTIFFs with proper CRS and
// geotransform for the canonical ingestion pipeline.
class TerrariumTerrainProvider final : public TerrainDownloadProvider {
public:
    explicit TerrariumTerrainProvider(
        std::shared_ptr<ports::HttpClient> httpClient);

    ~TerrariumTerrainProvider() override = default;

    [[nodiscard]] const ProviderInfo& info() const noexcept override;

    [[nodiscard]] std::vector<ProviderRequest> planRequests(
        const std::vector<SelectionTile>& selectedTiles) const override;

    [[nodiscard]] double effectiveResolutionMpp(
        const GeoBounds& area) const override;

    std::filesystem::path fetchRequest(
        const ProviderRequest& request,
        const std::filesystem::path& tempDir,
        const std::string& credentialHint,
        const CancellationCallback& cancel) const override;

    [[nodiscard]] bool supportsSelectiveRequests() const noexcept override;

private:
    std::shared_ptr<ports::HttpClient> httpClient_;
    ProviderInfo info_;

    // Maximum retry attempts for transient failures.
    static constexpr int kMaxRetries = 3;
    // Base delay for exponential backoff (milliseconds).
    static constexpr int kBaseBackoffMs = 500;
};

} // namespace infraforge::domain::terrain
