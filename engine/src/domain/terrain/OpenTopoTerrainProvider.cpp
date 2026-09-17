#include "infraforge/domain/terrain/OpenTopoTerrainProvider.hpp"
#include "infraforge/runtime/Logging.hpp"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace infraforge::domain::terrain {

OpenTopoTerrainProvider::OpenTopoTerrainProvider(
    std::shared_ptr<ports::HttpClient> httpClient,
    std::string demType,
    std::string providerId,
    std::string displayName)
    : httpClient_(std::move(httpClient))
    , demType_(std::move(demType)) {
    info_.providerId = std::move(providerId);
    info_.displayName = std::move(displayName);
    info_.attribution = "OpenTopography / NASA / USGS / DLR / JAXA. See opentopography.org for terms.";
    info_.requiresAuth = true;
    info_.maxResolutionMpp = 30.0;
}

const ProviderInfo& OpenTopoTerrainProvider::info() const noexcept {
    return info_;
}

std::vector<ProviderRequest> OpenTopoTerrainProvider::planRequests(
    const std::vector<SelectionTile>& selectedTiles) const {
    if (selectedTiles.empty()) {
        return {};
    }
    GeoBounds bounds = selectedTiles.front().bounds;
    for (const auto& tile : selectedTiles) {
        bounds.west = std::min(bounds.west, tile.bounds.west);
        bounds.south = std::min(bounds.south, tile.bounds.south);
        bounds.east = std::max(bounds.east, tile.bounds.east);
        bounds.north = std::max(bounds.north, tile.bounds.north);
    }
    ProviderRequest req;
    req.requestId = demType_ + "_" + std::to_string(bounds.west) + "_" + std::to_string(bounds.south);
    req.bounds = bounds;
    req.estimatedBytes = 0;
    return {req};
}

double OpenTopoTerrainProvider::effectiveResolutionMpp(
    const std::vector<SelectionTile>& selectedTiles) const {
    return selectedTiles.empty() ? 0.0 : 30.0;
}

bool OpenTopoTerrainProvider::supportsSelectiveRequests() const noexcept {
    return false;
}

std::filesystem::path OpenTopoTerrainProvider::fetchRequest(
    const ProviderRequest& request,
    const std::filesystem::path& tempDir,
    const std::string& credentialHint,
    const CancellationCallback& cancel) const {
    if (cancel && cancel()) {
        throw ProviderCancelled();
    }
    std::ostringstream url;
    url << std::setprecision(8)
        << "https://portal.opentopography.org/API/globaldem?demtype=" << demType_
        << "&south=" << request.bounds.south
        << "&north=" << request.bounds.north
        << "&west=" << request.bounds.west
        << "&east=" << request.bounds.east
        << "&outputFormat=GTiff";
    if (!credentialHint.empty()) {
        url << "&API_Key=" << credentialHint;
    }

    const auto response = httpClient_->get(url.str(), cancel);
    if (response.transportError == ports::TransportError::Cancelled) {
        throw ProviderCancelled();
    }
    if (response.statusCode == 401 || response.statusCode == 403) {
        throw ProviderError(ProviderErrorCode::AuthenticationFailed, "OpenTopography API key required or invalid");
    }
    if (response.statusCode == 429) {
        throw ProviderError(ProviderErrorCode::RateLimited, "OpenTopography rate limit exceeded");
    }
    if (response.statusCode != 200 || response.body.empty()) {
        throw ProviderError(ProviderErrorCode::SourceUnavailable, "OpenTopography returned HTTP " + std::to_string(response.statusCode));
    }

    const auto outputPath = tempDir / ("opentopo_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".tif");
    std::ofstream out(outputPath, std::ios::binary);
    out.write(reinterpret_cast<const char*>(response.body.data()), static_cast<std::streamsize>(response.body.size()));
    out.close();

    return outputPath;
}

} // namespace infraforge::domain::terrain
