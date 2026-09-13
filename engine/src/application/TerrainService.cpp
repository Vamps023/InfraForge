#include "infraforge/application/TerrainService.hpp"

#include "infraforge/application/CommandFailure.hpp"
#include "infraforge/application/TerrainTileGenerator.hpp"
#include "infraforge/domain/geo/GeoTransformService.hpp"
#include "infraforge/domain/terrain/TerrainTileFile.hpp"
#include "infraforge/domain/world/Invalidation.hpp"
#include "infraforge/runtime/FileSystemUtf8.hpp"
#include "infraforge/runtime/Logging.hpp"
#include "infraforge/runtime/Sha256.hpp"
#include "infraforge/runtime/Timestamp.hpp"
#include "infraforge/runtime/Uuid.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>

namespace infraforge::application {
namespace {

using domain::terrain::TerrainError;
using domain::terrain::TerrainErrorCode;
using domain::world::ChunkCoord;
using domain::world::InvalidationClass;
using domain::world::InvalidationMask;

// Copy chunk for streaming the source file into project-owned storage.
constexpr std::size_t kImportCopyChunkBytes = 4u * 1024u * 1024u;
// Elevation scan strip height (raster rows) for range/NoData measurement.
constexpr std::int64_t kElevationScanRows = 256;
// Upper bound of eagerly generated tiles per import; beyond it the import
// completes with a documented deferral diagnostic and
// terrain.regenerate_tiles covers the remainder in bounded batches.
constexpr std::uint64_t kMaxEagerTilesPerImport = 4096;

// Phase weights for coherent normalized import progress (BLOCKER 12).
// The import has three phases with different work units (bytes, rows);
// normalized progress in [0,1] avoids backwards jumps when units change.
// Phase 1 (copy source):   [0.00, 0.40]
// Phase 2 (validate):      [0.40, 0.45]
// Phase 3 (coverage+scan): [0.45, 1.00]
constexpr double kPhase1Start = 0.0;
constexpr double kPhase1End = 0.40;
constexpr double kPhase2Start = 0.40;
constexpr double kPhase2End = 0.45;
constexpr double kPhase3Start = 0.45;
constexpr double kPhase3End = 1.0;

[[noreturn]] void failImport(TerrainErrorCode code, const std::string& message) {
    throw TerrainError(code, message);
}

[[nodiscard]] std::filesystem::path renameReplace(const std::filesystem::path& from,
    const std::filesystem::path& to) {
    std::error_code error;
    std::filesystem::rename(from, to, error);
    if (error) {
        // Windows rename cannot overwrite an existing target.
        std::filesystem::remove(to, error);
        std::filesystem::rename(from, to, error);
        if (error) {
            failImport(TerrainErrorCode::SourceDataMissing,
                "cannot publish project-owned raster: " + error.message());
        }
    }
    return to;
}

} // namespace

TerrainService::TerrainService(ports::ProjectStore& store,
    const domain::geo::GeoTransformService& transforms, ports::TerrainSourceReader& reader,
    WorldState& world, JobSystem& jobs, EventSink eventSink)
    : store_(store),
      transforms_(transforms),
      reader_(reader),
      world_(world),
      jobs_(jobs),
      eventSink_(std::move(eventSink)) {}

void TerrainService::onProjectOpened() {
    const auto& record = store_.current();
    project_ = transforms_.resolveProjectGeoreference(record.georeference);
    revision_ = record.revision;
    world_.resetForProject(*project_);
    datasets_.clear();

    // Registry replay in creation order reproduces the spatial index's
    // terrain-class content generations deterministically, so derived tile
    // caches recorded against lastAffectingRevision(chunk, Terrain) remain
    // current across reopen (docs/05_DOMAINS/TERRAIN.md).
    for (domain::terrain::TerrainDataset& dataset : store_.terrainDatasets()) {
        std::error_code error;
        const auto storedFile = projectDirectory_() / std::filesystem::path{dataset.storagePath};
        if (!std::filesystem::is_regular_file(storedFile, error)) {
            dataset.diagnostics.push_back({TerrainErrorCode::SourceDataMissing,
                "project-owned raster storage is missing: " + dataset.storagePath});
            runtime::logError("terrain", "terrain.storage_missing",
                {{"dataset", domain::terrain::uuidTextFromEntityId(dataset.id)}});
        }
        // Registry replay mutation: its chunk-diff value is intentionally
        // unused — generation replay is the effect that matters here.
        (void)world_.insert(dataset.id, dataset.bounds, InvalidationMask::of(InvalidationClass::Terrain));
        datasets_.push_back(std::move(dataset));
    }
}

void TerrainService::onProjectClosed() {
    cancelTrackedJobs();
    project_.reset();
    datasets_.clear();
    revision_ = 0;
}

TerrainProbeResult TerrainService::probeSource(const std::filesystem::path& file) const {
    TerrainProbeResult result;
    result.source = reader_.probe(file);
    if (result.source.crsDefinition.empty()) {
        // Defensive: the reader port rejects CRS-less rasters already.
        failImport(TerrainErrorCode::MissingCrs,
            "source raster declares no CRS; a missing CRS is never assumed");
    }
    const auto resolved = transforms_.describeCrs(result.source.crsDefinition);
    result.crsName = resolved.name;
    result.crsKind = std::string{domain::geo::crsKindName(resolved.kind)};
    result.crsAuthority = resolved.authority;
    result.crsCode = resolved.code;
    return result;
}

JobRecord TerrainService::startImport(const TerrainImportSpec& spec) {
    if (!store_.isOpen() || !project_.has_value()) {
        throw CommandFailure(CommandFailureCode::ProjectNotOpen, "terrain import requires an open project");
    }
    if (spec.displayName.empty() || spec.displayName.size() > domain::terrain::kMaxTerrainDisplayNameLength) {
        throw CommandFailure(CommandFailureCode::InvalidArgument,
            "terrain display name must be 1.." + std::to_string(domain::terrain::kMaxTerrainDisplayNameLength)
                + " characters");
    }

    // Full source validation up front so CRS/raster problems surface as a
    // typed command error before any job starts or byte is copied.
    const TerrainProbeResult probe = probeSource(spec.sourcePath);

    const std::string datasetUuid = runtime::generateUuidV4();
    const std::filesystem::path projectDirectory = projectDirectory_();
    // Aggregate construction: ImportPayload is not default-constructible
    // because the chunk grid requires its configured cell size.
    auto payload = std::shared_ptr<ImportPayload>(new ImportPayload{
        .dataset = {},
        .projectUuid = store_.current().uuid,
        .projectDirectory = projectDirectory,
        .tempFile = projectDirectory / "terrain" / "elevation" / (datasetUuid + ".tif.importing"),
        .grid = world_.grid(),
        .project = *project_,
    });

    domain::terrain::TerrainDataset& dataset = payload->dataset;
    dataset.id = domain::terrain::entityIdFromUuidText(datasetUuid);
    dataset.displayName = spec.displayName;
    dataset.storagePath = "terrain/elevation/" + datasetUuid + ".tif";
    dataset.sourceFormat = probe.source.format;
    dataset.sourceCrs = probe.source.crsDefinition;
    dataset.rasterWidth = probe.source.width;
    dataset.rasterHeight = probe.source.height;
    dataset.originX = probe.source.originX;
    dataset.originY = probe.source.originY;
    dataset.cellSizeX = probe.source.pixelSizeX;
    dataset.cellSizeY = probe.source.pixelSizeY;
    dataset.elevationUnit = probe.source.elevationUnit;
    dataset.elevationUnitToMetre = probe.source.elevationUnitToMetre;
    dataset.hasNodata = probe.source.hasNodata;
    dataset.nodataValue = probe.source.nodataValue;
    dataset.sourceBytes = probe.source.fileBytes;
    dataset.createdAt = runtime::utcTimestampNow();
    dataset.modifiedAt = dataset.createdAt;

    const std::filesystem::path sourcePath = spec.sourcePath;
    const std::uint64_t totalBytes = probe.source.fileBytes;
    const auto body = [this, payload, sourcePath, totalBytes](JobContext& context) -> JobSystem::Payload {
        // Phase 1: stream the source into project-owned storage, hashing
        // while copying. Progress units are real bytes.
        {
            std::ifstream input(sourcePath, std::ios::binary);
            if (!input) {
                failImport(TerrainErrorCode::SourceUnreadable,
                    "terrain source cannot be opened: " + sourcePath.string());
            }
            std::ofstream output(payload->tempFile, std::ios::binary | std::ios::trunc);
            if (!output) {
                failImport(TerrainErrorCode::SourceDataMissing,
                    "project-owned terrain storage cannot be created: " + payload->tempFile.string());
            }
            runtime::Sha256 hash;
            std::vector<char> buffer(kImportCopyChunkBytes);
            std::uint64_t copied = 0;
            while (input) {
                context.throwIfCancelled();
                input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                const std::streamsize read = input.gcount();
                if (read <= 0) {
                    break;
                }
                output.write(buffer.data(), read);
                if (!output) {
                    failImport(TerrainErrorCode::SourceDataMissing,
                        "project-owned terrain storage write failed: " + payload->tempFile.string());
                }
                hash.update(buffer.data(), static_cast<std::size_t>(read));
                copied += static_cast<std::uint64_t>(read);
                const double copyRatio = totalBytes > 0
                    ? static_cast<double>(copied) / static_cast<double>(totalBytes)
                    : 0.0;
                context.reportNormalizedProgress(
                    kPhase1Start + copyRatio * (kPhase1End - kPhase1Start), "copying source raster");
            }
            if (input.bad()) {
                failImport(TerrainErrorCode::CorruptSource,
                    "terrain source read failed while copying: " + sourcePath.string());
            }
            if (totalBytes != 0 && copied != totalBytes) {
                failImport(TerrainErrorCode::CorruptSource,
                    "terrain source changed size while being read (expected "
                        + std::to_string(totalBytes) + " bytes, read " + std::to_string(copied) + ")");
            }
            payload->dataset.sourceSha256 = runtime::sha256Hex(hash.finish());
        }

        // Phase 2: validate the stored copy as a raster and make it the
        // authoritative source of canonical metadata (HIGH 5 TOCTOU fix).
        // The initial probe (before the job) may have observed a different
        // source state; after the async copy, the stored copy is the only
        // truth. All canonical metadata is re-derived from the stored copy.
        context.reportNormalizedProgress(kPhase2Start, "validating stored copy");
        context.throwIfCancelled();
        const ports::TerrainSourceInfo stored = reader_.probe(payload->tempFile);
        if (stored.width <= 0 || stored.height <= 0 || stored.crsDefinition.empty()) {
            failImport(TerrainErrorCode::CorruptSource,
                "stored terrain copy is not a valid raster");
        }
        // Re-derive canonical metadata from the stored copy. If the source
        // changed between the initial probe and the copy, the stored copy
        // wins — the dataset agrees with what is actually persisted.
        payload->dataset.sourceFormat = stored.format;
        payload->dataset.sourceCrs = stored.crsDefinition;
        payload->dataset.rasterWidth = stored.width;
        payload->dataset.rasterHeight = stored.height;
        payload->dataset.originX = stored.originX;
        payload->dataset.originY = stored.originY;
        payload->dataset.cellSizeX = stored.pixelSizeX;
        payload->dataset.cellSizeY = stored.pixelSizeY;
        payload->dataset.elevationUnit = stored.elevationUnit;
        payload->dataset.elevationUnitToMetre = stored.elevationUnitToMetre;
        payload->dataset.hasNodata = stored.hasNodata;
        payload->dataset.nodataValue = stored.nodataValue;
        payload->dataset.sourceBytes = stored.fileBytes;

        // Worker-confined transform service (PROJ objects are thread-bound);
        // all project geometry comes from the immutable payload snapshot,
        // never from executor-owned service members.
        const domain::geo::GeoTransformService workerTransforms;
        const domain::geo::ProjectGeoreference& project = payload->project;
        const domain::geo::SourceSpatialReference sourceSrs{
            .horizontalCrs = payload->dataset.sourceCrs, .verticalCrs = ""};

        // Phase 3: coverage transform + elevation scan. Progress units are
        // scanned raster rows; NoData cells are counted, never resampled.
        const std::uint64_t totalScanRows = static_cast<std::uint64_t>(payload->dataset.rasterHeight);
        bool anyValid = false;
        double minZSource = std::numeric_limits<double>::infinity();
        double maxZSource = -std::numeric_limits<double>::infinity();
        std::uint64_t nodataCells = 0;

        domain::world::SpatialBounds coverage = domain::world::SpatialBounds::empty();
        for (std::int64_t corner = 0; corner < 4; ++corner) {
            const double sourceX = payload->dataset.originX
                + (corner % 2 == 1 ? static_cast<double>(payload->dataset.rasterWidth) * payload->dataset.cellSizeX : 0.0);
            const double sourceY = payload->dataset.originY
                - (corner / 2 == 1 ? static_cast<double>(payload->dataset.rasterHeight) * payload->dataset.cellSizeY : 0.0);
            const auto position = workerTransforms.sourceToProjectGlobal(
                project, sourceSrs, domain::geo::GeoCoordinate{.x = sourceX, .y = sourceY, .z = 0.0});
            if (!domain::geo::isFinite(position)) {
                failImport(TerrainErrorCode::InvalidCoverage,
                    "source coverage corner transforms to a non-finite canonical position");
            }
            coverage.expandTo(position);
        }
        if (coverage.isEmpty()) {
            failImport(TerrainErrorCode::InvalidCoverage, "source coverage is empty after transform");
        }
        payload->dataset.bounds = coverage;

        for (std::int64_t row = 0; row < payload->dataset.rasterHeight; row += kElevationScanRows) {
            context.throwIfCancelled();
            const ports::TerrainElevationBlock block = reader_.readBlock(payload->tempFile, 0, row,
                payload->dataset.rasterWidth,
                std::min<std::int64_t>(kElevationScanRows, payload->dataset.rasterHeight - row));
            for (const double value : block.elevations) {
                if (std::isnan(value)) {
                    ++nodataCells;
                    continue;
                }
                anyValid = true;
                minZSource = std::min(minZSource, value);
                maxZSource = std::max(maxZSource, value);
            }
            const double scanProgress =
                static_cast<double>(std::min(row + kElevationScanRows, payload->dataset.rasterHeight))
                / static_cast<double>(totalScanRows);
            context.reportNormalizedProgress(
                kPhase3Start + scanProgress * (kPhase3End - kPhase3Start), "measuring elevation range");
        }
        if (!anyValid) {
            failImport(TerrainErrorCode::UnsupportedRaster, "raster contains no valid elevation cells");
        }
        payload->dataset.minZ = minZSource * payload->dataset.elevationUnitToMetre
            / project.linearUnit.toMetre;
        payload->dataset.maxZ = maxZSource * payload->dataset.elevationUnitToMetre
            / project.linearUnit.toMetre;
        if (nodataCells > 0) {
            payload->dataset.hasNodata = true;
            payload->dataset.diagnostics.push_back({TerrainErrorCode::NodataCells,
                std::to_string(nodataCells) + " NoData cells are excluded from sampling and rendering"});
        }
        return payload;
    };

    const auto onProgress = [this](const JobRecord& record) { emitJobUpdate(record); };
    const auto onComplete = [this, payload](const JobOutcome& outcome) {
        untrackJob(outcome.record.jobId);
        if (outcome.record.state != JobState::Completed) {
            // Failed or cancelled before commit: clean the temporary copy so
            // no partial storage remains and no canonical state exists.
            // The registry already has the terminal state (Failed/Cancelled)
            // because non-Completed outcomes finalize immediately.
            std::error_code error;
            std::filesystem::remove(payload->tempFile, error);
            emitJobUpdate(outcome.record);
            return;
        }
        // Body succeeded; the job is finalizing (registry state = Running).
        // The canonical commit below determines the authoritative terminal
        // state. Only after commit success does the job become Completed.
        try {
            if (!store_.isOpen() || store_.current().uuid != payload->projectUuid) {
                throw CommandFailure(CommandFailureCode::ProjectNotOpen,
                    "the project closed or changed while the terrain import was running");
            }
            commitImportedDataset(*std::static_pointer_cast<ImportPayload>(outcome.payload));
            jobs_.markCompleted(outcome.record.jobId);
            // Emit the completed job update after the registry is authoritative.
            JobRecord completed = outcome.record;
            completed.state = JobState::Completed;
            emitJobUpdate(completed);
        } catch (const std::exception& error) {
            // Canonical commit failed: the temp file is already cleaned inside
            // commitImportedDataset's compensation. Mark the job FAILED in the
            // authoritative registry so job.list agrees with the event.
            jobs_.markFailed(outcome.record.jobId,
                std::string{"terrain import commit failed: "} + error.what());
            JobRecord failed = outcome.record;
            failed.state = JobState::Failed;
            failed.message = std::string{"terrain import commit failed: "} + error.what();
            emitJobUpdate(failed);
        }
    };

    const JobRecord record = jobs_.submit("terrain.import", "copying source raster",
        body, onProgress, onComplete, /*requiresFinalization=*/true);
    trackJob(record.jobId);
    return record;
}

void TerrainService::commitImportedDataset(ImportPayload payload) {
    // Publish the project-owned raster at its canonical storage location
    // before the database transaction (manifest-first compensation: a crash
    // before commit leaves an unreferenced file, never a canonical record
    // without storage).
    const std::filesystem::path canonicalPath =
        payload.projectDirectory / std::filesystem::path{payload.dataset.storagePath};
    (void)renameReplace(payload.tempFile, canonicalPath);

    // The canonical commit boundary: DB row + world index + in-memory
    // projection must all succeed together. If any fails after the DB row
    // is committed, the DB row is rolled back (removeTerrainDataset) and
    // the raster is removed. After this boundary, the dataset is canonical
    // and post-commit failures (events, tile generation) never delete the
    // raster or roll back the DB.
    const std::string datasetUuid = domain::terrain::uuidTextFromEntityId(payload.dataset.id);

    ports::TerrainDatasetInsertResult inserted;
    try {
        inserted = store_.insertTerrainDataset(payload.dataset);
        // world_.insert does not throw (validated bounds math), but if it
        // ever does, we must roll back the DB row.
        const domain::world::IndexMutation mutation = world_.insert(
            payload.dataset.id, payload.dataset.bounds, InvalidationMask::of(InvalidationClass::Terrain));
        domain::world::ChunkDirtySet dirty;
        dirty.absorb(mutation);
        datasets_.push_back(payload.dataset);
        revision_ = inserted.record.revision;

        runtime::logInfo("terrain", "terrain.dataset_imported",
            {{"dataset", datasetUuid},
                {"dirtyChunks", std::to_string(dirty.size())}});
    } catch (...) {
        // Canonical commit failed. Roll back the DB row (if it was
        // committed) and remove the raster. The UUID-derived storage path
        // guarantees no collision with a previous file.
        try {
            store_.removeTerrainDataset(datasetUuid);
        } catch (const std::exception& rollbackError) {
            runtime::logError("terrain", "terrain.rollback_db_failed",
                {{"dataset", datasetUuid}, {"error", rollbackError.what()}});
        }
        std::error_code cleanupError;
        std::filesystem::remove(canonicalPath, cleanupError);
        if (cleanupError) {
            runtime::logError("terrain", "terrain.compensation_cleanup_failed",
                {{"path", runtime::utf8String(canonicalPath)},
                    {"error", cleanupError.message()}});
        }
        throw;
    }

    // Post-commit: events and derived tile generation. Failures here are
    // logged/reported but do NOT delete the canonical raster or roll back
    // the DB — the dataset is already canonical and durable.
    try {
        if (eventSink_) {
            eventSink_({.job = std::nullopt, .datasetAdded = payload.dataset, .revision = revision_});
        }
        submitTileGenerationJob(std::move(payload));
    } catch (const std::exception& postCommitError) {
        // The dataset is canonical; tile generation is derived cache work.
        // Log the failure but do not invalidate the committed import.
        runtime::logError("terrain", "terrain.post_commit_failed",
            {{"dataset", datasetUuid}, {"error", postCommitError.what()}});
    }
}

void TerrainService::submitTileGenerationJob(ImportPayload payload) {
    const std::string datasetUuid = domain::terrain::uuidTextFromEntityId(payload.dataset.id);
    std::vector<ChunkCoord> chunks = expectedChunks(payload.dataset);
    if (chunks.size() > kMaxEagerTilesPerImport) {
        // Eager generation is bounded; the remainder is covered by
        // terrain.regenerate_tiles in cancellable batches (documented in
        // docs/05_DOMAINS/TERRAIN.md).
        runtime::logWarn("terrain", "terrain.tiles_deferred",
            {{"dataset", datasetUuid},
                {"deferredChunks", std::to_string(chunks.size() - kMaxEagerTilesPerImport)}});
        chunks.resize(static_cast<std::size_t>(kMaxEagerTilesPerImport));
    }

    const auto body = [this, payload, chunks](JobContext& context) -> JobSystem::Payload {
        const TerrainTileGenerator::Summary summary = TerrainTileGenerator::generateTiles(
            {.projectDirectory = payload.projectDirectory,
                .dataset = payload.dataset,
                .grid = payload.grid,
                .project = payload.project,
                .sourceCrs = payload.dataset.sourceCrs},
            reader_, chunks,
            [&context] { context.throwIfCancelled(); },
            [&context](std::uint64_t done, std::uint64_t total) {
                context.reportProgress(done, total, "generating terrain tiles");
            });
        runtime::logInfo("terrain", "terrain.tiles_generated",
            {{"dataset", domain::terrain::uuidTextFromEntityId(payload.dataset.id)},
                {"generated", std::to_string(summary.tilesGenerated)},
                {"skipped", std::to_string(summary.tilesSkipped)}});
        return nullptr;
    };

    const auto onProgress = [this](const JobRecord& record) { emitJobUpdate(record); };
    const auto onComplete = [this](const JobOutcome& outcome) {
        untrackJob(outcome.record.jobId);
        emitJobUpdate(outcome.record);
        if (outcome.record.state != JobState::Completed) {
            runtime::logWarn("terrain", "terrain.tiles_job_not_completed",
                {{"job", outcome.record.jobId},
                    {"state", std::string{jobStateName(outcome.record.state)}}});
        }
    };

    const JobRecord record = jobs_.submit("terrain.tiles", "generating terrain tiles",
        body, onProgress, onComplete, /*requiresFinalization=*/false);
    trackJob(record.jobId);
}

JobRecord TerrainService::regenerateTiles(const std::string& datasetUuid) {
    if (!store_.isOpen() || !project_.has_value()) {
        throw CommandFailure(CommandFailureCode::ProjectNotOpen, "terrain requires an open project");
    }
    const auto dataset = findDataset(datasetUuid);
    if (!dataset.has_value()) {
        throw CommandFailure(CommandFailureCode::InvalidArgument,
            "unknown terrain dataset: " + datasetUuid);
    }

    std::vector<ChunkCoord> missing;
    for (const ChunkCoord chunk : expectedChunks(*dataset)) {
        const auto path = TerrainTileGenerator::tilePath(projectDirectory_(), datasetUuid, chunk.x, chunk.y);
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error)) {
            missing.push_back(chunk);
        }
    }
    if (missing.empty()) {
        throw CommandFailure(CommandFailureCode::InvalidArgument,
            "all derived tiles for dataset " + datasetUuid + " are present");
    }

    auto payload = std::shared_ptr<ImportPayload>(new ImportPayload{
        .dataset = *dataset,
        .projectUuid = store_.current().uuid,
        .projectDirectory = projectDirectory_(),
        .tempFile = {},
        .grid = world_.grid(),
        .project = *project_,
    });

    const auto body = [this, payload, missing](JobContext& context) -> JobSystem::Payload {
        (void)TerrainTileGenerator::generateTiles(
            {.projectDirectory = payload->projectDirectory,
                .dataset = payload->dataset,
                .grid = payload->grid,
                .project = payload->project,
                .sourceCrs = payload->dataset.sourceCrs},
            reader_, missing,
            [&context] { context.throwIfCancelled(); },
            [&context](std::uint64_t done, std::uint64_t total) {
                context.reportProgress(done, total, "generating terrain tiles");
            });
        return nullptr;
    };
    const auto onProgress = [this](const JobRecord& record) { emitJobUpdate(record); };
    const auto onComplete = [this](const JobOutcome& outcome) {
        untrackJob(outcome.record.jobId);
        emitJobUpdate(outcome.record);
    };
    const JobRecord record = jobs_.submit("terrain.tiles", "generating terrain tiles",
        body, onProgress, onComplete, /*requiresFinalization=*/false);
    trackJob(record.jobId);
    return record;
}

std::vector<domain::terrain::TerrainDataset> TerrainService::listDatasets() const {
    return datasets_;
}

TerrainDatasetDetails TerrainService::datasetDetails(const std::string& datasetUuid) const {
    const auto dataset = findDataset(datasetUuid);
    if (!dataset.has_value()) {
        throw CommandFailure(CommandFailureCode::InvalidArgument,
            "unknown terrain dataset: " + datasetUuid);
    }
    TerrainDatasetDetails details{.dataset = *dataset};
    for (const ChunkCoord chunk : expectedChunks(*dataset)) {
        ++details.expectedTiles;
        const auto path = TerrainTileGenerator::tilePath(projectDirectory_(), datasetUuid, chunk.x, chunk.y);
        std::error_code error;
        if (std::filesystem::is_regular_file(path, error)) {
            ++details.presentTiles;
        }
    }
    return details;
}

TerrainSceneProjection TerrainService::sceneProjection() const {
    if (!project_.has_value()) {
        throw CommandFailure(CommandFailureCode::ProjectNotOpen, "terrain requires an open project");
    }
    TerrainSceneProjection projection;
    projection.renderOrigin = project_->origin;
    projection.revision = revision_;
    for (const domain::terrain::TerrainDataset& dataset : datasets_) {
        for (const ChunkCoord chunk : expectedChunks(dataset)) {
            const auto path = TerrainTileGenerator::tilePath(projectDirectory_(),
                domain::terrain::uuidTextFromEntityId(dataset.id), chunk.x, chunk.y);
            std::error_code error;
            if (!std::filesystem::is_regular_file(path, error)) {
                ++projection.missingTiles;
                continue;
            }
            const domain::world::SpatialBounds tileRect =
                world_.grid().chunkBounds(chunk).intersectedWith(dataset.bounds);
            projection.tiles.push_back({
                .datasetUuid = domain::terrain::uuidTextFromEntityId(dataset.id),
                .datasetRevision = dataset.revision,
                .chunkX = chunk.x,
                .chunkY = chunk.y,
                .absolutePath = runtime::utf8String(path),
                .minEasting = tileRect.minEasting,
                .minNorthing = tileRect.minNorthing,
                .maxEasting = tileRect.maxEasting,
                .maxNorthing = tileRect.maxNorthing,
            });
        }
    }
    return projection;
}

TerrainSampleResult TerrainService::sample(
    const std::string& datasetUuid, const double easting, const double northing) const {
    if (!store_.isOpen() || !project_.has_value()) {
        throw CommandFailure(CommandFailureCode::ProjectNotOpen, "terrain requires an open project");
    }
    if (!std::isfinite(easting) || !std::isfinite(northing)) {
        throw CommandFailure(CommandFailureCode::InvalidArgument, "sample coordinates must be finite");
    }

    std::optional<domain::terrain::TerrainDataset> dataset;
    if (!datasetUuid.empty()) {
        dataset = findDataset(datasetUuid);
        if (!dataset.has_value()) {
            throw CommandFailure(CommandFailureCode::InvalidArgument,
                "unknown terrain dataset: " + datasetUuid);
        }
    } else {
        // Deterministic overlap rule: the most recently imported covering
        // dataset wins (reverse creation order).
        for (auto iterator = datasets_.rbegin(); iterator != datasets_.rend(); ++iterator) {
            if (iterator->bounds.contains(easting, northing)) {
                dataset = *iterator;
                break;
            }
        }
        if (!dataset.has_value()) {
            return {.datasetUuid = "", .sample = {.status = domain::terrain::TerrainSampleStatus::OutsideCoverage}};
        }
    }

    domain::terrain::TerrainSampler::RasterGeometry geometry;
    geometry.width = dataset->rasterWidth;
    geometry.height = dataset->rasterHeight;
    geometry.originX = dataset->originX;
    geometry.originY = dataset->originY;
    geometry.cellSizeX = dataset->cellSizeX;
    geometry.cellSizeY = dataset->cellSizeY;
    geometry.elevationUnitToMetre = dataset->elevationUnitToMetre;

    const domain::terrain::TerrainSampler sampler(
        reader_, projectDirectory_() / std::filesystem::path{dataset->storagePath},
        geometry, *project_,
        domain::geo::SourceSpatialReference{.horizontalCrs = dataset->sourceCrs, .verticalCrs = ""},
        transforms_);
    return {.datasetUuid = domain::terrain::uuidTextFromEntityId(dataset->id), .sample = sampler.sample(easting, northing)};
}

std::filesystem::path TerrainService::projectDirectory_() const {
    return std::filesystem::path{runtime::pathFromUtf8(store_.current().directory)};
}

std::optional<domain::terrain::TerrainDataset> TerrainService::findDataset(
    const std::string& datasetUuid) const {
    for (const domain::terrain::TerrainDataset& dataset : datasets_) {
        if (domain::terrain::uuidTextFromEntityId(dataset.id) == datasetUuid) {
            return dataset;
        }
    }
    return std::nullopt;
}

std::vector<ChunkCoord> TerrainService::expectedChunks(
    const domain::terrain::TerrainDataset& dataset) const {
    return world_.chunksIntersecting(dataset.bounds);
}

void TerrainService::emitJobUpdate(const JobRecord& record) {
    if (eventSink_) {
        eventSink_({.job = record, .datasetAdded = std::nullopt, .revision = revision_});
    }
}

void TerrainService::trackJob(const std::string& jobId) {
    activeJobIds_.push_back(jobId);
}

void TerrainService::untrackJob(const std::string& jobId) {
    std::erase(activeJobIds_, jobId);
}

void TerrainService::cancelTrackedJobs() {
    // Cooperative: each job stops at its next bounded checkpoint; canonical
    // state stays consistent because commits verify the project identity.
    for (const std::string& jobId : activeJobIds_) {
        (void)jobs_.requestCancel(jobId);
    }
}

} // namespace infraforge::application
