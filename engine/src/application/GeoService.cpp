#include "infraforge/application/GeoService.hpp"

#include "StoreErrorTranslation.hpp"

#include <cstdint>

namespace infraforge::application {
namespace {

[[noreturn]] void fail(CommandFailureCode code, std::string message) {
    throw CommandFailure(code, std::move(message));
}

void requireOpenProject(const ports::ProjectStore& store) {
    if (!store.isOpen()) {
        fail(CommandFailureCode::ProjectNotOpen, "no project is open");
    }
}

} // namespace

GeoService::GeoService(ports::ProjectStore& store, const domain::geo::GeoTransformService& transforms)
    : store_(store),
      transforms_(transforms) {}

GeoreferenceInfoResult GeoService::getGeoreference() const {
    requireOpenProject(store_);
    const auto& record = store_.current();
    try {
        return GeoreferenceInfoResult{
            .config = record.georeference,
            .resolved = transforms_.resolveProjectGeoreference(record.georeference),
            .revision = record.revision,
        };
    } catch (const domain::geo::GeoError& error) {
        translateGeoError(error);
    }
}

GeoreferenceMutationResult GeoService::setGeoreference(
    const domain::geo::GeoreferenceConfig& georeference,
    const std::optional<std::uint64_t> expectedRevision) {
    requireOpenProject(store_);
    // Copy: updateGeoreference mutates the store's live record, and the
    // dirty-transition check below compares before/after state.
    const auto current = store_.current();
    if (expectedRevision.has_value() && *expectedRevision != current.revision) {
        fail(CommandFailureCode::InvalidArgument,
            "project revision changed (expected " + std::to_string(*expectedRevision) + ", current "
                + std::to_string(current.revision) + "); reload the georeference and retry");
    }

    GeoreferenceMutationResult result;
    try {
        // Validation and canonicalization happen before any write: an
        // invalid or unsupported configuration leaves the project and its
        // revision untouched.
        const domain::geo::GeoreferenceConfig canonical = transforms_.canonicalizeConfig(georeference);
        result.resolved = transforms_.resolveProjectGeoreference(canonical);
        result.record = store_.updateGeoreference(canonical);
        result.record.georeference = canonical;
    } catch (const domain::geo::GeoError& error) {
        translateGeoError(error);
    } catch (const ports::StoreError& error) {
        translateStoreError(error);
    }

    result.events.push_back({ProjectEventKind::GeoreferenceChanged, result.record, result.resolved});
    result.events.push_back({ProjectEventKind::RevisionChanged, result.record});
    if (result.record.isDirty() != current.isDirty()) {
        result.events.push_back({ProjectEventKind::DirtyStateChanged, result.record});
    }
    return result;
}

std::vector<domain::geo::ProjectGlobalPosition> GeoService::transformToProjectGlobal(
    const domain::geo::SourceSpatialReference& source,
    const std::span<const domain::geo::GeoCoordinate> coordinates) const {
    requireOpenProject(store_);
    if (coordinates.size() > kMaxTransformCoordinates) {
        fail(CommandFailureCode::InvalidArgument,
            "transform batch exceeds the " + std::to_string(kMaxTransformCoordinates)
                + " coordinate limit");
    }

    try {
        const domain::geo::ProjectGeoreference project =
            transforms_.resolveProjectGeoreference(store_.current().georeference);
        std::vector<domain::geo::ProjectGlobalPosition> positions;
        positions.reserve(coordinates.size());
        for (const domain::geo::GeoCoordinate& coordinate : coordinates) {
            positions.push_back(transforms_.sourceToProjectGlobal(project, source, coordinate));
        }
        return positions;
    } catch (const domain::geo::GeoError& error) {
        translateGeoError(error);
    }
}

} // namespace infraforge::application
