#pragma once

#include "infraforge/application/GeoService.hpp"
#include "infraforge/application/ProjectService.hpp"
#include "infraforge/ports/ProjectStore.hpp"

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>

namespace infraforge::protocol::v1 {
class Frame;
enum DiagnosticSeverity : int;
}

namespace infraforge::domain::geo {
class GeoTransformService;
}

namespace infraforge::application {

// Delivers serialized result/event frames from the application executor to
// the transport layer. Implementations must be safe to call from the executor
// thread while network callbacks run on their own threads.
class CommandSink {
public:
    virtual ~CommandSink() = default;

    virtual void sendToConnection(std::string_view connectionId, const protocol::v1::Frame& frame) = 0;
    virtual void broadcastEvent(const protocol::v1::Frame& frame) = 0;
};

// Single application executor (docs/01_ARCHITECTURE/THREADING_MODEL.md).
// Network callbacks never mutate project state directly; they post
// authenticated command frames, and this processor executes them one at a
// time, emitting result frames and event frames through the sink.
class CommandProcessor final {
public:
    CommandProcessor(
        ports::ProjectStore& store,
        const domain::geo::GeoTransformService& transforms,
        CommandSink& sink);
    ~CommandProcessor();

    CommandProcessor(const CommandProcessor&) = delete;
    CommandProcessor& operator=(const CommandProcessor&) = delete;

    void start();
    void post(std::string connectionId, protocol::v1::Frame frame);

    // Stops accepting work, discards queued commands, and joins the executor.
    // Idempotent; also invoked by the destructor.
    void shutdown();

private:
    struct PostedCommand {
        std::string connectionId;
        std::string jobId;
        std::unique_ptr<protocol::v1::Frame> frame;
    };

    void runExecutor();
    void processCommand(const std::string& connectionId, const std::string& jobId, const protocol::v1::Frame& frame);

    void handleCreateProject(const std::string& connectionId, const std::string& jobId, const protocol::v1::Frame& frame);
    void handleOpenProject(const std::string& connectionId, const std::string& jobId, const protocol::v1::Frame& frame);
    void handleSaveProjectAs(const std::string& connectionId, const std::string& jobId, const protocol::v1::Frame& frame);
    void handleGetGeoreference(const std::string& connectionId, const std::string& jobId, const protocol::v1::Frame& frame);
    void handleSetGeoreference(const std::string& connectionId, const std::string& jobId, const protocol::v1::Frame& frame);
    void handleTransformToProjectGlobal(const std::string& connectionId, const std::string& jobId, const protocol::v1::Frame& frame);

    // Executes one service use case, then emits the correlated result frame
    // (state or closed) and the derived event frames. Argument-validation
    // failures are handled by the handle* methods before reaching this.
    template <typename UseCase>
    void runServiceCommand(const std::string& connectionId, const std::string& jobId, const protocol::v1::Frame& frame, UseCase&& useCase);

    // Shared command envelope: timing, CommandFailure/error translation,
    // and structured logging. `emit` sends the result frame and publishes
    // any events. Emits job.started before and job.completed/job.failed after.
    template <typename Emit>
    void executeCommand(const std::string& connectionId, const std::string& jobId, const protocol::v1::Frame& frame, Emit&& emit);

    void publishEvents(std::span<const ProjectEvent> events);

    // Emits a job lifecycle event frame. These are real events sourced from
    // the application executor's command lifecycle, not fabricated.
    void publishJobQueued(const std::string& jobId, std::string_view operation, std::string_view label, std::string_view targetId);
    void publishJobStarted(const std::string& jobId);
    void publishJobCompleted(const std::string& jobId);
    void publishJobFailed(const std::string& jobId, std::string_view errorCode, std::string_view errorMessage);

    // Emits diagnostic lifecycle events for real backend-observed conditions.
    // The vertical CRS warning is emitted when a project has no vertical
    // reference configured; it is removed when one is added or the project
    // closes.
    void publishDiagnosticAdded(std::string_view diagnosticId, std::string_view source,
        protocol::v1::DiagnosticSeverity severity, std::string_view message, std::string_view targetId);
    void publishDiagnosticRemoved(std::string_view diagnosticId);
    void publishDiagnosticCleared(std::string_view source);

    // Evaluates the vertical reference state after a project open/create or
    // georeference change and emits the appropriate diagnostic event so the
    // Problems panel reflects the real geospatial condition.
    void evaluateVerticalReferenceDiagnostic(const domain::project::ProjectRecord& record);

    // Stable diagnostic ID for the "no vertical reference" warning on a
    // given project.
    static std::string verticalReferenceDiagnosticId(std::string_view projectUuid);

    void sendFailureResult(const std::string& connectionId, const std::string& requestId, const CommandFailure& failure);
    void sendInternalErrorResult(
        const std::string& connectionId,
        const std::string& requestId,
        std::string_view commandLabel);

    ports::ProjectStore& store_;
    ProjectService service_;
    GeoService geoService_;
    CommandSink& sink_;

    std::thread executor_;
    std::mutex mutex_;
    std::condition_variable signal_;
    std::deque<PostedCommand> queue_;
    bool started_{false};
    bool stopped_{false};
};

} // namespace infraforge::application
