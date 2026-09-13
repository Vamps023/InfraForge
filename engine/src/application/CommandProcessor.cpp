#include "infraforge/application/CommandProcessor.hpp"

#include "infraforge/application/ProjectService.hpp"
#include "infraforge/runtime/FileSystemUtf8.hpp"
#include "infraforge/runtime/Logging.hpp"
#include "infraforge/runtime/Uuid.hpp"
#include "infraforge/version.hpp"

#include <infraforge/protocol/v1/foundation.pb.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
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
    }
    return "INTERNAL";
}

} // namespace

CommandProcessor::CommandProcessor(
    ports::ProjectStore& store,
    const domain::geo::GeoTransformService& transforms,
    CommandSink& sink)
    : store_(store),
      service_(store, transforms),
      geoService_(store, transforms),
      sink_(sink) {}

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

void CommandProcessor::shutdown() {
    {
        std::lock_guard lock{mutex_};
        if (stopped_) {
            return;
        }
        stopped_ = true;
        const std::size_t discarded = queue_.size();
        queue_.clear();
        if (discarded > 0) {
            // Commands discarded during shutdown never had job.queued
            // emitted (post() is enqueue-only), so no orphan pending jobs
            // are created.
            runtime::logWarn("application", "command.queue_discarded", {{"count", std::to_string(discarded)}});
        }
    }
    signal_.notify_all();
    if (executor_.joinable()) {
        executor_.join();
    }
}

void CommandProcessor::runExecutor() {
    std::unique_lock lock{mutex_};
    for (;;) {
        signal_.wait(lock, [this] { return stopped_ || !queue_.empty(); });
        if (stopped_ || queue_.empty()) {
            if (stopped_) {
                return;
            }
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
    publishJobQueued(jobId, label, label, targetId);
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

        // Terminal success event emitted exactly once.
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
    executeCommand( [this, &connectionId, &frame, &useCase] {
        const ProjectCommandResult result = useCase();

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
    executeCommand( [this, &connectionId, &frame] {
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
    std::string_view targetId) {
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
    // Cancellation is not yet wired end-to-end; no current operation supports it.
    queued->set_cancellable(false);
    sink_.broadcastEvent(eventFrame);
}

void CommandProcessor::publishJobStarted(const std::string& jobId) {
    ProtocolFrame eventFrame;
    auto* envelope = eventFrame.mutable_event();
    envelope->set_event_id(runtime::generateUuidV4());
    envelope->mutable_job_started()->set_job_id(jobId);
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

} // namespace infraforge::application
