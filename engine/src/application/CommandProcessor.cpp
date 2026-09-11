#include "infraforge/application/CommandProcessor.hpp"

#include "infraforge/application/ProjectService.hpp"
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
    case protocol::v1::CommandEnvelope::COMMAND_NOT_SET:
        break;
    }
    return "unknown";
}

} // namespace

CommandProcessor::CommandProcessor(ports::ProjectStore& store, CommandSink& sink)
    : store_(store),
      service_(store),
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
    const std::string_view label = frame.has_command() ? commandName(frame) : std::string_view{"unknown"};
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
