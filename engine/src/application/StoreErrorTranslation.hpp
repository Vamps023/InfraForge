#pragma once

#include "infraforge/application/ProjectService.hpp"
#include "infraforge/domain/geo/GeoTypes.hpp"
#include "infraforge/ports/ProjectStore.hpp"

namespace infraforge::application {

// Shared translation of persistence-port failures into the application
// command taxonomy. Internal to the application layer.
[[noreturn]] inline void translateStoreError(const ports::StoreError& error) {
    switch (error.category()) {
    case ports::StoreErrorCategory::DirectoryInvalid:
        throw CommandFailure(CommandFailureCode::ProjectDirectoryInvalid, error.what());
    case ports::StoreErrorCategory::FormatUnsupported:
        throw CommandFailure(CommandFailureCode::ProjectFormatUnsupported, error.what());
    case ports::StoreErrorCategory::SchemaUnsupported:
        throw CommandFailure(CommandFailureCode::SchemaVersionUnsupported, error.what());
    case ports::StoreErrorCategory::PersistenceFailure:
        throw CommandFailure(CommandFailureCode::PersistenceFailure, error.what());
    }
    throw CommandFailure(CommandFailureCode::Internal, error.what());
}

// Shared translation of canonical geospatial failures into the application
// command taxonomy.
[[noreturn]] inline void translateGeoError(const domain::geo::GeoError& error) {
    switch (error.code()) {
    case domain::geo::GeoErrorCode::InvalidCrs:
    case domain::geo::GeoErrorCode::NotFinite:
        throw CommandFailure(CommandFailureCode::InvalidArgument, error.what());
    case domain::geo::GeoErrorCode::UnsupportedCrs:
    case domain::geo::GeoErrorCode::UnsupportedUnit:
    case domain::geo::GeoErrorCode::UnsupportedTransform:
        throw CommandFailure(CommandFailureCode::GeoUnsupported, error.what());
    case domain::geo::GeoErrorCode::LibraryUnavailable:
    case domain::geo::GeoErrorCode::LibraryFailure:
        break;
    }
    throw CommandFailure(CommandFailureCode::Internal, error.what());
}

} // namespace infraforge::application
