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
//
// Threading contract: post() is called from network threads and is
// enqueue-only with respect to canonical project state. It does not read
// ProjectStore, does not call broadcastEvent, and does not emit job events.
// All job lifecycle events (queued, started, completed, failed) are emitted
// from the executor thread inside processCommand(), which is the sole owner
// of the job lifecycle. This guarantees deterministic queued -> started ->
// terminal ordering and prevents orphan pending jobs.
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
    // Idempotent; also invoked by the destructor. Commands discarded during
    // shutdown never had job.queued emitted (post() is enqueue-only), so no
    // orphan pending jobs are created.
    void shutdown();

private:
    struct PostedCommand {
        std::string connectionId;
        std::string jobId;
        std::unique_ptr<protocol::v1::Frame> frame;
    };

    void runExecutor();
    // Lifecycle envelope: emits job.queued + job.started, dispatches to the
    // appropriate handle* method, and emits job.completed/job.failed as the
    // terminal event. All validation failures and service exceptions are
    // caught here so every queued job reaches exactly one terminal state.
    // Runs on the executor thread; safe to read ProjectStore here.
    void processCommand(const std::string& connectionId, const std::string& jobId, const protocol::v1::Frame& frame);

    void handleCreateProject(const std::string& connectionId, const protocol::v1::Frame& frame);
    void handleOpenProject(const std::string& connectionId, const protocol::v1::Frame& frame);
    void handleSaveProjectAs(const std::string& connectionId, const protocol::v1::Frame& frame);
    void handleGetGeoreference(const std::string& connectionId, const protocol::v1::Frame& frame);
    void handleSetGeoreference(const std::string& connectionId, const protocol::v1::Frame& frame);
    void handleTransformToProjectGlobal(const std::string& connectionId, const protocol::v1::Frame& frame);

    // Executes one service use case, then emits the correlated result frame
    // (state or closed) and the derived event frames. Argument-validation
    // failures throw CommandFailure before reaching this.
    template <typename UseCase>
    void runServiceCommand(const std::string& connectionId, const protocol::v1::Frame& frame, UseCase&& useCase);

    // Shared command envelope: timing, structured logging, and error
    // translation. `emit` sends the result frame and publishes any events.
    // Exceptions propagate to the caller (processCommand) which owns the
    // job lifecycle (queued -> started -> completed/failed).
    template <typename Emit>
    void executeCommand(Emit&& emit);

    void publishEvents(std::span<const ProjectEvent> events);

    // Emits a job lifecycle event frame. These are real events sourced from
    // the application executor's command lifecycle, not fabricated. All
    // publishJob* methods are called from the executor thread.
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
