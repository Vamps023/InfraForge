#pragma once

#include "infraforge/application/CommandFailure.hpp"
#include "infraforge/application/GeoService.hpp"
#include "infraforge/application/JobSystem.hpp"
#include "infraforge/application/ProjectService.hpp"
#include "infraforge/application/RoadService.hpp"
#include "infraforge/application/TerrainService.hpp"
#include "infraforge/application/WorldState.hpp"
#include "infraforge/persistence/GdalTerrainSource.hpp"
#include "infraforge/ports/ProjectStore.hpp"
#include "infraforge/ports/TerrainSource.hpp"

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
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
// All command-level job lifecycle events (queued, started, completed, failed)
// are emitted from the executor thread inside processCommand(), which is the
// sole owner of the command lifecycle. This guarantees deterministic queued ->
// started -> terminal ordering and prevents orphan pending jobs.
//
// Long-running terrain operations (import, tile regeneration) are submitted
// to the JobSystem as background jobs with their own job IDs. The command
// dispatch lifecycle completes immediately (meaning "command accepted"); the
// background job lifecycle (queued -> started -> progress -> terminal) is the
// real long operation tracked by the Operations surface. The two lifecycles
// have different job IDs and are never confused.
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

    // Runs an arbitrary task on the application executor (the job system's
    // completion bridge). Tasks posted after shutdown are dropped with a
    // log record.
    void postTask(std::function<void()> task);

    // Stops accepting work, joins the application executor (sole mutator of
    // canonical state), then cancels/joins the JobSystem worker, then clears
    // terrain session state. Idempotent; also invoked by the destructor.
    // This ordering prevents cross-thread mutation of executor-owned state
    // and use-after-free: the executor is dead before the worker is joined,
    // and the worker is joined before TerrainService is touched or destroyed.
    void shutdown();

private:
    struct PostedCommand {
        std::string connectionId;
        std::string jobId;
        std::unique_ptr<protocol::v1::Frame> frame;
    };
    struct PostedTask {
        std::function<void()> task;
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

    void handleTerrainProbeSource(const std::string& connectionId, const protocol::v1::Frame& frame);
    void handleTerrainImportDataset(const std::string& connectionId, const protocol::v1::Frame& frame);
    void handleTerrainListDatasets(const std::string& connectionId, const protocol::v1::Frame& frame);
    void handleTerrainGetDataset(const std::string& connectionId, const protocol::v1::Frame& frame);
    void handleTerrainSample(const std::string& connectionId, const protocol::v1::Frame& frame);
    void handleTerrainRegenerateTiles(const std::string& connectionId, const protocol::v1::Frame& frame);
    void handleTerrainGetScene(const std::string& connectionId, const protocol::v1::Frame& frame);
    void handleTerrainListSources(const std::string& connectionId, const protocol::v1::Frame& frame);
    void handleTerrainPlanDownload(const std::string& connectionId, const protocol::v1::Frame& frame);
    void handleTerrainDownloadSelected(const std::string& connectionId, const protocol::v1::Frame& frame);
    void handleTerrainExport(const std::string& connectionId, const protocol::v1::Frame& frame);
    void handleJobCancel(const std::string& connectionId, const protocol::v1::Frame& frame);
    void handleJobList(const std::string& connectionId, const protocol::v1::Frame& frame);

    // Road command handlers.
    void handleCreateRoad(const std::string& connectionId, const protocol::v1::Frame& frame);
    void handleDeleteRoad(const std::string& connectionId, const protocol::v1::Frame& frame);
    void handleRenameRoad(const std::string& connectionId, const protocol::v1::Frame& frame);
    void handleInsertRoadControl(const std::string& connectionId, const protocol::v1::Frame& frame);
    void handleMoveRoadControl(const std::string& connectionId, const protocol::v1::Frame& frame);
    void handleDeleteRoadControl(const std::string& connectionId, const protocol::v1::Frame& frame);
    void handleFitRoadSource(const std::string& connectionId, const protocol::v1::Frame& frame);
    void handleUpdateRoadElevation(const std::string& connectionId, const protocol::v1::Frame& frame);
    void handleUpdateRoadSuperelevation(const std::string& connectionId, const protocol::v1::Frame& frame);
    void handleUpdateRoadWidth(const std::string& connectionId, const protocol::v1::Frame& frame);
    void handleUndoRoad(const std::string& connectionId, const protocol::v1::Frame& frame);
    void handleRedoRoad(const std::string& connectionId, const protocol::v1::Frame& frame);
    void handleListRoads(const std::string& connectionId, const protocol::v1::Frame& frame);
    void handleGetRoad(const std::string& connectionId, const protocol::v1::Frame& frame);
    void handleGetRoadScene(const std::string& connectionId, const protocol::v1::Frame& frame);

    // Executes one service use case, then emits the correlated result frame
    // (state or closed) and the derived event frames. Argument-validation
    // failures are handled by the handle* methods before reaching this.
    template <typename UseCase>
    void runServiceCommand(const std::string& connectionId, const protocol::v1::Frame& frame, UseCase&& useCase);

    // Shared command envelope: timing, structured logging, and error
    // translation. `emit` sends the result frame and publishes any events.
    // Exceptions propagate to the caller (processCommand) which owns the
    // job lifecycle (queued -> started -> completed/failed).
    template <typename Emit>
    void executeCommand(Emit&& emit);

    void publishEvents(std::span<const ProjectEvent> events);
    void publishTerrainEvent(const TerrainServiceEvent& event);
    void publishRoadEvent(const RoadServiceEvent& event);

    // Emits a background-job lifecycle event frame using the canonical 1.3
    // event format (owned by Issue #5). These are real events sourced from
    // the JobSystem completion bridge, not fabricated.
    void publishJobQueued(const std::string& jobId, std::string_view operation, std::string_view label, std::string_view targetId, bool cancellable);
    void publishJobStarted(const std::string& jobId);
    void publishJobProgress(const std::string& jobId, double progress, std::uint64_t processed, std::uint64_t total, std::string_view message);
    void publishJobCompleted(const std::string& jobId);
    void publishJobFailed(const std::string& jobId, std::string_view errorCode, std::string_view errorMessage);
    void publishJobCancelled(const std::string& jobId);

    // Emits diagnostic lifecycle events for real backend-observed conditions.
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

    void syncTerrainSession();

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
    std::deque<PostedTask> tasks_;
    bool started_{false};
    bool stopped_{false};

    // Declared after the executor fields so they are destroyed first: their
    // workers can then still post (dropped) tasks while the processor's own
    // synchronization primitives are alive. The explicit shutdown() joins the
    // JobSystem worker before these are destroyed, preventing use-after-free.
    std::optional<persistence::GdalTerrainSource> terrainReader_;
    WorldState world_;
    std::optional<JobSystem> jobs_;
    std::optional<TerrainService> terrainService_;
    std::optional<RoadService> roadService_;

    // Tracks whether any terrain-scoped diagnostics have been emitted, so
    // close only emits diagnosticCleared("terrain") when there is something
    // to clear (avoids spurious events in the event stream).
    bool hasTerrainDiagnostics_{false};
};

} // namespace infraforge::application
