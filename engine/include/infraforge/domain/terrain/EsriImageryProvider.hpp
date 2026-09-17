#pragma once

#include "infraforge/domain/terrain/TerrainDownloadProvider.hpp"
#include "infraforge/ports/HttpClient.hpp"

#include <memory>
#include <string>

namespace infraforge::domain::terrain {

// Provider for downloading high-resolution satellite imagery (Esri World Imagery).
// Used for draping orthophoto albedo onto 3D terrain and exporting textured terrain packages.
class EsriImageryProvider final {
public:
    explicit EsriImageryProvider(std::shared_ptr<ports::HttpClient> httpClient);
    ~EsriImageryProvider() = default;

    [[nodiscard]] const ProviderInfo& info() const noexcept;

    // Download satellite imagery covering the given geographic bounds and composite
    // it into a georeferenced GeoTIFF RGB image in tempDir.
    std::filesystem::path fetchImageryForBounds(
        const GeoBounds& bounds,
        const std::filesystem::path& tempDir,
        const CancellationCallback& cancel = nullptr) const;

private:
    std::shared_ptr<ports::HttpClient> httpClient_;
    ProviderInfo info_;
};

} // namespace infraforge::domain::terrain
