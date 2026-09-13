#pragma once

#include "infraforge/domain/terrain/TerrainTypes.hpp"
#include "infraforge/domain/world/EntityId.hpp"
#include "infraforge/domain/world/SpatialBounds.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace infraforge::domain::terrain {

// Maximum characters accepted for a user-supplied terrain display name.
inline constexpr std::size_t kMaxTerrainDisplayNameLength = 200;

// One active terrain diagnostic attached to a dataset. Codes come from the
// typed TerrainErrorCode taxonomy; diagnostics are detected facts — the
// importer never fabricates warnings (ADR-0010).
struct TerrainDiagnostic {
    TerrainErrorCode code{TerrainErrorCode::InvalidArgument};
    std::string message;
};

// Canonical terrain dataset record (docs/05_DOMAINS/TERRAIN.md). The record
// owns stable identity and raster geometry; the raster bytes live in
// project-owned file storage (ADR-0005) referenced by a project-relative
// path, so projects stay relocatable. Generated render tiles are NOT part
// of this record — they are rebuildable cache content versioned separately.
struct TerrainDataset {
    // Stable 128-bit identity, minted at import commit; uuid text form is
    // the canonical persisted key and the world-index entity id.
    world::EntityId id{};
    std::string displayName;

    // Project-relative storage location of the ingested raster copy, e.g.
    // "terrain/elevation/<uuid>.tif". Never an OS-specific absolute path.
    std::string storagePath;

    // Source format short name as reported by the raster library ("GTiff").
    std::string sourceFormat;
    // Resolved source CRS definition (authority text or WKT2). Empty is
    // impossible for an accepted dataset — missing CRS fails the import.
    std::string sourceCrs;

    // Source raster geometry in source CRS coordinates (north-up, no
    // rotation; the importer rejects rotated geotransforms). origin is the
    // top-left pixel corner in GDAL geotransform convention.
    std::int64_t rasterWidth{0};
    std::int64_t rasterHeight{0};
    double originX{0.0};
    double originY{0.0};
    double cellSizeX{0.0};
    double cellSizeY{0.0}; // stored positive; northing decreases with row index

    // Elevation unit of the source samples: canonical unit-database name
    // plus its metres factor. Heights are converted into the project's
    // canonical linear unit at sampling/tile time.
    std::string elevationUnit;
    double elevationUnitToMetre{1.0};

    bool hasNodata{false};
    double nodataValue{0.0};

    // Canonical coverage (project-global space, closed edges) and the
    // canonical elevation range of valid cells, in the project linear unit.
    world::SpatialBounds bounds{};
    double minZ{0.0};
    double maxZ{0.0};

    // Integrity of the project-owned storage copy.
    std::string sourceSha256;
    std::uint64_t sourceBytes{0};

    // Per-dataset content revision; advances when canonical content is
    // re-ingested. Cache metadata records the revision it was derived from.
    std::uint64_t revision{1};

    // Active diagnostics detected at import time (e.g. NoData cells present).
    std::vector<TerrainDiagnostic> diagnostics;

    std::string createdAt;
    std::string modifiedAt;
};

// Structural validation for values about to be persisted or registered.
// Returns nullopt when the record is well-formed.
[[nodiscard]] std::optional<std::string> validateTerrainDataset(const TerrainDataset& dataset);

// EntityId <-> canonical uuid text. Dataset ids are persisted as uuid text
// (the row primary key) and used as EntityId values in the world index;
// the mapping packs the 16 uuid bytes big-endian into (high, low).
[[nodiscard]] world::EntityId entityIdFromUuidText(std::string_view uuidText);
[[nodiscard]] std::string uuidTextFromEntityId(world::EntityId id);

} // namespace infraforge::domain::terrain
