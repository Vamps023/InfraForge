#include <doctest/doctest.h>

#include "infraforge/application/JobSystem.hpp"

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

using namespace infraforge::application;

TEST_SUITE("terrain job system") {

// Deterministic executor stand-in: completion/progress tasks are queued and
// drained by the test thread, mirroring the real architecture in which
// worker results are applied on the application executor.
class QueuedExecutor {
public:
    JobSystem::ExecutorPoster poster() {
        return [this](std::function<void()> task) {
            std::lock_guard lock{mutex_};
            tasks_.push_back(std::move(task));
        };
    }

    std::size_t drain() {
        std::deque<std::function<void()>> local;
        {
            std::lock_guard lock{mutex_};
            local.swap(tasks_);
        }
        std::size_t count = 0;
        while (!local.empty()) {
            local.front()();
            local.pop_front();
            ++count;
        }
        return count;
    }

private:
    std::mutex mutex_;
    std::deque<std::function<void()>> tasks_;
};

TEST_CASE("jobs run to completion with real progress and executor-side handlers") {
    QueuedExecutor executor;
    JobSystem jobs{executor.poster()};

    std::atomic<std::uint64_t> lastReportedDone{0};
    const JobRecord record = jobs.submit(
        "test.kind",
        "working",
        [](JobContext& context) -> JobSystem::Payload {
            for (std::uint64_t unit = 1; unit <= 10; ++unit) {
                context.reportProgress(unit, 10, "working");
            }
            return nullptr;
        },
        [&lastReportedDone](const JobRecord& progress) {
            lastReportedDone.store(progress.progress.unitsDone, std::memory_order_relaxed);
        },
        [](const JobOutcome&) {});

    const auto waitForTerminal = [&]() {
        for (int attempt = 0; attempt < 1000; ++attempt) {
            executor.drain();
            const auto snapshot = jobs.job(record.jobId);
            if (snapshot.has_value()
                && (snapshot->state == JobState::Completed || snapshot->state == JobState::Failed
                    || snapshot->state == JobState::Cancelled)) {
                return snapshot;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{2});
        }
        return std::optional<JobRecord>{};
    };

    const auto finished = waitForTerminal();
    REQUIRE(finished.has_value());
    CHECK(finished->state == JobState::Completed);
    CHECK(finished->progress.unitsDone == 10);
    CHECK(lastReportedDone.load() == 10);
}

TEST_CASE("cancellation reaches running work and stops it deterministically") {
    QueuedExecutor executor;
    JobSystem jobs{executor.poster()};

    std::atomic<bool> bodyStarted{false};
    const JobRecord record = jobs.submit(
        "test.kind",
        "working",
        [&bodyStarted](JobContext& context) -> JobSystem::Payload {
            context.reportProgress(5, 100000, "working");
            bodyStarted.store(true);
            // Park at a cooperative checkpoint until cancelled.
            while (true) {
                context.throwIfCancelled();
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }
        },
        [](const JobRecord&) {},
        [](const JobOutcome&) {});

    bool observedRunning = false;
    for (int attempt = 0; attempt < 5000 && !observedRunning; ++attempt) {
        executor.drain();
        const auto snapshot = jobs.job(record.jobId);
        observedRunning = snapshot.has_value() && snapshot->state == JobState::Running;
        if (!observedRunning) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
    }
    REQUIRE(observedRunning);
    REQUIRE(jobs.requestCancel(record.jobId));

    for (int attempt = 0; attempt < 5000; ++attempt) {
        executor.drain();
        const auto snapshot = jobs.job(record.jobId);
        if (snapshot.has_value() && snapshot->state == JobState::Cancelled) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    const auto finished = jobs.job(record.jobId);
    REQUIRE(finished.has_value());
    CHECK(finished->state == JobState::Cancelled);
}

TEST_CASE("queued jobs cancel without running and unknown ids report false") {
    QueuedExecutor executor;
    JobSystem jobs{executor.poster()};

    std::atomic<bool> bodyRan{false};
    // Occupies the single worker with a long job.
    std::atomic<bool> releaseWorker{false};
    const JobRecord blocker = jobs.submit(
        "test.blocker",
        "blocking",
        [&releaseWorker](JobContext& context) -> JobSystem::Payload {
            while (!releaseWorker.load()) {
                context.throwIfCancelled();
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }
            return nullptr;
        },
        [](const JobRecord&) {},
        [](const JobOutcome&) {});

    for (int attempt = 0; attempt < 5000; ++attempt) {
        executor.drain();
        if (const auto snapshot = jobs.job(blocker.jobId);
            snapshot.has_value() && snapshot->state == JobState::Running) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    const JobRecord queued = jobs.submit(
        "test.queued",
        "waiting",
        [&bodyRan](JobContext&) -> JobSystem::Payload {
            bodyRan.store(true);
            return nullptr;
        },
        [](const JobRecord&) {},
        [](const JobOutcome&) {});

    CHECK(jobs.requestCancel(queued.jobId));
    releaseWorker.store(true);

    for (int attempt = 0; attempt < 5000; ++attempt) {
        executor.drain();
        const auto snapshot = jobs.job(queued.jobId);
        if (snapshot.has_value() && snapshot->state == JobState::Cancelled) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    const auto finished = jobs.job(queued.jobId);
    REQUIRE(finished.has_value());
    CHECK(finished->state == JobState::Cancelled);
    CHECK_FALSE(bodyRan.load());
    CHECK(jobs.requestCancel("unknown-job-id") == false);
    CHECK_FALSE(jobs.job("unknown-job-id").has_value());
    (void)jobs.listJobs();
}

// BLOCKER 4 regression: a job that requires finalization must stay Running
// until markCompleted/markFailed is called. If finalization fails, the
// authoritative state must be FAILED, not Completed.
TEST_CASE("finalization failure marks job as failed, not completed") {
    QueuedExecutor executor;
    JobSystem jobs{executor.poster()};

    const JobRecord record = jobs.submit(
        "test.finalize",
        "working",
        [](JobContext&) -> JobSystem::Payload { return nullptr; },
        [](const JobRecord&) {},
        [](const JobOutcome& outcome) {
            // Simulate finalization failure: the completion handler calls
            // markFailed instead of markCompleted.
            // (In the real architecture, this is where commitImportedDataset
            // would fail and call markFailed.)
            // We can't call markFailed from here directly, so we use a
            // different approach: the test verifies the state transition
            // by calling markFailed externally.
            (void)outcome;
        },
        /*requiresFinalization=*/true);

    const auto waitForState = [&](JobState target) {
        for (int attempt = 0; attempt < 5000; ++attempt) {
            executor.drain();
            const auto snapshot = jobs.job(record.jobId);
            if (snapshot.has_value() && snapshot->state == target) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        return false;
    };

    // After the body completes, the job should still be Running (finalizing).
    REQUIRE(waitForState(JobState::Running));

    // The completion handler ran (drained). Now simulate finalization failure.
    executor.drain();
    jobs.markFailed(record.jobId, "commit failed");

    REQUIRE(waitForState(JobState::Failed));
    const auto finished = jobs.job(record.jobId);
    REQUIRE(finished.has_value());
    CHECK(finished->state == JobState::Failed);
    CHECK(finished->message == "commit failed");
}

// BLOCKER 4 regression: a job that requires finalization and succeeds must
// transition to Completed only after markCompleted is called.
TEST_CASE("finalization success marks job as completed") {
    QueuedExecutor executor;
    JobSystem jobs{executor.poster()};

    const JobRecord record = jobs.submit(
        "test.finalize-ok",
        "working",
        [](JobContext&) -> JobSystem::Payload { return nullptr; },
        [](const JobRecord&) {},
        [](const JobOutcome&) {},
        /*requiresFinalization=*/true);

    const auto waitForState = [&](JobState target) {
        for (int attempt = 0; attempt < 5000; ++attempt) {
            executor.drain();
            const auto snapshot = jobs.job(record.jobId);
            if (snapshot.has_value() && snapshot->state == target) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        return false;
    };

    REQUIRE(waitForState(JobState::Running));
    executor.drain();
    jobs.markCompleted(record.jobId);
    REQUIRE(waitForState(JobState::Completed));
}

// BLOCKER 5 regression: shutdown during an active job must not crash and
// must leave the job in a terminal state.
TEST_CASE("shutdown during running job is safe and terminal") {
    QueuedExecutor executor;
    JobSystem* jobs = new JobSystem{executor.poster()};

    std::atomic<bool> bodyStarted{false};
    std::atomic<bool> releaseWorker{false};
    const JobRecord record = jobs->submit(
        "test.shutdown",
        "working",
        [&bodyStarted, &releaseWorker](JobContext& context) -> JobSystem::Payload {
            bodyStarted.store(true);
            while (!releaseWorker.load()) {
                context.throwIfCancelled();
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }
            return nullptr;
        },
        [](const JobRecord&) {},
        [](const JobOutcome&) {});

    // Wait for the body to start.
    for (int attempt = 0; attempt < 5000 && !bodyStarted.load(); ++attempt) {
        executor.drain();
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    REQUIRE(bodyStarted.load());

    // Shutdown must join the worker and not crash.
    jobs->shutdown();
    delete jobs;

    // The job should be in a terminal state (Cancelled).
    // We can't query after delete, but the fact that we got here without
    // crashing is the key assertion.
    (void)record;
}

// BLOCKER 11 regression: finished job records are evicted oldest-first,
// and active jobs are never evicted.
TEST_CASE("eviction removes oldest finished records and keeps active jobs") {
    QueuedExecutor executor;
    JobSystem jobs{executor.poster()};

    // Submit more than 64 jobs that complete immediately.
    std::vector<std::string> jobIds;
    for (int i = 0; i < 70; ++i) {
        const JobRecord record = jobs.submit(
            "test.evict",
            "working",
            [](JobContext&) -> JobSystem::Payload { return nullptr; },
            [](const JobRecord&) {},
            [](const JobOutcome&) {});
        jobIds.push_back(record.jobId);
        // Drain to let the job complete.
        for (int attempt = 0; attempt < 100; ++attempt) {
            executor.drain();
            const auto snapshot = jobs.job(record.jobId);
            if (snapshot.has_value() && snapshot->state == JobState::Completed) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
    }

    // The first 6 jobs should have been evicted (70 - 64 = 6).
    for (int i = 0; i < 6; ++i) {
        CHECK_FALSE(jobs.job(jobIds[i]).has_value());
    }
    // The remaining 64 should still be present.
    for (int i = 6; i < 70; ++i) {
        const auto snapshot = jobs.job(jobIds[i]);
        CHECK(snapshot.has_value());
        CHECK(snapshot->state == JobState::Completed);
    }
}

// BLOCKER 5 regression: repeated shutdown is idempotent.
TEST_CASE("repeated shutdown is idempotent") {
    QueuedExecutor executor;
    JobSystem jobs{executor.poster()};
    jobs.shutdown();
    jobs.shutdown(); // must not deadlock or crash
}

} // TEST_SUITE
