#pragma once

#include "infraforge/application/ProjectService.hpp"
#include "infraforge/domain/geo/GeoTransformService.hpp"
#include "infraforge/domain/project/ProjectModel.hpp"
#include "infraforge/ports/ProjectStore.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace infraforge::application {

// Server-side bound on a single transform batch; larger requests are
// rejected as invalid rather than processed partially.
inline constexpr std::size_t kMaxTransformCoordinates = 4096;

// Resolved canonical georeference: the persisted configuration plus the
// engine-computed metadata used for status display and capability
// reporting.
struct GeoreferenceInfoResult {
    domain::geo::GeoreferenceConfig config;
    domain::geo::ProjectGeoreference resolved;
    std::uint64_t revision{0};
};

struct GeoreferenceMutationResult {
    domain::project::ProjectRecord record;
    domain::geo::ProjectGeoreference resolved;
    std::vector<ProjectEvent> events;
};

// Application boundary of the canonical project georeference (ADR-0007).
// Owns the get/configure/transform use cases on top of the single Geo
// transform service and the project store port. Runs on the single
// application executor.
class GeoService {
public:
    GeoService(ports::ProjectStore& store, const domain::geo::GeoTransformService& transforms);

    // Resolved canonical georeference of the open project.
    [[nodiscard]] GeoreferenceInfoResult getGeoreference() const;

    // Validates, canonicalizes, and persists a new canonical georeference.
    // This is a project-level migration operation: it rewrites the
    // persisted configuration, advances the project revision, and marks the
    // session dirty until the next save. `expectedRevision`, when set,
    // guards against lost updates from stale clients.
    [[nodiscard]] GeoreferenceMutationResult setGeoreference(
        const domain::geo::GeoreferenceConfig& georeference,
        std::optional<std::uint64_t> expectedRevision);

    // Transforms source-CRS coordinates into canonical project-global
    // space through the open project's georeference.
    [[nodiscard]] std::vector<domain::geo::ProjectGlobalPosition> transformToProjectGlobal(
        const domain::geo::SourceSpatialReference& source,
        std::span<const domain::geo::GeoCoordinate> coordinates) const;

private:
    ports::ProjectStore& store_;
    const domain::geo::GeoTransformService& transforms_;
};

} // namespace infraforge::application
