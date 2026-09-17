#include "infraforge/application/CommandProcessor.hpp"

#include "infraforge/application/ProjectService.hpp"
#include "infraforge/domain/geo/GeoTypes.hpp"
#include "infraforge/domain/terrain/TerrainTypes.hpp"
#include "infraforge/persistence/GdalTerrainSource.hpp"
#include "infraforge/runtime/FileSystemUtf8.hpp"
#include "infraforge/runtime/Logging.hpp"
#include "infraforge/runtime/Uuid.hpp"
#include "infraforge/version.hpp"

#include <infraforge/protocol/v1/foundation.pb.h>
#include <infraforge/protocol/v1/road.pb.h>

#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

namespace infraforge::application {
namespace {

using ProtocolFrame = protocol::v1::Frame;
using ProtocolCommandErrorCode = protocol::v1::CommandErrorCode;

ProtocolCommandErrorCode mapFailureCode(const CommandFailureCode code) {
    switch (code) {
    case CommandFailureCode::InvalidArgument:
        return ProtocolCommandErrorCode::COMMAND_ERROR_CODE_INVALID_ARGUMENT;
    case CommandFailureCode::ProjectNotOpen:
        return ProtocolCommandErrorCode::COMMAND_ERROR_CODE_PROJECT_NOT_OPEN;
    case CommandFailureCode::ProjectAlreadyOpen:
        return ProtocolCommandErrorCode::COMMAND_ERROR_CODE_PROJECT_ALREADY_OPEN;
    case CommandFailureCode::ProjectDirectoryInvalid:
        return ProtocolCommandErrorCode::COMMAND_ERROR_CODE_PROJECT_DIRECTORY_INVALID;
    case CommandFailureCode::ProjectFormatUnsupported:
        return ProtocolCommandErrorCode::COMMAND_ERROR_CODE_PROJECT_FORMAT_UNSUPPORTED;
    case CommandFailureCode::SchemaVersionUnsupported:
        return ProtocolCommandErrorCode::COMMAND_ERROR_CODE_SCHEMA_VERSION_UNSUPPORTED;
    case CommandFailureCode::PersistenceFailure:
        return ProtocolCommandErrorCode::COMMAND_ERROR_CODE_PERSISTENCE_FAILURE;
    case CommandFailureCode::Internal:
        return ProtocolCommandErrorCode::COMMAND_ERROR_CODE_INTERNAL;
    case CommandFailureCode::GeoUnsupported:
        return ProtocolCommandErrorCode::COMMAND_ERROR_CODE_GEO_UNSUPPORTED;
    case CommandFailureCode::TerrainUnsupported:
        return ProtocolCommandErrorCode::COMMAND_ERROR_CODE_TERRAIN_UNSUPPORTED;
    case CommandFailureCode::NotFound:
        return ProtocolCommandErrorCode::COMMAND_ERROR_CODE_NOT_FOUND;
    }
    return ProtocolCommandErrorCode::COMMAND_ERROR_CODE_INTERNAL;
}

std::optional<domain::project::TrafficSide> mapTrafficSide(const protocol::v1::TrafficSide side) {
    switch (side) {
    case protocol::v1::TRAFFIC_SIDE_LEFT:
        return domain::project::TrafficSide::Left;
    case protocol::v1::TRAFFIC_SIDE_RIGHT:
        return domain::project::TrafficSide::Right;
    default:
        return std::nullopt;
    }
}

std::optional<domain::geo::AxisConvention> mapAxisConvention(const protocol::v1::AxisConvention convention) {
    switch (convention) {
    case protocol::v1::AXIS_CONVENTION_EASTING_NORTHING_UP:
        return domain::geo::AxisConvention::EastingNorthingUp;
    default:
        return std::nullopt;
    }
}

void fillGeoreferenceConfig(
    protocol::v1::GeoreferenceConfig* output,
    const domain::geo::GeoreferenceConfig& config) {
    output->set_horizontal_crs(config.horizontalCrs);
    output->set_linear_unit(config.linearUnit);
    output->set_axis_convention(
        config.axisConvention == domain::geo::AxisConvention::EastingNorthingUp
            ? protocol::v1::AXIS_CONVENTION_EASTING_NORTHING_UP
            : protocol::v1::AXIS_CONVENTION_UNSPECIFIED);
    output->set_origin_easting(config.originEasting);
    output->set_origin_northing(config.originNorthing);
    output->set_origin_height(config.originHeight);
    output->set_vertical_crs(config.verticalCrs);
}

// Projects the resolved canonical georeference into the wire info shape.
void fillGeoreferenceInfo(
    protocol::v1::GeoreferenceInfo* info,
    const domain::geo::GeoreferenceConfig& config,
    const domain::geo::ProjectGeoreference& resolved) {
    fillGeoreferenceConfig(info->mutable_config(), config);

    auto* crs = info->mutable_horizontal_crs();
    crs->set_identifier(resolved.horizontalCrs.identifier);
    crs->set_name(resolved.horizontalCrs.name);
    crs->set_authority(resolved.horizontalCrs.authority);
    crs->set_code(resolved.horizontalCrs.code);
    crs->set_kind(std::string{domain::geo::crsKindName(resolved.horizontalCrs.kind)});
    crs->set_axis_unit_to_metre(resolved.horizontalCrs.axisUnitToMetre);

    info->set_linear_unit_name(resolved.linearUnit.name);
    info->set_linear_unit_to_metre(resolved.linearUnit.toMetre);

    auto* vertical = info->mutable_vertical_reference();
    vertical->set_present(resolved.vertical.present);
    vertical->set_identifier(resolved.vertical.identifier);
    vertical->set_name(resolved.vertical.name);
    vertical->set_transform_supported(resolved.vertical.transformSupported);
    vertical->set_axis_unit_to_metre(resolved.vertical.axisUnitToMetre);
}

void fillSummary(protocol::v1::ProjectSummary* summary, const domain::project::ProjectRecord& record) {
    summary->set_project_uuid(record.uuid);
    summary->set_display_name(record.displayName);
    summary->set_directory(record.directory);
    summary->set_revision(record.revision);
    summary->set_dirty(record.isDirty());
    summary->set_traffic_side(
        record.trafficSide == domain::project::TrafficSide::Left
            ? protocol::v1::TRAFFIC_SIDE_LEFT
            : protocol::v1::TRAFFIC_SIDE_RIGHT);
    summary->set_created_at(record.createdAt);
    summary->set_modified_at(record.modifiedAt);

    fillGeoreferenceConfig(summary->mutable_georeference(), record.georeference);
}

std::string_view commandName(const ProtocolFrame& frame) {
    switch (frame.command().command_case()) {
    case protocol::v1::CommandEnvelope::kCreateProject:
        return "project.create";
    case protocol::v1::CommandEnvelope::kOpenProject:
        return "project.open";
    case protocol::v1::CommandEnvelope::kSaveProject:
        return "project.save";
    case protocol::v1::CommandEnvelope::kSaveProjectAs:
        return "project.save_as";
    case protocol::v1::CommandEnvelope::kCloseProject:
        return "project.close";
    case protocol::v1::CommandEnvelope::kGetProjectSummary:
        return "project.get_summary";
    case protocol::v1::CommandEnvelope::kGetGeoreference:
        return "geo.get_georeference";
    case protocol::v1::CommandEnvelope::kSetGeoreference:
        return "geo.set_georeference";
    case protocol::v1::CommandEnvelope::kTransformToProjectGlobal:
        return "geo.transform_to_project_global";
    case protocol::v1::CommandEnvelope::kTerrainProbeSource:
        return "terrain.probe_source";
    case protocol::v1::CommandEnvelope::kTerrainImportDataset:
        return "terrain.import_dataset";
    case protocol::v1::CommandEnvelope::kTerrainListDatasets:
        return "terrain.list_datasets";
    case protocol::v1::CommandEnvelope::kTerrainGetDataset:
        return "terrain.get_dataset";
    case protocol::v1::CommandEnvelope::kTerrainSample:
        return "terrain.sample";
    case protocol::v1::CommandEnvelope::kTerrainRegenerateTiles:
        return "terrain.regenerate_tiles";
    case protocol::v1::CommandEnvelope::kTerrainGetScene:
        return "terrain.get_scene";
    case protocol::v1::CommandEnvelope::kTerrainListSources:
        return "terrain.list_sources";
    case protocol::v1::CommandEnvelope::kTerrainPlanDownload:
        return "terrain.plan_download";
    case protocol::v1::CommandEnvelope::kTerrainDownloadSelected:
        return "terrain.download_selected";
    case protocol::v1::CommandEnvelope::kTerrainExport:
        return "terrain.export";
    case protocol::v1::CommandEnvelope::kJobCancel:
        return "job.cancel";
    case protocol::v1::CommandEnvelope::kJobList:
        return "job.list";
    case protocol::v1::CommandEnvelope::kCreateRoad:
        return "road.create";
    case protocol::v1::CommandEnvelope::kDeleteRoad:
        return "road.delete";
    case protocol::v1::CommandEnvelope::kRenameRoad:
        return "road.rename";
    case protocol::v1::CommandEnvelope::kInsertRoadControl:
        return "road.insert_control";
    case protocol::v1::CommandEnvelope::kMoveRoadControl:
        return "road.move_control";
    case protocol::v1::CommandEnvelope::kDeleteRoadControl:
        return "road.delete_control";
    case protocol::v1::CommandEnvelope::kFitRoadSource:
        return "road.fit_source";
    case protocol::v1::CommandEnvelope::kUpdateRoadElevation:
        return "road.update_elevation";
    case protocol::v1::CommandEnvelope::kUpdateRoadSuperelevation:
        return "road.update_superelevation";
    case protocol::v1::CommandEnvelope::kUndoRoad:
        return "road.undo";
    case protocol::v1::CommandEnvelope::kRedoRoad:
        return "road.redo";
    case protocol::v1::CommandEnvelope::kListRoads:
        return "road.list";
    case protocol::v1::CommandEnvelope::kGetRoad:
        return "road.get";
    case protocol::v1::CommandEnvelope::kGetRoadScene:
        return "road.get_scene";
    case protocol::v1::CommandEnvelope::COMMAND_NOT_SET:
        break;
    }
    return "unknown";
}

// Stable string names for CommandFailureCode used in job.failed events so
// the frontend can map them without depending on the protobuf enum integer.
std::string_view failureCodeName(const CommandFailureCode code) {
    switch (code) {
    case CommandFailureCode::InvalidArgument:
        return "INVALID_ARGUMENT";
    case CommandFailureCode::ProjectNotOpen:
        return "PROJECT_NOT_OPEN";
    case CommandFailureCode::ProjectAlreadyOpen:
        return "PROJECT_ALREADY_OPEN";
    case CommandFailureCode::ProjectDirectoryInvalid:
        return "PROJECT_DIRECTORY_INVALID";
    case CommandFailureCode::ProjectFormatUnsupported:
        return "PROJECT_FORMAT_UNSUPPORTED";
    case CommandFailureCode::SchemaVersionUnsupported:
        return "SCHEMA_VERSION_UNSUPPORTED";
    case CommandFailureCode::PersistenceFailure:
        return "PERSISTENCE_FAILURE";
    case CommandFailureCode::Internal:
        return "INTERNAL";
    case CommandFailureCode::GeoUnsupported:
        return "GEO_UNSUPPORTED";
    case CommandFailureCode::TerrainUnsupported:
        return "TERRAIN_UNSUPPORTED";
    case CommandFailureCode::NotFound:
        return "NOT_FOUND";
    }
    return "INTERNAL";
}

// Translates terrain-domain failures into the application failure taxonomy.
[[nodiscard]] CommandFailure toCommandFailure(const domain::terrain::TerrainError& error) {
    switch (error.code()) {
    case domain::terrain::TerrainErrorCode::InvalidArgument:
        return CommandFailure(CommandFailureCode::InvalidArgument, error.what());
    case domain::terrain::TerrainErrorCode::MissingCrs:
    case domain::terrain::TerrainErrorCode::UnsupportedRaster:
    case domain::terrain::TerrainErrorCode::CorruptSource:
    case domain::terrain::TerrainErrorCode::SourceUnreadable:
    case domain::terrain::TerrainErrorCode::SourceDataMissing:
    case domain::terrain::TerrainErrorCode::InvalidCoverage:
    case domain::terrain::TerrainErrorCode::TileGenerationFailed:
    case domain::terrain::TerrainErrorCode::NodataCells:
    case domain::terrain::TerrainErrorCode::SuspiciousEncoding:
    case domain::terrain::TerrainErrorCode::ProviderAuthenticationFailed:
    case domain::terrain::TerrainErrorCode::ProviderRateLimited:
    case domain::terrain::TerrainErrorCode::ProviderNetworkTimeout:
    case domain::terrain::TerrainErrorCode::ProviderUnavailable:
    case domain::terrain::TerrainErrorCode::ProviderUnsupportedCoverage:
    case domain::terrain::TerrainErrorCode::ProviderInvalidResponse:
    case domain::terrain::TerrainErrorCode::ProviderCorruptResponse:
    case domain::terrain::TerrainErrorCode::SelectionTooLarge:
        return CommandFailure(CommandFailureCode::TerrainUnsupported, error.what());
    }
    return CommandFailure(CommandFailureCode::TerrainUnsupported, error.what());
}

[[nodiscard]] CommandFailure toCommandFailure(const domain::geo::GeoError& error) {
    switch (error.code()) {
    case domain::geo::GeoErrorCode::InvalidCrs:
    case domain::geo::GeoErrorCode::NotFinite:
        return CommandFailure(CommandFailureCode::InvalidArgument, error.what());
    case domain::geo::GeoErrorCode::UnsupportedCrs:
    case domain::geo::GeoErrorCode::UnsupportedUnit:
    case domain::geo::GeoErrorCode::UnsupportedTransform:
        return CommandFailure(CommandFailureCode::GeoUnsupported, error.what());
    case domain::geo::GeoErrorCode::LibraryUnavailable:
    case domain::geo::GeoErrorCode::LibraryFailure:
        return CommandFailure(CommandFailureCode::Internal, error.what());
    }
    return CommandFailure(CommandFailureCode::Internal, error.what());
}

void fillTerrainDatasetInfo(
    protocol::v1::TerrainDatasetInfo* info, const domain::terrain::TerrainDataset& dataset) {
    info->set_dataset_uuid(domain::terrain::uuidTextFromEntityId(dataset.id));
    info->set_display_name(dataset.displayName);
    info->set_storage_path(dataset.storagePath);
    info->set_source_format(dataset.sourceFormat);
    info->set_source_crs(dataset.sourceCrs);
    info->set_raster_width(dataset.rasterWidth);
    info->set_raster_height(dataset.rasterHeight);
    info->set_cell_size_x(dataset.cellSizeX);
    info->set_cell_size_y(dataset.cellSizeY);
    info->set_elevation_unit(dataset.elevationUnit);
    info->set_has_nodata(dataset.hasNodata);
    info->set_nodata_value(dataset.nodataValue);
    info->set_min_z(dataset.minZ);
    info->set_max_z(dataset.maxZ);
    info->set_bounds_east(dataset.bounds.maxEasting);
    info->set_bounds_west(dataset.bounds.minEasting);
    info->set_bounds_north(dataset.bounds.maxNorthing);
    info->set_bounds_south(dataset.bounds.minNorthing);
    info->set_revision(dataset.revision);
    for (const domain::terrain::TerrainDiagnostic& diagnostic : dataset.diagnostics) {
        auto* entry = info->add_diagnostics();
        entry->set_code(std::string{domain::terrain::terrainErrorCodeName(diagnostic.code)});
        entry->set_message(diagnostic.message);
    }
    info->set_created_at(dataset.createdAt);
    info->set_source_attribution(dataset.sourceAttribution);
    info->set_horizontal_unit_name(dataset.horizontalUnitName);
    info->set_horizontal_unit_symbol(dataset.horizontalUnitSymbol);
    info->set_horizontal_unit_is_angular(dataset.horizontalUnitIsAngular);
    info->set_elevation_unit_source(dataset.elevationUnitSource);
    info->set_sample_scale(dataset.sampleScale);
    info->set_sample_offset(dataset.sampleOffset);
}

protocol::v1::JobState mapJobState(const JobState state) {
    switch (state) {
    case JobState::Queued:
        return protocol::v1::JOB_STATE_QUEUED;
    case JobState::Running:
        return protocol::v1::JOB_STATE_RUNNING;
    case JobState::Completed:
        return protocol::v1::JOB_STATE_COMPLETED;
    case JobState::Failed:
        return protocol::v1::JOB_STATE_FAILED;
    case JobState::Cancelled:
        return protocol::v1::JOB_STATE_CANCELLED;
    }
    return protocol::v1::JOB_STATE_UNSPECIFIED;
}

void fillJobRecord(protocol::v1::JobRecord* output, const JobRecord& record) {
    output->set_job_id(record.jobId);
    output->set_operation(record.kind);
    output->set_state(mapJobState(record.state));
    if (record.progress.normalized > 0.0 || record.progress.unitsTotal > 0) {
        output->set_progress(record.progress.normalized);
    }
    if (record.progress.unitsTotal > 0) {
        output->set_processed(record.progress.unitsDone);
        output->set_total(record.progress.unitsTotal);
    }
    output->set_label(record.progress.label);
    output->set_message(record.message);
    output->set_created_at(record.createdAt);
    output->set_cancellable(record.cancellable);
}

} // namespace

CommandProcessor::CommandProcessor(
    ports::ProjectStore& store,
    const domain::geo::GeoTransformService& transforms,
    CommandSink& sink)
    : store_(store),
      service_(store, transforms),
      geoService_(store, transforms),
      sink_(sink) {
    // The job system marshals worker completions onto this executor; the
    // terrain service emits its events through the shared publishing path.
    terrainReader_.emplace();
    jobs_.emplace([this](std::function<void()> task) { postTask(std::move(task)); });
    world_ = WorldState{};
    terrainService_.emplace(store_, transforms, *terrainReader_, world_, *jobs_,
        production::makeProductionTerrainProviders(),
        [this](const TerrainServiceEvent& event) { publishTerrainEvent(event); });
    roadService_.emplace(store_, world_,
        [this](const RoadServiceEvent& event) { publishRoadEvent(event); });
}

CommandProcessor::~CommandProcessor() {
    shutdown();
}

void CommandProcessor::start() {
    std::lock_guard lock{mutex_};
    if (started_) {
        return;
    }
    started_ = true;
    stopped_ = false;
    executor_ = std::thread([this] { runExecutor(); });
}

void CommandProcessor::post(std::string connectionId, protocol::v1::Frame frame) {
    const std::string jobId = runtime::generateUuidV4();
    {
        std::lock_guard lock{mutex_};
        if (stopped_) {
            // The engine is shutting down; connections are closing anyway.
            // No job.queued is emitted (post() is enqueue-only), so no
            // orphan pending job is created.
            runtime::logWarn("application", "command.dropped_at_shutdown",
                {{"command", frame.has_command() ? commandName(frame) : std::string_view{"unknown"}}});
            return;
        }
        // Enqueue only: no ProjectStore read, no broadcastEvent, no job.queued.
        // The job lifecycle (queued -> started -> terminal) is owned entirely
        // by processCommand() on the executor thread, preserving the single
        // application-executor ownership model.
        queue_.push_back({std::move(connectionId), jobId, std::make_unique<ProtocolFrame>(std::move(frame))});
    }
    signal_.notify_one();
}

void CommandProcessor::postTask(std::function<void()> task) {
    {
        std::lock_guard lock{mutex_};
        if (stopped_) {
            runtime::logWarn("application", "task.dropped_at_shutdown", {});
            return;
        }
        tasks_.push_back({std::move(task)});
    }
    signal_.notify_one();
}

void CommandProcessor::shutdown() {
    {
        std::lock_guard lock{mutex_};
        if (stopped_) {
            return;
        }
        stopped_ = true;
        const std::size_t discarded = queue_.size() + tasks_.size();
        queue_.clear();
        tasks_.clear();
        if (discarded > 0) {
            // Commands discarded during shutdown never had job.queued
            // emitted (post() is enqueue-only), so no orphan pending jobs
            // are created.
            runtime::logWarn("application", "command.queue_discarded", {{"count", std::to_string(discarded)}});
        }
    }
    signal_.notify_all();

    // Deterministic shutdown order that prevents cross-thread mutation of
    // executor-owned state:
    //
    // 1. Join the application executor FIRST. The executor is the sole
    //    mutator of canonical project/terrain state (ProjectStore,
    //    TerrainService datasets_, world_, etc.). Once stopped_ is true and
    //    the queue is cleared, the executor finishes its current command
    //    (if any) and returns. After the join, no thread mutates
    //    executor-owned state.
    //
    // 2. Shut down the JobSystem (cancel + join the background worker). The
    //    worker body captures TerrainService/reader state; TerrainService is
    //    still alive (member of CommandProcessor, destroyed later). The
    //    worker's completion callback would post to the executor, but
    //    postTask() drops it (stopped_ = true), so no callback runs against
    //    destroyed objects. The worker is joined before TerrainService is
    //    destroyed, preventing use-after-free.
    //
    // 3. Call terrainService_->onProjectClosed() — now safe because both the
    //    executor and the worker are dead; no concurrent access to
    //    TerrainService state.
    //
    // This ordering cannot deadlock: the executor never waits for the worker
    // to complete (command dispatch returns immediately after submitting a
    // background job), so joining the executor before shutting down the
    // JobSystem is safe.
    if (executor_.joinable()) {
        executor_.join();
    }
    if (jobs_) {
        jobs_->shutdown();
    }
    if (terrainService_) {
        terrainService_->onProjectClosed();
    }
    if (roadService_) {
        roadService_->onProjectClosed();
    }
}

void CommandProcessor::runExecutor() {
    std::unique_lock lock{mutex_};
    for (;;) {
        signal_.wait(lock, [this] { return stopped_ || !queue_.empty() || !tasks_.empty(); });
        if (stopped_) {
            return;
        }
        // Process posted tasks (job completion callbacks) with priority so
        // long-running job finalization is not delayed by queued commands.
        if (!tasks_.empty()) {
            auto task = std::move(tasks_.front());
            tasks_.pop_front();
            lock.unlock();
            try {
                if (task.task) {
                    task.task();
                }
            } catch (const std::exception& error) {
                runtime::logError("application", "task.executor_unexpected_error", {{"detail", error.what()}});
            } catch (...) {
                runtime::logError("application", "task.executor_unexpected_error", {{"detail", "unknown exception"}});
            }
            lock.lock();
            continue;
        }
        if (queue_.empty()) {
            continue;
        }
        auto command = std::move(queue_.front());
        queue_.pop_front();
        lock.unlock();
        try {
            processCommand(command.connectionId, command.jobId, *command.frame);
        } catch (const std::exception& error) {
            // processCommand owns failure translation; a leak through here is
            // an engine bug and must not kill the executor thread silently.
            runtime::logError("application", "command.executor_unexpected_error", {{"detail", error.what()}});
        } catch (...) {
            runtime::logError("application", "command.executor_unexpected_error", {{"detail", "unknown exception"}});
        }
        lock.lock();
    }
}

void CommandProcessor::processCommand(
    const std::string& connectionId,
    const std::string& jobId,
    const ProtocolFrame& frame) {
    const std::string_view label = frame.has_command() ? commandName(frame) : std::string_view{"unknown"};
    const std::string requestId = frame.request_id();

    // Derive targetId safely on the executor thread. This is the only place
    // ProjectStore is read for job metadata; post() does not read it.
    const std::string targetId = store_.isOpen() ? store_.current().uuid : std::string{};

    // Lifecycle envelope: emit queued -> started, dispatch, emit terminal.
    // Every path through this function emits exactly one terminal event.
    // For async terrain commands (import, regenerate), the command's
    // completed event means "command accepted"; the background job has its
    // own lifecycle with a separate job ID.
    publishJobQueued(jobId, label, label, targetId, false);
    publishJobStarted(jobId);

    const auto startedAt = std::chrono::steady_clock::now();

    try {
        switch (frame.command().command_case()) {
        case protocol::v1::CommandEnvelope::kCreateProject:
            handleCreateProject(connectionId, frame);
            break;
        case protocol::v1::CommandEnvelope::kOpenProject:
            handleOpenProject(connectionId, frame);
            break;
        case protocol::v1::CommandEnvelope::kSaveProject:
            runServiceCommand(connectionId, frame, [this] { return service_.save(); });
            break;
        case protocol::v1::CommandEnvelope::kSaveProjectAs:
            handleSaveProjectAs(connectionId, frame);
            break;
        case protocol::v1::CommandEnvelope::kCloseProject:
            runServiceCommand(connectionId, frame, [this] { return service_.close(); });
            break;
        case protocol::v1::CommandEnvelope::kGetProjectSummary:
            runServiceCommand(connectionId, frame, [this] { return service_.getSummary(); });
            break;
        case protocol::v1::CommandEnvelope::kGetGeoreference:
            handleGetGeoreference(connectionId, frame);
            break;
        case protocol::v1::CommandEnvelope::kSetGeoreference:
            handleSetGeoreference(connectionId, frame);
            break;
        case protocol::v1::CommandEnvelope::kTransformToProjectGlobal:
            handleTransformToProjectGlobal(connectionId, frame);
            break;
        case protocol::v1::CommandEnvelope::kTerrainProbeSource:
            handleTerrainProbeSource(connectionId, frame);
            break;
        case protocol::v1::CommandEnvelope::kTerrainImportDataset:
            handleTerrainImportDataset(connectionId, frame);
            break;
        case protocol::v1::CommandEnvelope::kTerrainListDatasets:
            handleTerrainListDatasets(connectionId, frame);
            break;
        case protocol::v1::CommandEnvelope::kTerrainGetDataset:
            handleTerrainGetDataset(connectionId, frame);
            break;
        case protocol::v1::CommandEnvelope::kTerrainSample:
            handleTerrainSample(connectionId, frame);
            break;
        case protocol::v1::CommandEnvelope::kTerrainRegenerateTiles:
            handleTerrainRegenerateTiles(connectionId, frame);
            break;
        case protocol::v1::CommandEnvelope::kTerrainGetScene:
            handleTerrainGetScene(connectionId, frame);
            break;
        case protocol::v1::CommandEnvelope::kTerrainListSources:
            handleTerrainListSources(connectionId, frame);
            break;
        case protocol::v1::CommandEnvelope::kTerrainPlanDownload:
            handleTerrainPlanDownload(connectionId, frame);
            break;
        case protocol::v1::CommandEnvelope::kTerrainDownloadSelected:
            handleTerrainDownloadSelected(connectionId, frame);
            break;
        case protocol::v1::CommandEnvelope::kTerrainExport:
            handleTerrainExport(connectionId, frame);
            break;
        case protocol::v1::CommandEnvelope::kJobCancel:
            handleJobCancel(connectionId, frame);
            break;
        case protocol::v1::CommandEnvelope::kJobList:
            handleJobList(connectionId, frame);
            break;
        // Road commands.
        case protocol::v1::CommandEnvelope::kCreateRoad:
            handleCreateRoad(connectionId, frame);
            break;
        case protocol::v1::CommandEnvelope::kDeleteRoad:
            handleDeleteRoad(connectionId, frame);
            break;
        case protocol::v1::CommandEnvelope::kRenameRoad:
            handleRenameRoad(connectionId, frame);
            break;
        case protocol::v1::CommandEnvelope::kInsertRoadControl:
            handleInsertRoadControl(connectionId, frame);
            break;
        case protocol::v1::CommandEnvelope::kMoveRoadControl:
            handleMoveRoadControl(connectionId, frame);
            break;
        case protocol::v1::CommandEnvelope::kDeleteRoadControl:
            handleDeleteRoadControl(connectionId, frame);
            break;
        case protocol::v1::CommandEnvelope::kFitRoadSource:
            handleFitRoadSource(connectionId, frame);
            break;
        case protocol::v1::CommandEnvelope::kUpdateRoadElevation:
            handleUpdateRoadElevation(connectionId, frame);
            break;
        case protocol::v1::CommandEnvelope::kUpdateRoadSuperelevation:
            handleUpdateRoadSuperelevation(connectionId, frame);
            break;
        case protocol::v1::CommandEnvelope::kUndoRoad:
            handleUndoRoad(connectionId, frame);
            break;
        case protocol::v1::CommandEnvelope::kRedoRoad:
            handleRedoRoad(connectionId, frame);
            break;
        case protocol::v1::CommandEnvelope::kListRoads:
            handleListRoads(connectionId, frame);
            break;
        case protocol::v1::CommandEnvelope::kGetRoad:
            handleGetRoad(connectionId, frame);
            break;
        case protocol::v1::CommandEnvelope::kGetRoadScene:
            handleGetRoadScene(connectionId, frame);
            break;
        case protocol::v1::CommandEnvelope::COMMAND_NOT_SET:
            throw CommandFailure{CommandFailureCode::InvalidArgument, "command envelope is empty"};
        }

        const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startedAt);
        runtime::logInfo("application", "project.command_handled",
            {{"command", std::string{label}},
                {"requestId", requestId},
                {"outcome", "ok"},
                {"durationMs", std::to_string(duration.count())}});

        // Terminal success event emitted exactly once for the command dispatch.
        publishJobCompleted(jobId);
    } catch (const CommandFailure& failure) {
        sendFailureResult(connectionId, requestId, failure);
        const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startedAt);
        runtime::logInfo("application", "project.command_failed",
            {{"command", std::string{label}},
                {"requestId", requestId},
                {"outcome", std::to_string(static_cast<int>(failure.code()))},
                {"durationMs", std::to_string(duration.count())}});

        // Terminal failure event emitted exactly once with structured error.
        publishJobFailed(jobId, failureCodeName(failure.code()), failure.what());
    } catch (const domain::terrain::TerrainError& error) {
        const CommandFailure failure = toCommandFailure(error);
        sendFailureResult(connectionId, requestId, failure);
        publishJobFailed(jobId, failureCodeName(failure.code()), failure.what());
    } catch (const domain::geo::GeoError& error) {
        const CommandFailure failure = toCommandFailure(error);
        sendFailureResult(connectionId, requestId, failure);
        publishJobFailed(jobId, failureCodeName(failure.code()), failure.what());
    } catch (const std::exception& error) {
        // Internal details stay in the log; the client receives a stable
        // internal error code.
        runtime::logError("application", "project.command_internal_error",
            {{"command", std::string{label}}, {"detail", error.what()}});
        sendInternalErrorResult(connectionId, requestId, label);

        publishJobFailed(jobId, "INTERNAL", "internal engine error while handling " + std::string{label});
    }
}

void CommandProcessor::handleCreateProject(const std::string& connectionId, const ProtocolFrame& frame) {
    const auto& command = frame.command().create_project();
    const auto trafficSide = mapTrafficSide(command.traffic_side());
    const auto axisConvention = mapAxisConvention(command.georeference().axis_convention());
    if (!trafficSide.has_value() || !axisConvention.has_value()) {
        // Throw so processCommand's lifecycle envelope emits job.failed.
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            !trafficSide.has_value() ? "traffic_side must be LEFT or RIGHT"
                                     : "axis_convention must be EASTING_NORTHING_UP"};
    }

    domain::project::CreateProjectSpec spec;
    spec.displayName = command.display_name();
    spec.parentDirectory = runtime::pathFromUtf8(command.parent_directory());
    spec.trafficSide = *trafficSide;
    spec.georeference.horizontalCrs = command.georeference().horizontal_crs();
    spec.georeference.linearUnit = command.georeference().linear_unit();
    spec.georeference.axisConvention = *axisConvention;
    spec.georeference.originEasting = command.georeference().origin_easting();
    spec.georeference.originNorthing = command.georeference().origin_northing();
    spec.georeference.verticalCrs = command.georeference().vertical_crs();

    runServiceCommand(connectionId, frame, [this, spec = std::move(spec)] { return service_.create(spec); });
}

void CommandProcessor::handleOpenProject(const std::string& connectionId, const ProtocolFrame& frame) {
    const auto& command = frame.command().open_project();
    runServiceCommand(connectionId, frame,
        [this, directory = runtime::pathFromUtf8(command.project_directory())] {
            return service_.open(directory);
        });
}

void CommandProcessor::handleSaveProjectAs(const std::string& connectionId, const ProtocolFrame& frame) {
    const auto& command = frame.command().save_project_as();
    domain::project::SaveAsSpec spec;
    spec.displayName = command.display_name();
    spec.parentDirectory = runtime::pathFromUtf8(command.parent_directory());
    runServiceCommand(connectionId, frame, [this, spec = std::move(spec)] { return service_.saveAs(spec); });
}

template <typename UseCase>
void CommandProcessor::runServiceCommand(
    const std::string& connectionId,
    const ProtocolFrame& frame,
    UseCase&& useCase) {
    executeCommand([this, &connectionId, &frame, &useCase] {
        const ProjectCommandResult result = useCase();

        // Terrain session state (world partition + dataset registry) follows
        // the project session lifecycle events.
        for (const ProjectEvent& event : result.events) {
            if (event.kind == ProjectEventKind::Opened || event.kind == ProjectEventKind::Closed) {
                syncTerrainSession();
                break;
            }
        }

        ProtocolFrame response;
        response.set_request_id(frame.request_id());
        if (result.sessionClosed) {
            response.mutable_result()->mutable_project_closed()->set_project_uuid(result.record.uuid);
        } else {
            fillSummary(response.mutable_result()->mutable_project_state()->mutable_summary(), result.record);
        }
        sink_.sendToConnection(connectionId, response);
        publishEvents(result.events);

        // After project open/create, evaluate the vertical reference state
        // and emit diagnostic events for the real "no vertical reference"
        // condition. After close, clear project-scoped diagnostics.
        for (const ProjectEvent& event : result.events) {
            if (event.kind == ProjectEventKind::Opened) {
                evaluateVerticalReferenceDiagnostic(event.record);
            } else if (event.kind == ProjectEventKind::Closed) {
                publishDiagnosticCleared("georeference");
            }
        }
    });
}

void CommandProcessor::handleGetGeoreference(const std::string& connectionId, const ProtocolFrame& frame) {
    executeCommand([this, &connectionId, &frame] {
        const GeoreferenceInfoResult result = geoService_.getGeoreference();

        ProtocolFrame response;
        response.set_request_id(frame.request_id());
        auto* state = response.mutable_result()->mutable_georeference_state();
        fillGeoreferenceInfo(state->mutable_georeference(), result.config, result.resolved);
        state->set_revision(result.revision);
        sink_.sendToConnection(connectionId, response);
    });
}

void CommandProcessor::handleSetGeoreference(const std::string& connectionId, const ProtocolFrame& frame) {
    const auto& command = frame.command().set_georeference();
    const auto axisConvention = mapAxisConvention(command.georeference().axis_convention());
    if (!axisConvention.has_value()) {
        // Throw so processCommand's lifecycle envelope emits job.failed.
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "axis_convention must be EASTING_NORTHING_UP"};
    }
    if (terrainService_->hasDatasets()) {
        // Imported terrain coverage is derived from the canonical
        // georeference; changing it would silently invalidate stored
        // dataset bounds. The conflict is rejected explicitly instead.
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "the project georeference cannot change while terrain datasets exist"};
    }

    domain::geo::GeoreferenceConfig config;
    config.horizontalCrs = command.georeference().horizontal_crs();
    config.linearUnit = command.georeference().linear_unit();
    config.axisConvention = *axisConvention;
    config.originEasting = command.georeference().origin_easting();
    config.originNorthing = command.georeference().origin_northing();
    config.originHeight = command.georeference().origin_height();
    config.verticalCrs = command.georeference().vertical_crs();

    const std::optional<std::uint64_t> expectedRevision = command.has_expected_revision()
        ? std::optional<std::uint64_t>{command.expected_revision()}
        : std::nullopt;

    executeCommand(
        [this, &connectionId, &frame, config = std::move(config), expectedRevision] {
            const GeoreferenceMutationResult result =
                geoService_.setGeoreference(config, expectedRevision);

            ProtocolFrame response;
            response.set_request_id(frame.request_id());
            auto* state = response.mutable_result()->mutable_georeference_state();
            fillGeoreferenceInfo(state->mutable_georeference(), result.record.georeference, result.resolved);
            state->set_revision(result.record.revision);
            sink_.sendToConnection(connectionId, response);
            publishEvents(result.events);

            // After a georeference change, re-evaluate the vertical reference
            // diagnostic so the Problems panel reflects the new state.
            evaluateVerticalReferenceDiagnostic(result.record);
        });
}

void CommandProcessor::handleTransformToProjectGlobal(
    const std::string& connectionId,
    const ProtocolFrame& frame) {
    const auto& command = frame.command().transform_to_project_global();
    if (command.coordinates_size() > static_cast<int>(kMaxTransformCoordinates)) {
        // Throw so processCommand's lifecycle envelope emits job.failed.
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "transform batch exceeds the " + std::to_string(kMaxTransformCoordinates)
                + " coordinate limit"};
    }

    domain::geo::SourceSpatialReference source;
    source.horizontalCrs = command.source_crs();
    source.verticalCrs = command.source_vertical_crs();

    std::vector<domain::geo::GeoCoordinate> coordinates;
    coordinates.reserve(static_cast<std::size_t>(command.coordinates_size()));
    for (const auto& coordinate : command.coordinates()) {
        coordinates.push_back({coordinate.x(), coordinate.y(), coordinate.z()});
    }

    executeCommand(
        [this, &connectionId, &frame, source = std::move(source), coordinates = std::move(coordinates)] {
            const std::vector<domain::geo::ProjectGlobalPosition> positions =
                geoService_.transformToProjectGlobal(source, coordinates);

            ProtocolFrame response;
            response.set_request_id(frame.request_id());
            auto* result = response.mutable_result()->mutable_transform_to_project_global();
            for (const auto& position : positions) {
                auto* coordinate = result->add_coordinates();
                coordinate->set_easting(position.easting);
                coordinate->set_northing(position.northing);
                coordinate->set_height(position.height);
            }
            sink_.sendToConnection(connectionId, response);
        });
}

// --- Terrain command handlers ---

void CommandProcessor::handleTerrainProbeSource(
    const std::string& connectionId, const ProtocolFrame& frame) {
    const auto& command = frame.command().terrain_probe_source();
    if (command.path().empty()) {
        throw CommandFailure{CommandFailureCode::InvalidArgument, "path must not be empty"};
    }
    executeCommand([this, &connectionId, &frame, path = runtime::pathFromUtf8(command.path())] {
        const TerrainProbeResult result = terrainService_->probeSource(path);

        ProtocolFrame response;
        response.set_request_id(frame.request_id());
        auto* probe = response.mutable_result()->mutable_terrain_probe_source_result();
        auto* source = probe->mutable_source();
        source->set_format(result.source.format);
        source->set_crs_definition(result.source.crsDefinition);
        source->set_width(result.source.width);
        source->set_height(result.source.height);
        source->set_origin_x(result.source.originX);
        source->set_origin_y(result.source.originY);
        source->set_pixel_size_x(result.source.pixelSizeX);
        source->set_pixel_size_y(result.source.pixelSizeY);
        source->set_elevation_unit(result.source.elevationUnit);
        source->set_sample_type(result.source.sampleTypeName);
        source->set_has_nodata(result.source.hasNodata);
        source->set_nodata_value(result.source.nodataValue);
        source->set_file_bytes(result.source.fileBytes);
        source->set_horizontal_unit_name(result.source.horizontalUnitName);
        source->set_horizontal_unit_symbol(result.source.horizontalUnitSymbol);
        source->set_horizontal_unit_is_angular(result.source.horizontalUnitIsAngular);
        source->set_sample_scale(result.source.sampleScale);
        source->set_sample_offset(result.source.sampleOffset);
        source->set_elevation_unit_source(result.source.elevationUnitSource);
        probe->set_crs_name(result.crsName);
        probe->set_crs_kind(result.crsKind);
        probe->set_crs_authority(result.crsAuthority);
        probe->set_crs_code(result.crsCode);
        sink_.sendToConnection(connectionId, response);
    });
}

void CommandProcessor::handleTerrainImportDataset(
    const std::string& connectionId, const ProtocolFrame& frame) {
    const auto& command = frame.command().terrain_import_dataset();
    if (command.path().empty() || command.display_name().empty()) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "path and display_name must not be empty"};
    }
    executeCommand(
        [this, &connectionId, &frame, path = runtime::pathFromUtf8(command.path()),
            displayName = command.display_name(), elevationUnitOverride = command.elevation_unit_override()] {
            TerrainImportSpec spec;
            spec.sourcePath = path;
            spec.displayName = displayName;
            spec.elevationUnitOverride = elevationUnitOverride;
            const JobRecord record = terrainService_->startImport(spec);

            // Return the background job ID immediately. The command dispatch
            // lifecycle completes (meaning "command accepted"); the background
            // job lifecycle is the real long operation tracked by Operations.
            ProtocolFrame response;
            response.set_request_id(frame.request_id());
            auto* started = response.mutable_result()->mutable_job_started();
            started->set_job_id(record.jobId);
            sink_.sendToConnection(connectionId, response);

            // Emit the background job's queued event.
            TerrainServiceEvent event;
            event.job = record;
            event.revision = store_.isOpen() ? store_.current().revision : 0;
            publishTerrainEvent(event);
        });
}

void CommandProcessor::handleTerrainListDatasets(
    const std::string& connectionId, const ProtocolFrame& frame) {
    executeCommand([this, &connectionId, &frame] {
        const std::vector<domain::terrain::TerrainDataset> datasets = terrainService_->listDatasets();

        ProtocolFrame response;
        response.set_request_id(frame.request_id());
        auto* result = response.mutable_result()->mutable_terrain_list_datasets_result();
        for (const domain::terrain::TerrainDataset& dataset : datasets) {
            fillTerrainDatasetInfo(result->add_datasets(), dataset);
        }
        sink_.sendToConnection(connectionId, response);
    });
}

void CommandProcessor::handleTerrainGetDataset(
    const std::string& connectionId, const ProtocolFrame& frame) {
    const auto& command = frame.command().terrain_get_dataset();
    executeCommand(
        [this, &connectionId, &frame, uuid = command.dataset_uuid()] {
            const TerrainDatasetDetails details = terrainService_->datasetDetails(uuid);

            ProtocolFrame response;
            response.set_request_id(frame.request_id());
            auto* result = response.mutable_result()->mutable_terrain_get_dataset_result();
            fillTerrainDatasetInfo(result->mutable_dataset(), details.dataset);
            result->set_expected_tiles(details.expectedTiles);
            result->set_present_tiles(details.presentTiles);
            sink_.sendToConnection(connectionId, response);
        });
}

void CommandProcessor::handleTerrainSample(
    const std::string& connectionId, const ProtocolFrame& frame) {
    const auto& command = frame.command().terrain_sample();
    executeCommand(
        [this, &connectionId, &frame, uuid = command.dataset_uuid(), easting = command.easting(),
            northing = command.northing()] {
            const TerrainSampleResult result = terrainService_->sample(uuid, easting, northing);

            ProtocolFrame response;
            response.set_request_id(frame.request_id());
            auto* sample = response.mutable_result()->mutable_terrain_sample_result();
            sample->set_dataset_uuid(result.datasetUuid);
            switch (result.sample.status) {
            case domain::terrain::TerrainSampleStatus::Height:
                sample->set_status(protocol::v1::TERRAIN_SAMPLE_STATUS_HEIGHT);
                sample->set_height(result.sample.height);
                break;
            case domain::terrain::TerrainSampleStatus::NoData:
                sample->set_status(protocol::v1::TERRAIN_SAMPLE_STATUS_NODATA);
                break;
            case domain::terrain::TerrainSampleStatus::OutsideCoverage:
                sample->set_status(protocol::v1::TERRAIN_SAMPLE_STATUS_OUTSIDE_COVERAGE);
                break;
            }
            sink_.sendToConnection(connectionId, response);
        });
}

void CommandProcessor::handleTerrainRegenerateTiles(
    const std::string& connectionId, const ProtocolFrame& frame) {
    const auto& command = frame.command().terrain_regenerate_tiles();
    executeCommand([this, &connectionId, &frame, uuid = command.dataset_uuid()] {
        const JobRecord record = terrainService_->regenerateTiles(uuid);

        ProtocolFrame response;
        response.set_request_id(frame.request_id());
        auto* started = response.mutable_result()->mutable_job_started();
        started->set_job_id(record.jobId);
        sink_.sendToConnection(connectionId, response);

        // Emit the background job's queued event.
        TerrainServiceEvent event;
        event.job = record;
        event.revision = store_.isOpen() ? store_.current().revision : 0;
        publishTerrainEvent(event);
    });
}

void CommandProcessor::handleTerrainGetScene(
    const std::string& connectionId, const ProtocolFrame& frame) {
    executeCommand([this, &connectionId, &frame] {
        const TerrainSceneProjection projection = terrainService_->sceneProjection();

        ProtocolFrame response;
        response.set_request_id(frame.request_id());
        auto* scene = response.mutable_result()->mutable_terrain_scene_result();
        scene->set_origin_easting(projection.renderOrigin.easting);
        scene->set_origin_northing(projection.renderOrigin.northing);
        scene->set_origin_height(projection.renderOrigin.height);
        for (const TerrainSceneTile& tile : projection.tiles) {
            auto* message = scene->add_tiles();
            message->set_dataset_uuid(tile.datasetUuid);
            message->set_dataset_revision(tile.datasetRevision);
            message->set_chunk_x(tile.chunkX);
            message->set_chunk_y(tile.chunkY);
            message->set_absolute_path(tile.absolutePath);
            message->set_min_easting(tile.minEasting);
            message->set_min_northing(tile.minNorthing);
            message->set_max_easting(tile.maxEasting);
            message->set_max_northing(tile.maxNorthing);
        }
        scene->set_missing_tiles(projection.missingTiles);
        scene->set_revision(projection.revision);
        sink_.sendToConnection(connectionId, response);
    });
}

void CommandProcessor::handleTerrainListSources(
    const std::string& connectionId, const ProtocolFrame& frame) {
    executeCommand([this, &connectionId, &frame] {
        const auto providers = terrainService_->listSources();

        ProtocolFrame response;
        response.set_request_id(frame.request_id());
        auto* result = response.mutable_result()->mutable_terrain_list_sources_result();
        for (const auto& info : providers) {
            auto* p = result->add_providers();
            p->set_provider_id(info.providerId);
            p->set_display_name(info.displayName);
            p->set_attribution(info.attribution);
            p->set_requires_auth(info.requiresAuth);
            p->set_max_resolution_mpp(info.maxResolutionMpp);
            p->set_coverage_west(info.coverage.west);
            p->set_coverage_south(info.coverage.south);
            p->set_coverage_east(info.coverage.east);
            p->set_coverage_north(info.coverage.north);
        }
        sink_.sendToConnection(connectionId, response);
    });
}

void CommandProcessor::handleTerrainPlanDownload(
    const std::string& connectionId, const ProtocolFrame& frame) {
    executeCommand([this, &connectionId, &frame] {
        const auto& command = frame.command().terrain_plan_download();
        domain::terrain::GeoBounds area;
        area.west = command.area().west();
        area.south = command.area().south();
        area.east = command.area().east();
        area.north = command.area().north();
        std::vector<std::int32_t> selectedIndices(
            command.selected_indices().begin(), command.selected_indices().end());

        const auto plan = terrainService_->planDownload(
            command.provider_id(), area, command.tile_size_metres(), selectedIndices);

        ProtocolFrame response;
        response.set_request_id(frame.request_id());
        auto* result = response.mutable_result()->mutable_terrain_plan_download_result();
        auto* planMsg = result->mutable_plan();
        planMsg->set_provider_id(plan.providerId);
        for (const auto& tile : plan.selectionTiles) {
            auto* t = planMsg->add_selection_tiles();
            t->set_col(tile.col);
            t->set_row(tile.row);
            auto* b = t->mutable_bounds();
            b->set_west(tile.bounds.west);
            b->set_south(tile.bounds.south);
            b->set_east(tile.bounds.east);
            b->set_north(tile.bounds.north);
            t->set_area_sqm(tile.areaSqm);
        }
        for (auto idx : plan.selectedIndices) {
            planMsg->add_selected_indices(idx);
        }
        for (const auto& req : plan.providerRequests) {
            auto* r = planMsg->add_provider_requests();
            r->set_request_id(req.requestId);
            auto* b = r->mutable_bounds();
            b->set_west(req.bounds.west);
            b->set_south(req.bounds.south);
            b->set_east(req.bounds.east);
            b->set_north(req.bounds.north);
            r->set_estimated_bytes(req.estimatedBytes);
        }
        planMsg->set_request_count(plan.requestCount);
        planMsg->set_deduplicated_request_count(plan.deduplicatedRequestCount);
        planMsg->set_effective_resolution_mpp(plan.effectiveResolutionMpp);
        planMsg->set_estimated_bytes(plan.estimatedBytes);
        for (const auto& w : plan.warnings) {
            planMsg->add_warnings(w);
        }
        planMsg->set_full_coverage(plan.fullCoverage);
        planMsg->set_total_tile_count(plan.totalTileCount);
        planMsg->set_selected_tile_count(plan.selectedTileCount);
        planMsg->set_selected_area_sqm(plan.selectedAreaSqm);
        sink_.sendToConnection(connectionId, response);
    });
}

void CommandProcessor::handleTerrainDownloadSelected(
    const std::string& connectionId, const ProtocolFrame& frame) {
    const auto& command = frame.command().terrain_download_selected();
    domain::terrain::GeoBounds area;
    area.west = command.area().west();
    area.south = command.area().south();
    area.east = command.area().east();
    area.north = command.area().north();
    std::vector<std::int32_t> selectedIndices(
        command.selected_indices().begin(), command.selected_indices().end());

    const JobRecord record = terrainService_->startDownload(
        command.provider_id(), area, command.tile_size_metres(),
        selectedIndices, command.display_name());

    ProtocolFrame response;
    response.set_request_id(frame.request_id());
    auto* result = response.mutable_result()->mutable_job_started();
    result->set_job_id(record.jobId);
    sink_.sendToConnection(connectionId, response);

    // Emit the background job's queued event so all subscribers (including
    // the frontend terrain event projector) learn about terrain.download.
    TerrainServiceEvent event;
    event.job = record;
    event.revision = store_.isOpen() ? store_.current().revision : 0;
    publishTerrainEvent(event);
}

void CommandProcessor::handleTerrainExport(
    const std::string& connectionId, const ProtocolFrame& frame) {
    const auto& command = frame.command().terrain_export();
    executeCommand([this, &connectionId, &frame, &command] {
        TerrainExportOptions options;
        options.datasetUuid = command.dataset_uuid();
        options.outputDirectory = command.output_directory();
        options.targetResolution = command.target_resolution();
        options.targetCrs = command.target_crs().empty() ? "auto" : command.target_crs();

        switch (command.heightmap_format()) {
            case protocol::v1::TERRAIN_EXPORT_FORMAT_GEOTIFF_FLOAT32:
                options.heightmapFormat = ExportHeightmapFormat::GeoTiffFloat32;
                break;
            case protocol::v1::TERRAIN_EXPORT_FORMAT_GEOTIFF_INT16:
                options.heightmapFormat = ExportHeightmapFormat::GeoTiffInt16;
                break;
            case protocol::v1::TERRAIN_EXPORT_FORMAT_GEOTIFF_UINT16:
                options.heightmapFormat = ExportHeightmapFormat::GeoTiffUInt16;
                break;
            case protocol::v1::TERRAIN_EXPORT_FORMAT_PNG_16:
                options.heightmapFormat = ExportHeightmapFormat::Png16;
                break;
            case protocol::v1::TERRAIN_EXPORT_FORMAT_RAW_R16:
                options.heightmapFormat = ExportHeightmapFormat::RawR16;
                break;
            case protocol::v1::TERRAIN_EXPORT_FORMAT_NONE:
                options.heightmapFormat = ExportHeightmapFormat::None;
                break;
            default:
                options.heightmapFormat = ExportHeightmapFormat::GeoTiffFloat32;
                break;
        }

        switch (command.albedo_format()) {
            case protocol::v1::TERRAIN_ALBEDO_EXPORT_FORMAT_PNG_RGB:
                options.albedoFormat = ExportAlbedoFormat::PngRgb;
                break;
            case protocol::v1::TERRAIN_ALBEDO_EXPORT_FORMAT_GEOTIFF_RGB:
                options.albedoFormat = ExportAlbedoFormat::GeoTiffRgb;
                break;
            default:
                options.albedoFormat = ExportAlbedoFormat::None;
                break;
        }

        const TerrainExportOutput output = terrainService_->exportDataset(options);

        ProtocolFrame response;
        response.set_request_id(frame.request_id());
        auto* result = response.mutable_result()->mutable_terrain_export_result();
        result->set_dataset_uuid(output.datasetUuid);
        result->set_manifest_path(output.manifestPath.string());
        for (const auto& file : output.exportedFiles) {
            result->add_exported_files(file.string());
        }
        result->set_total_bytes(output.totalBytes);
        sink_.sendToConnection(connectionId, response);
    });
}

void CommandProcessor::handleJobCancel(
    const std::string& connectionId, const ProtocolFrame& frame) {
    const auto& command = frame.command().job_cancel();
    executeCommand([this, &connectionId, &frame, jobId = command.job_id()] {
        if (!jobs_->job(jobId).has_value()) {
            throw CommandFailure{CommandFailureCode::NotFound, "unknown job: " + jobId};
        }
        const bool cancelled = jobs_->requestCancel(jobId);

        ProtocolFrame response;
        response.set_request_id(frame.request_id());
        auto* result = response.mutable_result()->mutable_job_cancel_result();
        result->set_job_id(jobId);
        result->set_cancelled(cancelled);
        sink_.sendToConnection(connectionId, response);
    });
}

void CommandProcessor::handleJobList(
    const std::string& connectionId, const ProtocolFrame& frame) {
    executeCommand([this, &connectionId, &frame] {
        const std::vector<JobRecord> records = jobs_->listJobs();

        ProtocolFrame response;
        response.set_request_id(frame.request_id());
        auto* result = response.mutable_result()->mutable_job_list_result();
        for (const JobRecord& record : records) {
            fillJobRecord(result->add_jobs(), record);
        }
        sink_.sendToConnection(connectionId, response);
    });
}

template <typename Emit>
void CommandProcessor::executeCommand(Emit&& emit) {
    // No job lifecycle here — processCommand owns the lifecycle envelope.
    // Exceptions propagate to processCommand's catch block.
    emit();
}

void CommandProcessor::publishEvents(const std::span<const ProjectEvent> events) {
    for (const ProjectEvent& event : events) {
        ProtocolFrame eventFrame;
        auto* envelope = eventFrame.mutable_event();
        envelope->set_event_id(runtime::generateUuidV4());
        switch (event.kind) {
        case ProjectEventKind::Opened: {
            auto* opened = envelope->mutable_project_opened();
            fillSummary(opened->mutable_summary(), event.record);
            break;
        }
        case ProjectEventKind::Closed:
            envelope->mutable_project_closed()->set_project_uuid(event.record.uuid);
            break;
        case ProjectEventKind::RevisionChanged:
            envelope->mutable_project_revision_changed()->set_revision(event.record.revision);
            break;
        case ProjectEventKind::DirtyStateChanged: {
            auto* dirty = envelope->mutable_project_dirty_state_changed();
            dirty->set_dirty(event.record.isDirty());
            dirty->set_revision(event.record.revision);
            break;
        }
        case ProjectEventKind::GeoreferenceChanged: {
            auto* changed = envelope->mutable_georeference_changed();
            if (event.georeference.has_value()) {
                fillGeoreferenceInfo(changed->mutable_georeference(),
                    event.record.georeference, *event.georeference);
            }
            changed->set_revision(event.record.revision);
            break;
        }
        }
        sink_.broadcastEvent(eventFrame);
    }
}

void CommandProcessor::publishTerrainEvent(const TerrainServiceEvent& event) {
    if (event.datasetAdded.has_value()) {
        ProtocolFrame eventFrame;
        auto* envelope = eventFrame.mutable_event();
        envelope->set_event_id(runtime::generateUuidV4());
        auto* added = envelope->mutable_terrain_dataset_added();
        fillTerrainDatasetInfo(added->mutable_dataset(), *event.datasetAdded);
        added->set_revision(event.revision);
        sink_.broadcastEvent(eventFrame);

        // The dataset insert is a canonical mutation: derive the standard
        // revision/dirty projections from the post-mutation store state.
        if (store_.isOpen()) {
            const auto& record = store_.current();
            ProtocolFrame revisionFrame;
            auto* revisionEnvelope = revisionFrame.mutable_event();
            revisionEnvelope->set_event_id(runtime::generateUuidV4());
            revisionEnvelope->mutable_project_revision_changed()->set_revision(record.revision);
            sink_.broadcastEvent(revisionFrame);

            ProtocolFrame dirtyFrame;
            auto* dirtyEnvelope = dirtyFrame.mutable_event();
            dirtyEnvelope->set_event_id(runtime::generateUuidV4());
            auto* dirty = dirtyEnvelope->mutable_project_dirty_state_changed();
            dirty->set_dirty(record.isDirty());
            dirty->set_revision(record.revision);
            sink_.broadcastEvent(dirtyFrame);
        }

        // Route terrain diagnostics through the Problems/diagnostics system.
        for (const domain::terrain::TerrainDiagnostic& diagnostic : event.datasetAdded->diagnostics) {
            const std::string diagnosticId = "terrain:" + std::string{domain::terrain::uuidTextFromEntityId(event.datasetAdded->id)}
                + ":" + std::string{domain::terrain::terrainErrorCodeName(diagnostic.code)};
            publishDiagnosticAdded(diagnosticId, "terrain",
                protocol::v1::DIAGNOSTIC_SEVERITY_WARNING,
                diagnostic.message,
                domain::terrain::uuidTextFromEntityId(event.datasetAdded->id));
            hasTerrainDiagnostics_ = true;
        }
    }

    if (event.job.has_value()) {
        const JobRecord& record = *event.job;
        ProtocolFrame eventFrame;
        auto* envelope = eventFrame.mutable_event();
        envelope->set_event_id(runtime::generateUuidV4());
        switch (record.state) {
        case JobState::Queued: {
            auto* queued = envelope->mutable_job_queued();
            queued->set_job_id(record.jobId);
            queued->set_operation(record.kind);
            queued->set_label(record.progress.label);
            queued->set_cancellable(record.cancellable);
            break;
        }
        case JobState::Running: {
            // First Running update emits job.started; subsequent updates emit
            // job.progress. Track started jobs to distinguish the two.
            static thread_local std::unordered_set<std::string> startedJobs;
            if (startedJobs.insert(record.jobId).second) {
                envelope->mutable_job_started()->set_job_id(record.jobId);
            } else {
                auto* progress = envelope->mutable_job_progress();
                progress->set_job_id(record.jobId);
                progress->set_progress(record.progress.normalized);
                progress->set_processed(record.progress.unitsDone);
                progress->set_total(record.progress.unitsTotal);
                progress->set_message(record.progress.label);
            }
            break;
        }
        case JobState::Completed: {
            envelope->mutable_job_completed()->set_job_id(record.jobId);
            break;
        }
        case JobState::Failed: {
            auto* failed = envelope->mutable_job_failed();
            failed->set_job_id(record.jobId);
            // Use the typed error code from the job body (BLOCKER 5).
            // If no code was set, fall back to a generic internal error code.
            failed->set_error_code(record.errorCode.empty()
                ? "INTERNAL"
                : record.errorCode);
            failed->set_error_message(record.message);
            break;
        }
        case JobState::Cancelled: {
            envelope->mutable_job_cancelled()->set_job_id(record.jobId);
            break;
        }
        }
        sink_.broadcastEvent(eventFrame);
    }
}

void CommandProcessor::syncTerrainSession() {
    if (store_.isOpen()) {
        terrainService_->onProjectOpened();
        roadService_->onProjectOpened();
    } else {
        terrainService_->onProjectClosed();
        roadService_->onProjectClosed();
        // Clear terrain-scoped diagnostics when the project closes, but only
        // if any were actually emitted (avoids spurious events).
        if (hasTerrainDiagnostics_) {
            publishDiagnosticCleared("terrain");
            hasTerrainDiagnostics_ = false;
        }
    }
}

void CommandProcessor::sendFailureResult(
    const std::string& connectionId,
    const std::string& requestId,
    const CommandFailure& failure) {
    ProtocolFrame response;
    response.set_request_id(requestId);
    auto* error = response.mutable_result()->mutable_error();
    error->set_code(mapFailureCode(failure.code()));
    error->set_message(failure.what());
    if (store_.isOpen()) {
        error->set_current_revision(store_.current().revision);
    }
    sink_.sendToConnection(connectionId, response);
}

void CommandProcessor::sendInternalErrorResult(
    const std::string& connectionId,
    const std::string& requestId,
    std::string_view commandLabel) {
    ProtocolFrame response;
    response.set_request_id(requestId);
    auto* error = response.mutable_result()->mutable_error();
    error->set_code(ProtocolCommandErrorCode::COMMAND_ERROR_CODE_INTERNAL);
    error->set_message("internal engine error while handling " + std::string{commandLabel});
    sink_.sendToConnection(connectionId, response);
}

void CommandProcessor::publishJobQueued(
    const std::string& jobId,
    std::string_view operation,
    std::string_view label,
    std::string_view targetId,
    bool cancellable) {
    ProtocolFrame eventFrame;
    auto* envelope = eventFrame.mutable_event();
    envelope->set_event_id(runtime::generateUuidV4());
    auto* queued = envelope->mutable_job_queued();
    queued->set_job_id(jobId);
    queued->set_operation(std::string{operation});
    queued->set_label(std::string{label});
    if (!targetId.empty()) {
        queued->set_target_id(std::string{targetId});
    }
    queued->set_cancellable(cancellable);
    sink_.broadcastEvent(eventFrame);
}

void CommandProcessor::publishJobStarted(const std::string& jobId) {
    ProtocolFrame eventFrame;
    auto* envelope = eventFrame.mutable_event();
    envelope->set_event_id(runtime::generateUuidV4());
    envelope->mutable_job_started()->set_job_id(jobId);
    sink_.broadcastEvent(eventFrame);
}

void CommandProcessor::publishJobProgress(
    const std::string& jobId, double progress,
    std::uint64_t processed, std::uint64_t total, std::string_view message) {
    ProtocolFrame eventFrame;
    auto* envelope = eventFrame.mutable_event();
    envelope->set_event_id(runtime::generateUuidV4());
    auto* prog = envelope->mutable_job_progress();
    prog->set_job_id(jobId);
    prog->set_progress(progress);
    prog->set_processed(processed);
    prog->set_total(total);
    prog->set_message(std::string{message});
    sink_.broadcastEvent(eventFrame);
}

void CommandProcessor::publishJobCompleted(const std::string& jobId) {
    ProtocolFrame eventFrame;
    auto* envelope = eventFrame.mutable_event();
    envelope->set_event_id(runtime::generateUuidV4());
    envelope->mutable_job_completed()->set_job_id(jobId);
    sink_.broadcastEvent(eventFrame);
}

void CommandProcessor::publishJobFailed(
    const std::string& jobId,
    std::string_view errorCode,
    std::string_view errorMessage) {
    ProtocolFrame eventFrame;
    auto* envelope = eventFrame.mutable_event();
    envelope->set_event_id(runtime::generateUuidV4());
    auto* failed = envelope->mutable_job_failed();
    failed->set_job_id(jobId);
    failed->set_error_code(std::string{errorCode});
    failed->set_error_message(std::string{errorMessage});
    sink_.broadcastEvent(eventFrame);
}

void CommandProcessor::publishJobCancelled(const std::string& jobId) {
    ProtocolFrame eventFrame;
    auto* envelope = eventFrame.mutable_event();
    envelope->set_event_id(runtime::generateUuidV4());
    envelope->mutable_job_cancelled()->set_job_id(jobId);
    sink_.broadcastEvent(eventFrame);
}

void CommandProcessor::publishDiagnosticAdded(
    std::string_view diagnosticId,
    std::string_view source,
    protocol::v1::DiagnosticSeverity severity,
    std::string_view message,
    std::string_view targetId) {
    ProtocolFrame eventFrame;
    auto* envelope = eventFrame.mutable_event();
    envelope->set_event_id(runtime::generateUuidV4());
    auto* added = envelope->mutable_diagnostic_added();
    added->set_diagnostic_id(std::string{diagnosticId});
    added->set_source(std::string{source});
    added->set_severity(severity);
    added->set_message(std::string{message});
    if (!targetId.empty()) {
        added->set_target_id(std::string{targetId});
    }
    sink_.broadcastEvent(eventFrame);
}

void CommandProcessor::publishDiagnosticRemoved(std::string_view diagnosticId) {
    ProtocolFrame eventFrame;
    auto* envelope = eventFrame.mutable_event();
    envelope->set_event_id(runtime::generateUuidV4());
    envelope->mutable_diagnostic_removed()->set_diagnostic_id(std::string{diagnosticId});
    sink_.broadcastEvent(eventFrame);
}

void CommandProcessor::publishDiagnosticCleared(std::string_view source) {
    ProtocolFrame eventFrame;
    auto* envelope = eventFrame.mutable_event();
    envelope->set_event_id(runtime::generateUuidV4());
    envelope->mutable_diagnostic_cleared()->set_source(std::string{source});
    sink_.broadcastEvent(eventFrame);
}

std::string CommandProcessor::verticalReferenceDiagnosticId(std::string_view projectUuid) {
    return "georeference:no-vertical-reference:" + std::string{projectUuid};
}

void CommandProcessor::evaluateVerticalReferenceDiagnostic(const domain::project::ProjectRecord& record) {
    const std::string diagnosticId = verticalReferenceDiagnosticId(record.uuid);
    if (record.georeference.verticalCrs.empty()) {
        // No vertical reference configured: emit a warning diagnostic so the
        // Problems panel surfaces the real geospatial condition. Heights are
        // ambiguous without a vertical datum.
        publishDiagnosticAdded(
            diagnosticId,
            "georeference",
            protocol::v1::DIAGNOSTIC_SEVERITY_WARNING,
            "No vertical reference configured; heights are ambiguous without a vertical datum",
            record.uuid);
    } else {
        // A vertical reference is configured: remove the warning if it was
        // previously emitted. This is idempotent — removing a non-existent
        // diagnostic is a no-op on the frontend.
        publishDiagnosticRemoved(diagnosticId);
    }
}

// ---- Road command handlers ----

namespace {

void fillRoadSummary(protocol::v1::RoadSummary* out, const RoadSummary& s) {
    out->set_road_id(s.roadId);
    out->set_name(s.name);
    out->set_length(s.length);
    out->set_alignment_segment_count(s.alignmentSegmentCount);
    out->set_source_provider(s.sourceProvider);
    out->set_source_id(s.sourceId);
    out->set_protected_anchor_count(s.protectedAnchorCount);
    out->set_revision(s.revision);
}

void fillRoadDetails(protocol::v1::RoadDetails* out, const RoadDetails& d) {
    out->set_road_id(d.roadId);
    out->set_name(d.name);
    out->set_length(d.length);
    out->set_alignment_segment_count(d.alignmentSegmentCount);
    for (const auto& seg : d.alignmentSegments) {
        auto* segOut = out->add_alignment_segments();
        segOut->set_kind(seg.kind);
        segOut->set_start_station(seg.startStation);
        segOut->set_length(seg.length);
        segOut->set_start_curvature(seg.startCurvature);
        segOut->set_end_curvature(seg.endCurvature);
    }
    out->set_has_elevation_profile(d.hasElevationProfile);
    out->set_elevation_breakpoint_count(d.elevationBreakpointCount);
    out->set_has_superelevation_profile(d.hasSuperelevationProfile);
    out->set_superelevation_breakpoint_count(d.superelevationBreakpointCount);
    out->set_source_provider(d.sourceProvider);
    out->set_source_id(d.sourceId);
    out->set_source_crs(d.sourceCrs);
    out->set_protected_anchor_count(d.protectedAnchorCount);
    out->set_is_valid(d.isValid);
    for (const auto& diag : d.diagnostics) {
        auto* dOut = out->add_diagnostics();
        dOut->set_code(std::string{domain::road::roadErrorCodeName(diag.code)});
        dOut->set_message(diag.message);
        dOut->set_severity("error");
    }
    out->set_revision(d.revision);
    out->set_position_tolerance(d.positionTolerance);
    if (d.maxCurvature.has_value()) out->set_max_curvature(*d.maxCurvature);
    for (const auto& breakpoint : d.elevationBreakpoints) {
        auto* projected = out->add_elevation_breakpoints();
        projected->set_station(breakpoint.station);
        projected->set_value(breakpoint.value);
    }
    for (const auto& breakpoint : d.superelevationBreakpoints) {
        auto* projected = out->add_superelevation_breakpoints();
        projected->set_station(breakpoint.station);
        projected->set_value(breakpoint.value);
    }
    for (const auto& control : d.controlPoints) {
        auto* projected = out->add_control_points();
        projected->set_easting(control.easting);
        projected->set_northing(control.northing);
        if (control.elevation.has_value()) projected->set_elevation(*control.elevation);
        projected->set_protected_anchor(control.protectedAnchor);
    }
}

} // namespace

void CommandProcessor::handleCreateRoad(const std::string& connectionId, const ProtocolFrame& frame) {
    const auto& command = frame.command().create_road();
    CreateRoadInput input;
    input.name = command.name();
    const auto count = command.source_eastings_size();
    if (command.source_northings_size() != static_cast<int>(count)) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "source_eastings and source_northings must have the same length"};
    }
    input.sourcePoints.reserve(count);
    for (int i = 0; i < count; ++i) {
        input.sourcePoints.push_back(domain::road::AlignmentPoint{
            command.source_eastings(i), command.source_northings(i)});
    }
    if (command.source_elevations_size() == static_cast<int>(count)) {
        input.sourceElevations.reserve(count);
        for (int i = 0; i < count; ++i) {
            input.sourceElevations.push_back(command.source_elevations(i));
        }
    } else if (command.source_elevations_size() != 0) {
        // Blocker 16: reject non-empty elevation arrays whose length does
        // not equal the control/source point count.
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "source_elevations length must equal source point count when non-empty"};
    } else {
        input.sourceElevations.resize(count, std::nullopt);
    }
    input.positionTolerance = command.position_tolerance();
    if (command.has_max_curvature()) {
        input.maxCurvature = command.max_curvature();
    }
    for (int i = 0; i < command.protected_anchor_indices_size(); ++i) {
        input.protectedAnchorIndices.push_back(command.protected_anchor_indices(i));
    }

    auto summary = roadService_->createRoad(input);

    ProtocolFrame response;
    response.set_request_id(frame.request_id());
    fillRoadSummary(response.mutable_result()->mutable_create_road_result()->mutable_road(), summary);
    sink_.sendToConnection(connectionId, response);
}

void CommandProcessor::handleDeleteRoad(const std::string& connectionId, const ProtocolFrame& frame) {
    const auto& command = frame.command().delete_road();
    auto summary = roadService_->deleteRoad(command.road_id());

    ProtocolFrame response;
    response.set_request_id(frame.request_id());
    response.mutable_result()->mutable_delete_road_result();
    sink_.sendToConnection(connectionId, response);
}

void CommandProcessor::handleRenameRoad(const std::string& connectionId, const ProtocolFrame& frame) {
    const auto& command = frame.command().rename_road();
    auto summary = roadService_->renameRoad(command.road_id(), command.name());

    ProtocolFrame response;
    response.set_request_id(frame.request_id());
    fillRoadSummary(response.mutable_result()->mutable_rename_road_result()->mutable_road(), summary);
    sink_.sendToConnection(connectionId, response);
}

void CommandProcessor::handleInsertRoadControl(const std::string& connectionId, const ProtocolFrame& frame) {
    const auto& command = frame.command().insert_road_control();
    InsertControlInput input;
    input.roadId = command.road_id();
    input.insertBeforeIndex = command.insert_before_index();
    input.position = domain::road::AlignmentPoint{command.easting(), command.northing()};
    if (command.has_elevation()) {
        input.elevation = command.elevation();
    }
    auto summary = roadService_->insertControl(input);

    ProtocolFrame response;
    response.set_request_id(frame.request_id());
    fillRoadSummary(response.mutable_result()->mutable_insert_road_control_result()->mutable_road(), summary);
    sink_.sendToConnection(connectionId, response);
}

void CommandProcessor::handleMoveRoadControl(const std::string& connectionId, const ProtocolFrame& frame) {
    const auto& command = frame.command().move_road_control();
    MoveControlInput input;
    input.roadId = command.road_id();
    input.controlIndex = command.control_index();
    input.position = domain::road::AlignmentPoint{command.easting(), command.northing()};
    if (command.has_elevation()) {
        input.elevation = command.elevation();
    }
    auto summary = roadService_->moveControl(input);

    ProtocolFrame response;
    response.set_request_id(frame.request_id());
    fillRoadSummary(response.mutable_result()->mutable_move_road_control_result()->mutable_road(), summary);
    sink_.sendToConnection(connectionId, response);
}

void CommandProcessor::handleDeleteRoadControl(const std::string& connectionId, const ProtocolFrame& frame) {
    const auto& command = frame.command().delete_road_control();
    DeleteControlInput input;
    input.roadId = command.road_id();
    input.controlIndex = command.control_index();
    auto summary = roadService_->deleteControl(input);

    ProtocolFrame response;
    response.set_request_id(frame.request_id());
    fillRoadSummary(response.mutable_result()->mutable_delete_road_control_result()->mutable_road(), summary);
    sink_.sendToConnection(connectionId, response);
}

void CommandProcessor::handleFitRoadSource(const std::string& connectionId, const ProtocolFrame& frame) {
    const auto& command = frame.command().fit_road_source();
    FitSourceInput input;
    input.roadId = command.road_id();
    if (command.has_position_tolerance()) {
        input.positionTolerance = command.position_tolerance();
    }
    if (command.has_max_curvature()) {
        input.maxCurvature = command.max_curvature();
        input.replaceMaxCurvature = true;
    }
    if (command.clear_max_curvature()) {
        if (input.replaceMaxCurvature) {
            throw CommandFailure{CommandFailureCode::InvalidArgument,
                "max curvature cannot be replaced and cleared together"};
        }
        input.replaceMaxCurvature = true;
    }
    auto summary = roadService_->fitSource(input);

    ProtocolFrame response;
    response.set_request_id(frame.request_id());
    fillRoadSummary(response.mutable_result()->mutable_fit_road_source_result()->mutable_road(), summary);
    sink_.sendToConnection(connectionId, response);
}

void CommandProcessor::handleUpdateRoadElevation(const std::string& connectionId, const ProtocolFrame& frame) {
    const auto& command = frame.command().update_road_elevation();
    // Blocker 15: validate repeated-field sizes BEFORE indexing to avoid
    // out-of-range access on malformed protocol commands.
    if (command.stations_size() != command.elevations_size()) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "stations and elevations must have the same length"};
    }
    UpdateElevationInput input;
    input.roadId = command.road_id();
    for (int i = 0; i < command.stations_size(); ++i) {
        input.stations.push_back(command.stations(i));
        input.elevations.push_back(command.elevations(i));
    }
    auto summary = roadService_->updateElevation(input);

    ProtocolFrame response;
    response.set_request_id(frame.request_id());
    fillRoadSummary(response.mutable_result()->mutable_update_road_elevation_result()->mutable_road(), summary);
    sink_.sendToConnection(connectionId, response);
}

void CommandProcessor::handleUpdateRoadSuperelevation(const std::string& connectionId, const ProtocolFrame& frame) {
    const auto& command = frame.command().update_road_superelevation();
    // Blocker 15: validate repeated-field sizes BEFORE indexing to avoid
    // out-of-range access on malformed protocol commands.
    if (command.stations_size() != command.superelevations_size()) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "stations and superelevations must have the same length"};
    }
    UpdateSuperelevationInput input;
    input.roadId = command.road_id();
    for (int i = 0; i < command.stations_size(); ++i) {
        input.stations.push_back(command.stations(i));
        input.superelevations.push_back(command.superelevations(i));
    }
    auto summary = roadService_->updateSuperelevation(input);

    ProtocolFrame response;
    response.set_request_id(frame.request_id());
    fillRoadSummary(response.mutable_result()->mutable_update_road_superelevation_result()->mutable_road(), summary);
    sink_.sendToConnection(connectionId, response);
}

void CommandProcessor::handleUndoRoad(const std::string& connectionId, const ProtocolFrame& frame) {
    const auto& command = frame.command().undo_road();
    const auto history = roadService_->undo(command.road_id());
    if (!history) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "nothing to undo for road: " + command.road_id()};
    }

    ProtocolFrame response;
    response.set_request_id(frame.request_id());
    auto* result = response.mutable_result()->mutable_undo_road_result();
    result->set_road_id(history.roadId);
    result->set_exists_after_operation(history.existsAfterOperation);
    if (history.road.has_value()) {
        fillRoadSummary(result->mutable_road(), *history.road);
    }
    sink_.sendToConnection(connectionId, response);
}

void CommandProcessor::handleRedoRoad(const std::string& connectionId, const ProtocolFrame& frame) {
    const auto& command = frame.command().redo_road();
    const auto history = roadService_->redo(command.road_id());
    if (!history) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "nothing to redo for road: " + command.road_id()};
    }

    ProtocolFrame response;
    response.set_request_id(frame.request_id());
    auto* result = response.mutable_result()->mutable_redo_road_result();
    result->set_road_id(history.roadId);
    result->set_exists_after_operation(history.existsAfterOperation);
    if (history.road.has_value()) {
        fillRoadSummary(result->mutable_road(), *history.road);
    }
    sink_.sendToConnection(connectionId, response);
}

void CommandProcessor::handleListRoads(const std::string& connectionId, const ProtocolFrame& frame) {
    auto roads = roadService_->listRoads();

    ProtocolFrame response;
    response.set_request_id(frame.request_id());
    auto* result = response.mutable_result()->mutable_list_roads_result();
    for (const auto& r : roads) {
        fillRoadSummary(result->add_roads(), r);
    }
    sink_.sendToConnection(connectionId, response);
}

void CommandProcessor::handleGetRoad(const std::string& connectionId, const ProtocolFrame& frame) {
    const auto& command = frame.command().get_road();
    auto details = roadService_->getRoad(command.road_id());
    if (!details.has_value()) {
        throw CommandFailure{CommandFailureCode::NotFound,
            "road not found: " + command.road_id()};
    }

    ProtocolFrame response;
    response.set_request_id(frame.request_id());
    auto* result = response.mutable_result()->mutable_get_road_result();
    fillRoadDetails(result->mutable_road(), *details);
    const auto summary = roadService_->getRoadSummary(command.road_id());
    if (!summary.has_value()) {
        throw CommandFailure{CommandFailureCode::NotFound,
            "road disappeared while constructing projection: " + command.road_id()};
    }
    fillRoadSummary(result->mutable_summary(), *summary);
    sink_.sendToConnection(connectionId, response);
}

void CommandProcessor::handleGetRoadScene(const std::string& connectionId, const ProtocolFrame& frame) {
    auto projection = roadService_->roadSceneProjection();

    ProtocolFrame response;
    response.set_request_id(frame.request_id());
    auto* scene = response.mutable_result()->mutable_road_scene_result();
    scene->set_origin_easting(projection.originEasting);
    scene->set_origin_northing(projection.originNorthing);
    scene->set_origin_height(projection.originHeight);
    scene->set_revision(projection.revision);
    for (const auto& mesh : projection.meshes) {
        auto* meshOut = scene->add_meshes();
        meshOut->set_road_id(mesh.roadId);
        meshOut->set_chunk_x(mesh.chunkX);
        meshOut->set_chunk_y(mesh.chunkY);
        for (const auto& v : mesh.vertices) {
            auto* vOut = meshOut->add_vertices();
            vOut->set_x(v.x);
            vOut->set_y(v.y);
            vOut->set_z(v.z);
            vOut->set_nx(v.nx);
            vOut->set_ny(v.ny);
            vOut->set_nz(v.nz);
        }
        for (const auto idx : mesh.indices) {
            meshOut->add_indices(idx);
        }
    }
    sink_.sendToConnection(connectionId, response);
}

void CommandProcessor::publishRoadEvent(const RoadServiceEvent& event) {
    ProtocolFrame eventFrame;
    auto* envelope = eventFrame.mutable_event();
    envelope->set_event_id(runtime::generateUuidV4());

    switch (event.kind) {
    case RoadServiceEvent::Kind::Created: {
        auto* created = envelope->mutable_road_created();
        created->set_road_id(event.roadId);
        created->set_revision(event.revision);
        break;
    }
    case RoadServiceEvent::Kind::Updated: {
        auto* updated = envelope->mutable_road_updated();
        updated->set_road_id(event.roadId);
        updated->set_revision(event.revision);
        break;
    }
    case RoadServiceEvent::Kind::Removed: {
        auto* removed = envelope->mutable_road_removed();
        removed->set_road_id(event.roadId);
        removed->set_revision(event.revision);
        break;
    }
    case RoadServiceEvent::Kind::GeometryChanged: {
        auto* changed = envelope->mutable_road_geometry_changed();
        changed->set_road_id(event.roadId);
        changed->set_revision(event.revision);
        for (const auto& chunk : event.affectedChunks) {
            changed->add_chunk_x(chunk.x);
            changed->add_chunk_y(chunk.y);
        }
        break;
    }
    }
    sink_.broadcastEvent(eventFrame);

    // Road mutations are canonical: derive revision/dirty projections.
    if (store_.isOpen()) {
        const auto& record = store_.current();
        ProtocolFrame revisionFrame;
        auto* revisionEnvelope = revisionFrame.mutable_event();
        revisionEnvelope->set_event_id(runtime::generateUuidV4());
        revisionEnvelope->mutable_project_revision_changed()->set_revision(record.revision);
        sink_.broadcastEvent(revisionFrame);

        ProtocolFrame dirtyFrame;
        auto* dirtyEnvelope = dirtyFrame.mutable_event();
        dirtyEnvelope->set_event_id(runtime::generateUuidV4());
        auto* dirty = dirtyEnvelope->mutable_project_dirty_state_changed();
        dirty->set_dirty(record.isDirty());
        dirty->set_revision(record.revision);
        sink_.broadcastEvent(dirtyFrame);
    }
}

} // namespace infraforge::application
