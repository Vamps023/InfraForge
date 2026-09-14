#include "infraforge/application/JobSystem.hpp"

#include "infraforge/runtime/Logging.hpp"
#include "infraforge/runtime/Timestamp.hpp"
#include "infraforge/runtime/Uuid.hpp"

#include <algorithm>
#include <utility>

namespace infraforge::application {
namespace {

// Finished job records are kept for Operations-view queries; the oldest
// finished entries are evicted so a long engine session stays bounded.
constexpr std::size_t kMaxFinishedJobRecords = 64;

} // namespace

std::string_view jobStateName(const JobState state) noexcept {
    switch (state) {
    case JobState::Queued:
        return "queued";
    case JobState::Running:
        return "running";
    case JobState::Completed:
        return "completed";
    case JobState::Failed:
        return "failed";
    case JobState::Cancelled:
        return "cancelled";
    }
    return "unknown";
}

void JobContext::reportProgress(const std::uint64_t unitsDone, const std::uint64_t unitsTotal, std::string label) {
    // Cancellation is re-checked here as well so a cancelled job stops
    // even if the body only reports progress without its own checkpoint.
    if (isCancelled()) {
        throw JobCancelled{};
    }
    unitsDone_ = unitsDone;
    unitsTotal_ = unitsTotal;
    // Compute normalized progress from raw units when no explicit normalized
    // value was set. This is the per-phase ratio; for multi-phase jobs the
    // body should use reportNormalizedProgress with phase weighting.
    normalized_ = unitsTotal > 0 ? static_cast<double>(unitsDone) / static_cast<double>(unitsTotal) : 0.0;
    label_ = std::move(label);
    if (progressSink_) {
        progressSink_(unitsDone_, unitsTotal_, normalized_, label_);
    }
}

void JobContext::reportNormalizedProgress(const double normalized, std::string label) {
    if (isCancelled()) {
        throw JobCancelled{};
    }
    normalized_ = normalized;
    label_ = std::move(label);
    if (progressSink_) {
        progressSink_(unitsDone_, unitsTotal_, normalized_, label_);
    }
}

JobSystem::JobSystem(ExecutorPoster postToExecutor)
    : postToExecutor_(std::move(postToExecutor)) {
    worker_ = std::thread([this] { runWorker(); });
}

JobSystem::~JobSystem() {
    shutdown();
}

void JobSystem::shutdown() {
    {
        std::lock_guard lock{mutex_};
        if (stopped_) {
            return;
        }
        stopped_ = true;
        // Cancel everything outstanding so the worker stops at its next
        // checkpoint. Outcomes are discarded: the executor is shutting down
        // (or already shut down) so posted completion handlers are dropped.
        // Cancel ALL non-terminal jobs, including the currently running one
        // (which was already popped from the queue and won't be in queue_).
        for (auto& [id, entry] : jobs_) {
            if (entry->state == JobState::Queued || entry->state == JobState::Running) {
                entry->context.cancelled_.store(true, std::memory_order_relaxed);
                entry->state = JobState::Cancelled;
                if (entry->finishedSequence == 0) {
                    entry->finishedSequence = ++nextFinishedSequence_;
                }
            }
        }
        queue_.clear();
    }
    signal_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

JobRecord JobSystem::submit(
    std::string kind, std::string progressLabel, Body body,
    ProgressHandler onProgress, CompletionHandler onComplete) {
    return submit(std::move(kind), std::move(progressLabel), std::move(body),
        std::move(onProgress), std::move(onComplete), false);
}

JobRecord JobSystem::submit(
    std::string kind, std::string progressLabel, Body body,
    ProgressHandler onProgress, CompletionHandler onComplete,
    bool requiresFinalization) {
    auto entry = std::make_unique<Entry>();
    entry->jobId = runtime::generateUuidV4();
    entry->kind = std::move(kind);
    entry->createdAt = runtime::utcTimestampNow();
    entry->progress.label = std::move(progressLabel);
    entry->body = std::move(body);
    entry->onProgress = std::move(onProgress);
    entry->onComplete = std::move(onComplete);
    entry->requiresFinalization_ = requiresFinalization;

    // Worker-side progress reports land in the registry under the mutex and
    // are marshalled onto the executor only when the integer percent value
    // changes — event volume scales with reporting granularity, not raw
    // report count.
    const std::string jobId = entry->jobId;
    entry->context.progressSink_ = [this, jobId](std::uint64_t unitsDone, std::uint64_t unitsTotal,
                                                  double normalized, const std::string& label) {
        bool publish = false;
        {
            std::lock_guard lock{mutex_};
            const auto found = jobs_.find(jobId);
            if (found == jobs_.end()) {
                return;
            }
            Entry& tracked = *found->second;
            tracked.progress.unitsDone = unitsDone;
            tracked.progress.unitsTotal = unitsTotal;
            tracked.progress.normalized = normalized;
            tracked.progress.label = label;
            const std::uint32_t percent = static_cast<std::uint32_t>(normalized * 100.0);
            if (percent != tracked.lastReportedPercent) {
                tracked.lastReportedPercent = percent;
                publish = true;
            }
        }
        if (publish) {
            std::lock_guard lock{mutex_};
            const auto found = jobs_.find(jobId);
            // The running entry cannot be evicted while its own body runs:
            // eviction happens only on the worker thread, which is here.
            if (found != jobs_.end()) {
                publishRecord(*found->second);
            }
        }
    };

    JobRecord record;
    record.jobId = entry->jobId;
    record.kind = entry->kind;
    record.state = entry->state;
    record.progress = entry->progress;
    record.createdAt = entry->createdAt;
    record.cancellable = true;

    {
        std::lock_guard lock{mutex_};
        entry->sequence = ++nextSequence_;
        queue_.push_back(entry->jobId);
        jobs_.emplace(entry->jobId, std::move(entry));
    }
    signal_.notify_one();
    return record;
}

bool JobSystem::requestCancel(const std::string& jobId) {
    std::lock_guard lock{mutex_};
    const auto found = jobs_.find(jobId);
    if (found == jobs_.end()) {
        return false;
    }
    Entry& entry = *found->second;
    if (entry.state != JobState::Queued && entry.state != JobState::Running) {
        return false;
    }
    // Cannot cancel a job that is finalizing (worker done, executor committing).
    if (entry.finalizing_) {
        return false;
    }
    entry.context.cancelled_.store(true, std::memory_order_relaxed);
    return true;
}

void JobSystem::markCompleted(const std::string& jobId) {
    std::lock_guard lock{mutex_};
    const auto found = jobs_.find(jobId);
    if (found == jobs_.end()) {
        return;
    }
    Entry& entry = *found->second;
    // Only transition from Running/Finalizing to Completed. A job that is
    // already terminal stays terminal (exactly one terminal state).
    if (entry.state != JobState::Running) {
        return;
    }
    entry.state = JobState::Completed;
    entry.finalizing_ = false;
    if (entry.finishedSequence == 0) {
        entry.finishedSequence = ++nextFinishedSequence_;
    }
    evictFinished();
}

void JobSystem::markFailed(const std::string& jobId, std::string message) {
    std::lock_guard lock{mutex_};
    const auto found = jobs_.find(jobId);
    if (found == jobs_.end()) {
        return;
    }
    Entry& entry = *found->second;
    // Only transition from Running/Finalizing to Failed. A job that is
    // already terminal stays terminal (exactly one terminal state).
    if (entry.state != JobState::Running) {
        return;
    }
    entry.state = JobState::Failed;
    entry.finalizing_ = false;
    entry.message = std::move(message);
    if (entry.finishedSequence == 0) {
        entry.finishedSequence = ++nextFinishedSequence_;
    }
    evictFinished();
}

void JobSystem::markFailed(const std::string& jobId, std::string message, std::string errorCode) {
    std::lock_guard lock{mutex_};
    const auto found = jobs_.find(jobId);
    if (found == jobs_.end()) {
        return;
    }
    Entry& entry = *found->second;
    if (entry.state != JobState::Running) {
        return;
    }
    entry.state = JobState::Failed;
    entry.finalizing_ = false;
    entry.message = std::move(message);
    entry.errorCode = std::move(errorCode);
    if (entry.finishedSequence == 0) {
        entry.finishedSequence = ++nextFinishedSequence_;
    }
    evictFinished();
}

JobRecord JobSystem::recordFromEntry(const Entry& entry) const {
    JobRecord record;
    record.jobId = entry.jobId;
    record.kind = entry.kind;
    record.state = entry.state;
    record.progress = entry.progress;
    record.message = entry.message;
    record.errorCode = entry.errorCode;
    record.createdAt = entry.createdAt;
    record.cancellable = !entry.finalizing_ && (entry.state == JobState::Queued || entry.state == JobState::Running);
    return record;
}

std::optional<JobRecord> JobSystem::job(const std::string& jobId) const {
    std::lock_guard lock{mutex_};
    const auto found = jobs_.find(jobId);
    if (found == jobs_.end()) {
        return std::nullopt;
    }
    return recordFromEntry(*found->second);
}

std::vector<JobRecord> JobSystem::listJobs() const {
    std::lock_guard lock{mutex_};
    std::vector<JobRecord> records;
    records.reserve(jobs_.size());
    // Deterministic ordering: by creation sequence (ascending). This is the
    // order jobs were submitted, which is stable and useful for the
    // Operations surface.
    std::vector<const Entry*> ordered;
    ordered.reserve(jobs_.size());
    for (const auto& [id, entry] : jobs_) {
        ordered.push_back(entry.get());
    }
    std::sort(ordered.begin(), ordered.end(),
        [](const Entry* a, const Entry* b) { return a->sequence < b->sequence; });
    for (const Entry* entry : ordered) {
        records.push_back(recordFromEntry(*entry));
    }
    return records;
}

void JobSystem::finishEntry(Entry& entry, const JobState state, std::string message, Payload payload) {
    JobOutcome outcome;
    outcome.record.jobId = entry.jobId;
    outcome.record.kind = entry.kind;
    outcome.record.state = state;
    outcome.record.progress = entry.progress;
    outcome.record.message = message;
    // Preserve typed failure code set by the body (BLOCKER 5).
    outcome.record.errorCode = entry.context.failureCode();
    outcome.record.createdAt = entry.createdAt;
    outcome.record.cancellable = false;
    outcome.payload = std::move(payload);

    const bool needsFinalization = (state == JobState::Completed && entry.requiresFinalization_);

    if (needsFinalization) {
        // The body succeeded, but the executor-side completion handler must
        // still commit canonical state. Keep the public state as Running and
        // set the internal finalizing flag. The completion handler will call
        // markCompleted() or markFailed() to set the terminal state.
        std::lock_guard lock{mutex_};
        entry.finalizing_ = true;
        // State stays Running; the registry reports Running until
        // markCompleted/markFailed transitions it.
    } else {
        // Either the body failed/cancelled, or the job does not require
        // executor-side finalization. Set the terminal state immediately.
        std::lock_guard lock{mutex_};
        entry.state = state;
        entry.message = std::move(message);
        entry.errorCode = entry.context.failureCode();
        entry.finalizing_ = false;
        if (entry.finishedSequence == 0) {
            entry.finishedSequence = ++nextFinishedSequence_;
        }
    }

    if (postToExecutor_ && entry.onComplete) {
        // Capture the completion handler by value; the registry entry may
        // be evicted before the executor runs this task.
        auto handler = entry.onComplete;
        postToExecutor_([handler, outcome = std::move(outcome)] { handler(outcome); });
    }
}

void JobSystem::publishRecord(const Entry& entry) {
    if (!postToExecutor_ || !entry.onProgress) {
        return;
    }
    JobRecord record = recordFromEntry(entry);
    auto handler = entry.onProgress;
    postToExecutor_([handler, record = std::move(record)] { handler(record); });
}

void JobSystem::evictFinished() {
    // Collect finished entries with their completion sequence for
    // oldest-finished-first eviction. Called under the mutex.
    std::vector<std::pair<std::uint64_t, std::string>> finished;
    for (const auto& [id, entry] : jobs_) {
        if (entry->state == JobState::Completed || entry->state == JobState::Failed
            || entry->state == JobState::Cancelled) {
            finished.emplace_back(entry->finishedSequence, id);
        }
    }
    // Sort by finished sequence (ascending = oldest first).
    std::sort(finished.begin(), finished.end());
    const std::size_t excess = finished.size() > kMaxFinishedJobRecords
        ? finished.size() - kMaxFinishedJobRecords
        : 0;
    for (std::size_t i = 0; i < excess; ++i) {
        jobs_.erase(finished[i].second);
    }
}

void JobSystem::runWorker() {
    for (;;) {
        std::string jobId;
        {
            std::unique_lock lock{mutex_};
            signal_.wait(lock, [this] { return stopped_ || !queue_.empty(); });
            if (queue_.empty()) {
                if (stopped_) {
                    return;
                }
                continue;
            }
            jobId = queue_.front();
            queue_.pop_front();
        }

        Entry* entryPtr = nullptr;
        {
            std::lock_guard lock{mutex_};
            const auto found = jobs_.find(jobId);
            if (found == jobs_.end()) {
                continue;
            }
            entryPtr = found->second.get();
        }

        Entry& entry = *entryPtr;
        if (entry.context.isCancelled()) {
            finishEntry(entry, JobState::Cancelled, "cancelled before start", nullptr);
            {
                std::lock_guard lock{mutex_};
                evictFinished();
            }
            continue;
        }

        {
            std::lock_guard lock{mutex_};
            entry.state = JobState::Running;
        }
        publishRecord(entry);

        try {
            Payload payload = entry.body(entry.context);
            finishEntry(entry, JobState::Completed, {}, std::move(payload));
        } catch (const JobCancelled&) {
            finishEntry(entry, JobState::Cancelled, "cancelled", nullptr);
        } catch (const std::exception& error) {
            finishEntry(entry, JobState::Failed, error.what(), nullptr);
        } catch (...) {
            finishEntry(entry, JobState::Failed, "unknown job failure", nullptr);
        }

        // Evict the oldest finished records beyond the retention bound.
        {
            std::lock_guard lock{mutex_};
            evictFinished();
        }
    }
}

} // namespace infraforge::application
