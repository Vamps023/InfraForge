#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace infraforge::application {

// Long-operation job states (TRD "Long operations"): queued, running,
// completed, failed, cancelled. Completion is applied on the application
// executor only after all transactional side effects succeeded.
enum class JobState : std::uint8_t {
    Queued,
    Running,
    Completed,
    Failed,
    Cancelled,
};

[[nodiscard]] std::string_view jobStateName(JobState state) noexcept;

struct JobProgress {
    std::uint64_t unitsDone{0};
    std::uint64_t unitsTotal{0};
    // Normalized overall progress in [0,1] across all phases. When the job
    // body uses phase-weighted reporting, this is the coherent single value
    // the Operations surface displays, avoiding backwards progress jumps.
    double normalized{0.0};
    std::string label; // human-readable phase description, e.g. "copying source"
};

struct JobRecord {
    std::string jobId;
    std::string kind; // e.g. "terrain.import"
    JobState state{JobState::Queued};
    JobProgress progress;
    std::string message; // failure detail when Failed; empty otherwise
    std::string createdAt;
    bool cancellable{true};
};

// Thrown by a job body at its next bounded cancellation checkpoint.
// Bodies clean up through RAII and let it propagate; the job system owns
// the cancelled-state transition.
struct JobCancelled {};

// Thread-safe per-job control surface handed to a running body.
class JobContext {
public:
    [[nodiscard]] bool isCancelled() const noexcept { return cancelled_.load(std::memory_order_relaxed); }

    // Cooperative cancellation checkpoint: throws JobCancelled when a
    // cancellation was requested. Call at bounded work intervals.
    void throwIfCancelled() const {
        if (isCancelled()) {
            throw JobCancelled{};
        }
    }

    // Reports real measured work (units processed / total units) for the
    // current phase. The job system throttles executor-side progress events
    // to percent changes and computes the normalized overall progress.
    void reportProgress(std::uint64_t unitsDone, std::uint64_t unitsTotal, std::string label);

    // Reports a pre-computed normalized progress in [0,1] across all phases.
    // Use this when the body manages its own phase weighting. The label
    // describes the current phase.
    void reportNormalizedProgress(double normalized, std::string label);

private:
    friend class JobSystem;
    std::atomic_bool cancelled_{false};
    // Progress sink installed by JobSystem::submit; marshals the report
    // into the registry and onto the application executor.
    std::function<void(std::uint64_t, std::uint64_t, double, const std::string&)> progressSink_;
    std::uint64_t unitsDone_{0};
    std::uint64_t unitsTotal_{0};
    double normalized_{0.0};
    std::string label_;
};

// Result of a finished job, delivered on the application executor.
struct JobOutcome {
    JobRecord record;
    // Typed by the submitter's completion handler; carries the produced
    // result data (never partial canonical mutations).
    std::shared_ptr<void> payload;
};

// Serial background worker for CPU-heavy cancellable work (imports, tile
// generation). One job runs at a time on the worker thread; bodies use
// immutable inputs and produce outcomes that the application layer
// validates and applies on the executor (THREADING_MODEL.md "Worker pool";
// "Worker jobs never commit partial canonical state").
//
// The worker body produces a payload; the executor-side completion handler
// applies the canonical commit. The job remains in the Running state
// (internally flagged as "finalizing") until the completion handler calls
// markCompleted() or markFailed(). This guarantees the authoritative
// registry state never says Completed before the canonical commit succeeds.
//
// Outcome/progress handlers are marshalled onto the application executor
// through the thread-safe poster supplied at construction, so all event
// emission and canonical state application stays serialized.
class JobSystem {
public:
    // Result payload a job body hands back; the application layer applies
    // it transactionally on the executor (worker jobs never commit
    // canonical state themselves).
    using Payload = std::shared_ptr<void>;
    using Body = std::function<Payload(JobContext& context)>;
    // Executor-side handlers (invoked on the application executor thread).
    using ProgressHandler = std::function<void(const JobRecord& record)>;
    using CompletionHandler = std::function<void(const JobOutcome& outcome)>;
    // Thread-safe bridge onto the application executor.
    using ExecutorPoster = std::function<void(std::function<void()> task)>;

    explicit JobSystem(ExecutorPoster postToExecutor);
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    // Enqueues a job (executor thread). Returns its queued record.
    JobRecord submit(std::string kind, std::string progressLabel, Body body,
        ProgressHandler onProgress, CompletionHandler onComplete);

    // Requests cooperative cancellation. Safe from any thread; a queued job
    // is cancelled without running, a running job stops at its next
    // checkpoint. Returns false for unknown or already-finished jobs.
    bool requestCancel(const std::string& jobId);

    // Finalization API (executor thread). Called by the completion handler
    // after the executor-side canonical commit. Until one of these is
    // called, the job's authoritative state remains Running (finalizing).
    // markCompleted: the canonical commit succeeded; state becomes Completed.
    void markCompleted(const std::string& jobId);
    // markFailed: the canonical commit failed; state becomes Failed with the
    // given message. The registry is the authoritative source.
    void markFailed(const std::string& jobId, std::string message);

    // For jobs whose completion handler does not perform canonical commit
    // (e.g. tile generation), the body's success is the terminal state.
    // submit() auto-finalizes such jobs unless the completion handler calls
    // markCompleted/markFailed first. This is controlled by the
    // `requiresFinalization` flag on submit (default: true for import).
    JobRecord submit(std::string kind, std::string progressLabel, Body body,
        ProgressHandler onProgress, CompletionHandler onComplete,
        bool requiresFinalization);

    // Explicit shutdown: stops accepting work, cancels all active jobs,
    // joins the worker thread. Must be called before destroying dependent
    // services (TerrainService, readers, WorldState). Idempotent.
    void shutdown();

    [[nodiscard]] std::optional<JobRecord> job(const std::string& jobId) const;
    [[nodiscard]] std::vector<JobRecord> listJobs() const;

private:
    struct Entry {
        std::string jobId;
        std::string kind;
        std::string createdAt;
        JobState state{JobState::Queued};
        JobProgress progress;
        std::string message;
        JobContext context;
        Body body;
        ProgressHandler onProgress;
        CompletionHandler onComplete;
        std::uint32_t lastReportedPercent{0};
        // Internal finalization flag: when true, the worker body has
        // finished successfully and the executor-side completion handler
        // is running. The public state remains Running until
        // markCompleted/markFailed transitions it.
        bool finalizing_{false};
        // When false, the body's success is the terminal state (no
        // executor-side finalization needed). The state becomes Completed
        // immediately when the body returns.
        bool requiresFinalization_{true};
        // Monotonic creation sequence for deterministic eviction ordering.
        std::uint64_t sequence{0};
        // Monotonic completion sequence (set when the job reaches a
        // terminal state). Zero means not yet terminal. Used for
        // oldest-finished-first eviction.
        std::uint64_t finishedSequence{0};
    };

    void runWorker();
    void finishEntry(Entry& entry, JobState state, std::string message, Payload payload);
    void publishRecord(const Entry& entry);
    void evictFinished();
    JobRecord recordFromEntry(const Entry& entry) const;

    mutable std::mutex mutex_;
    std::condition_variable signal_;
    std::deque<std::string> queue_;
    std::map<std::string, std::unique_ptr<Entry>> jobs_;
    std::thread worker_;
    ExecutorPoster postToExecutor_;
    bool stopped_{false};
    // Monotonic counters for creation and completion ordering.
    std::uint64_t nextSequence_{0};
    std::uint64_t nextFinishedSequence_{0};
};

} // namespace infraforge::application
