#include "infraforge/application/CommandProcessor.hpp"

#include "infraforge/application/ProjectService.hpp"
#include "infraforge/application/validation/ProjectFoundationValidator.hpp"
#include "infraforge/application/validation/ValidationService.hpp"
#include "infraforge/runtime/FileSystemUtf8.hpp"
#include "infraforge/runtime/Logging.hpp"
#include "infraforge/runtime/Uuid.hpp"
#include "infraforge/version.hpp"

#include <infraforge/protocol/v1/foundation.pb.h>

#include <chrono>
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

std::optional<domain::project::AxisConvention> mapAxisConvention(const protocol::v1::AxisConvention convention) {
    switch (convention) {
    case protocol::v1::AXIS_CONVENTION_EASTING_NORTHING_UP:
        return domain::project::AxisConvention::EastingNorthingUp;
    default:
        return std::nullopt;
    }
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

    auto* georeference = summary->mutable_georeference();
    georeference->set_horizontal_crs(record.georeference.horizontalCrs);
    georeference->set_linear_unit(record.georeference.linearUnit);
    georeference->set_axis_convention(
        record.georeference.axisConvention == domain::project::AxisConvention::EastingNorthingUp
            ? protocol::v1::AXIS_CONVENTION_EASTING_NORTHING_UP
            : protocol::v1::AXIS_CONVENTION_UNSPECIFIED);
    georeference->set_origin_easting(record.georeference.originEasting);
    georeference->set_origin_northing(record.georeference.originNorthing);
    georeference->set_vertical_crs(record.georeference.verticalCrs);
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
    case protocol::v1::CommandEnvelope::kWorldCheck:
        return "world.check";
    case protocol::v1::CommandEnvelope::kWorldCancelCheck:
        return "world.cancel_check";
    case protocol::v1::CommandEnvelope::COMMAND_NOT_SET:
        break;
    }
    return "unknown";
}

protocol::v1::DiagnosticSeverity mapSeverity(domain::validation::Severity severity) {
    switch (severity) {
    case domain::validation::Severity::Info:
        return protocol::v1::DIAGNOSTIC_SEVERITY_INFO;
    case domain::validation::Severity::Warning:
        return protocol::v1::DIAGNOSTIC_SEVERITY_WARNING;
    case domain::validation::Severity::Error:
        return protocol::v1::DIAGNOSTIC_SEVERITY_ERROR;
    }
    return protocol::v1::DIAGNOSTIC_SEVERITY_UNSPECIFIED;
}

void fillDiagnostic(protocol::v1::Diagnostic* out, const domain::validation::Diagnostic& in) {
    out->set_code(in.code);
    out->set_severity(mapSeverity(in.severity));
    out->set_source(in.source);
    out->set_message(in.message);
    out->set_revision(in.revision);
    for (const auto& entity : in.entities) {
        auto* ref = out->add_entities();
        ref->set_kind(entity.kind);
        ref->set_id(entity.id);
    }
    if (in.suggestedAction.has_value()) {
        auto* action = out->mutable_suggested_action();
        action->set_kind(in.suggestedAction->kind);
        action->set_label(in.suggestedAction->label);
    }
}

} // namespace

CommandProcessor::CommandProcessor(ports::ProjectStore& store, CommandSink& sink)
    : store_(store),
      service_(store),
      validationService_(store, validatorRegistry_),
      sink_(sink) {
    validatorRegistry_.registerValidator(std::make_unique<validation::ProjectFoundationValidator>());
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
    const std::string_view label = frame.has_command() ? commandName(frame) : std::string_view{"unknown"};

    // Cancellation is handled on the calling (network) thread, not queued on
    // the executor, so the signal reaches an in-progress validator without
    // waiting for the executor to drain. The result is sent immediately.
    if (frame.has_command() && frame.command().command_case() == protocol::v1::CommandEnvelope::kWorldCancelCheck) {
        handleWorldCancelCheck(connectionId, frame);
        return;
    }

    {
        std::lock_guard lock{mutex_};
        if (stopped_) {
            // The engine is shutting down; connections are closing anyway.
            runtime::logWarn("application", "command.dropped_at_shutdown", {{"command", label}});
            return;
        }
        queue_.push_back({std::move(connectionId), std::make_unique<ProtocolFrame>(std::move(frame))});
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
            processCommand(command.connectionId, *command.frame);
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

void CommandProcessor::processCommand(const std::string& connectionId, const ProtocolFrame& frame) {
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
    case protocol::v1::CommandEnvelope::kWorldCheck:
        handleWorldCheck(connectionId, frame);
        break;
    case protocol::v1::CommandEnvelope::kWorldCancelCheck:
        handleWorldCancelCheck(connectionId, frame);
        break;
    case protocol::v1::CommandEnvelope::COMMAND_NOT_SET:
        sendFailureResult(connectionId, frame.request_id(),
            CommandFailure{CommandFailureCode::InvalidArgument, "command envelope is empty"});
        break;
    }
}

void CommandProcessor::handleCreateProject(const std::string& connectionId, const ProtocolFrame& frame) {
    const auto& command = frame.command().create_project();
    const auto trafficSide = mapTrafficSide(command.traffic_side());
    const auto axisConvention = mapAxisConvention(command.georeference().axis_convention());
    if (!trafficSide.has_value() || !axisConvention.has_value()) {
        sendFailureResult(connectionId, frame.request_id(),
            CommandFailure{CommandFailureCode::InvalidArgument,
                !trafficSide.has_value() ? "traffic_side must be LEFT or RIGHT"
                                         : "axis_convention must be EASTING_NORTHING_UP"});
        return;
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

void CommandProcessor::handleWorldCheck(const std::string& connectionId, const ProtocolFrame& frame) {
    const std::string requestId = frame.request_id();
    const auto startedAt = std::chrono::steady_clock::now();

    try {
        // Create a shared cancellation token so the network thread can
        // request cancellation via world.cancel_check while this run is
        // in progress on the executor.
        auto cancellation = std::make_shared<validation::CancellationToken>();
        {
            std::lock_guard lock{mutex_};
            activeCancellation_ = cancellation;
        }

        const validation::ValidationResult result = validationService_.check(cancellation);

        {
            std::lock_guard lock{mutex_};
            activeCancellation_.reset();
        }

        ProtocolFrame response;
        response.set_request_id(requestId);
        auto* worldCheck = response.mutable_result()->mutable_world_check();
        worldCheck->set_revision(result.revision);
        worldCheck->set_cancelled(result.cancelled);

        // When cancelled, partial results are never published. The frontend
        // receives cancelled=true with empty diagnostics and knows the
        // previous published set is still valid (if any).
        if (!result.cancelled) {
            // Mark stale if the published diagnostics were computed against a
            // different revision than the current canonical revision.
            const bool stale = diagnosticStore_.hasPublished()
                && diagnosticStore_.publishedRevision() != result.revision;
            worldCheck->set_stale(stale);

            for (const auto& diagnostic : result.diagnostics) {
                fillDiagnostic(worldCheck->add_diagnostics(), diagnostic);
            }

            // Publish the new diagnostic set and broadcast incremental
            // added/removed events to all connections.
            publishDiagnosticEvents(result.diagnostics, result.revision);
        }

        sink_.sendToConnection(connectionId, response);

        const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startedAt);
        runtime::logInfo("application", "world.check_handled",
            {{"requestId", requestId},
                {"outcome", result.cancelled ? "cancelled" : "ok"},
                {"diagnostics", std::to_string(result.diagnostics.size())},
                {"revision", std::to_string(result.revision)},
                {"durationMs", std::to_string(duration.count())}});
    } catch (const CommandFailure& failure) {
        sendFailureResult(connectionId, requestId, failure);
        runtime::logInfo("application", "world.check_failed",
            {{"requestId", requestId},
                {"outcome", std::to_string(static_cast<int>(failure.code()))}});
    } catch (const std::exception& error) {
        runtime::logError("application", "world.check_internal_error", {{"detail", error.what()}});
        sendInternalErrorResult(connectionId, requestId, "world.check");
    }
}

void CommandProcessor::handleWorldCancelCheck(const std::string& connectionId, const ProtocolFrame& frame) {
    const std::string requestId = frame.request_id();

    // Signal the active validation run to cancel. This is called from the
    // network thread (intercepted in post()) so the signal reaches the
    // executor thread's active validator without queue latency.
    bool requested = false;
    {
        std::lock_guard lock{mutex_};
        if (activeCancellation_) {
            activeCancellation_->requestCancellation();
            requested = true;
        }
    }

    ProtocolFrame response;
    response.set_request_id(requestId);
    response.mutable_result()->mutable_world_cancel_check()->set_cancellation_requested(requested);
    sink_.sendToConnection(connectionId, response);

    runtime::logInfo("application", "world.cancel_check_handled",
        {{"requestId", requestId},
            {"requested", requested ? "true" : "false"}});
}

void CommandProcessor::publishDiagnosticEvents(
    const std::vector<domain::validation::Diagnostic>& next,
    std::uint64_t revision) {
    const auto changes = diagnosticStore_.publish(next, revision);
    for (const auto& change : changes) {
        ProtocolFrame eventFrame;
        auto* envelope = eventFrame.mutable_event();
        envelope->set_event_id(runtime::generateUuidV4());
        if (change.kind == validation::DiagnosticStore::ChangeKind::Added) {
            auto* added = envelope->mutable_diagnostic_added();
            fillDiagnostic(added->mutable_diagnostic(), change.diagnostic);
        } else {
            auto* removed = envelope->mutable_diagnostic_removed();
            removed->set_code(change.diagnostic.code);
            for (const auto& entity : change.diagnostic.entities) {
                auto* ref = removed->add_entities();
                ref->set_kind(entity.kind);
                ref->set_id(entity.id);
            }
            removed->set_revision(revision);
        }
        sink_.broadcastEvent(eventFrame);
    }
}

void CommandProcessor::clearDiagnostics(const std::string& reason) {
    // Always broadcast a cleared event: even if the store was already empty,
    // the frontend needs to know diagnostics are invalidated for the given
    // reason (project_closed, revision_changed).
    (void)diagnosticStore_.clear();
    ProtocolFrame eventFrame;
    auto* envelope = eventFrame.mutable_event();
    envelope->set_event_id(runtime::generateUuidV4());
    auto* cleared = envelope->mutable_diagnostic_cleared();
    cleared->set_reason(reason);
    cleared->set_revision(0);
    sink_.broadcastEvent(eventFrame);
}

template <typename UseCase>
void CommandProcessor::runServiceCommand(
    const std::string& connectionId,
    const ProtocolFrame& frame,
    UseCase&& useCase) {
    const std::string requestId = frame.request_id();
    const std::string_view label = commandName(frame);
    const auto startedAt = std::chrono::steady_clock::now();

    try {
        const ProjectCommandResult result = useCase();

        ProtocolFrame response;
        response.set_request_id(requestId);
        if (result.sessionClosed) {
            response.mutable_result()->mutable_project_closed()->set_project_uuid(result.record.uuid);
        } else {
            fillSummary(response.mutable_result()->mutable_project_state()->mutable_summary(), result.record);
        }
        sink_.sendToConnection(connectionId, response);
        publishEvents(result);

        const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startedAt);
        runtime::logInfo("application", "project.command_handled",
            {{"command", std::string{label}},
                {"requestId", requestId},
                {"outcome", "ok"},
                {"durationMs", std::to_string(duration.count())}});
    } catch (const CommandFailure& failure) {
        sendFailureResult(connectionId, requestId, failure);
        const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startedAt);
        runtime::logInfo("application", "project.command_failed",
            {{"command", std::string{label}},
                {"requestId", requestId},
                {"outcome", std::to_string(static_cast<int>(failure.code()))},
                {"durationMs", std::to_string(duration.count())}});
    } catch (const std::exception& error) {
        // Internal details stay in the log; the client receives a stable
        // internal error code.
        runtime::logError("application", "project.command_internal_error",
            {{"command", std::string{label}}, {"detail", error.what()}});
        sendInternalErrorResult(connectionId, requestId, label);
    }
}

void CommandProcessor::publishEvents(const ProjectCommandResult& result) {
    for (const ProjectEvent& event : result.events) {
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
        }
        sink_.broadcastEvent(eventFrame);

        // After broadcasting the project lifecycle event, invalidate
        // diagnostics if the canonical state changed in a way that makes
        // them stale.
        if (event.kind == ProjectEventKind::Closed) {
            clearDiagnostics("project_closed");
        } else if (event.kind == ProjectEventKind::RevisionChanged) {
            clearDiagnostics("revision_changed");
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

} // namespace infraforge::application
