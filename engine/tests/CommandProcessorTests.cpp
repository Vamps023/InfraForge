#include <doctest/doctest.h>

#include "TestHelpers.hpp"
#include "infraforge/runtime/FileSystemUtf8.hpp"
#include "infraforge/application/CommandProcessor.hpp"
#include "infraforge/persistence/SqliteProjectStore.hpp"

#include <infraforge/protocol/v1/foundation.pb.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace {

using ProtocolFrame = infraforge::protocol::v1::Frame;

// Collects frames delivered by the processor's executor thread.
class RecordingSink final : public infraforge::application::CommandSink {
public:
    void sendToConnection(std::string_view connectionId, const ProtocolFrame& frame) override {
        std::lock_guard lock{mutex_};
        (void)connectionId;
        results_.push_back(frame);
        signal_.notify_all();
    }

    void broadcastEvent(const ProtocolFrame& frame) override {
        std::lock_guard lock{mutex_};
        events_.push_back(frame);
        signal_.notify_all();
    }

    bool waitForTotal(std::size_t total, std::chrono::milliseconds timeout) {
        std::unique_lock lock{mutex_};
        return signal_.wait_for(lock, timeout, [&] { return results_.size() + events_.size() >= total; });
    }

    // Waits for a specific number of result frames (sendToConnection),
    // independent of how many event frames are broadcast. This is robust
    // against new event types being added without changing test counts.
    bool waitForResults(std::size_t count, std::chrono::milliseconds timeout) {
        std::unique_lock lock{mutex_};
        return signal_.wait_for(lock, timeout, [&] { return results_.size() >= count; });
    }

    // Waits for a specific number of event frames (broadcastEvent).
    bool waitForEvents(std::size_t count, std::chrono::milliseconds timeout) {
        std::unique_lock lock{mutex_};
        return signal_.wait_for(lock, timeout, [&] { return events_.size() >= count; });
    }

    // Waits for event delivery to settle: returns true when no new frames
    // arrive for the given quiet period. Used after waitForResults.
    bool waitForQuiet(std::chrono::milliseconds quietPeriod, std::chrono::milliseconds timeout) {
        std::unique_lock lock{mutex_};
        auto deadline = std::chrono::steady_clock::now() + timeout;
        auto lastCount = results_.size() + events_.size();
        while (std::chrono::steady_clock::now() < deadline) {
            if (signal_.wait_for(lock, quietPeriod) == std::cv_status::timeout) {
                return results_.size() + events_.size() == lastCount;
            }
            lastCount = results_.size() + events_.size();
        }
        return false;
    }

    [[nodiscard]] std::vector<ProtocolFrame> results() const {
        std::lock_guard lock{mutex_};
        return {results_.begin(), results_.end()};
    }

    [[nodiscard]] std::vector<ProtocolFrame> events() const {
        std::lock_guard lock{mutex_};
        return {events_.begin(), events_.end()};
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable signal_;
    std::deque<ProtocolFrame> results_;
    std::deque<ProtocolFrame> events_;
};

ProtocolFrame createCommandFrame(const std::string& requestId, const std::filesystem::path& parent) {
    ProtocolFrame frame;
    frame.set_request_id(requestId);
    auto* command = frame.mutable_command();
    auto* create = command->mutable_create_project();
    create->set_display_name("Processor Test");
    create->set_parent_directory(infraforge::runtime::utf8String(parent));
    auto* georeference = create->mutable_georeference();
    georeference->set_horizontal_crs("EPSG:32633");
    georeference->set_linear_unit("metre");
    georeference->set_axis_convention(infraforge::protocol::v1::AXIS_CONVENTION_EASTING_NORTHING_UP);
    georeference->set_origin_easting(500000.0);
    georeference->set_origin_northing(4649776.0);
    create->set_traffic_side(infraforge::protocol::v1::TRAFFIC_SIDE_RIGHT);
    return frame;
}

ProtocolFrame openCommandFrame(const std::string& requestId, const std::filesystem::path& directory) {
    ProtocolFrame frame;
    frame.set_request_id(requestId);
    frame.mutable_command()->mutable_open_project()->set_project_directory(
        infraforge::runtime::utf8String(directory));
    return frame;
}

template <typename Filler>
ProtocolFrame simpleCommandFrame(const std::string& requestId, Filler filler) {
    ProtocolFrame frame;
    frame.set_request_id(requestId);
    filler(frame.mutable_command());
    return frame;
}

constexpr auto kWaitTimeout = std::chrono::seconds{10};

// Filters broadcast event frames by event case. Job and diagnostic events
// are now emitted alongside project events, so tests that assert specific
// project event sequences must filter rather than count total events.
std::vector<ProtocolFrame> projectEvents(const std::vector<ProtocolFrame>& events) {
    std::vector<ProtocolFrame> result;
    for (const auto& frame : events) {
        if (!frame.has_event()) continue;
        const auto& ev = frame.event();
        if (ev.has_project_opened() || ev.has_project_closed()
            || ev.has_project_revision_changed() || ev.has_project_dirty_state_changed()
            || ev.has_georeference_changed()) {
            result.push_back(frame);
        }
    }
    return result;
}

std::vector<ProtocolFrame> jobEvents(const std::vector<ProtocolFrame>& events) {
    std::vector<ProtocolFrame> result;
    for (const auto& frame : events) {
        if (!frame.has_event()) continue;
        const auto& ev = frame.event();
        if (ev.has_job_queued() || ev.has_job_started() || ev.has_job_progress()
            || ev.has_job_completed() || ev.has_job_failed() || ev.has_job_cancelled()) {
            result.push_back(frame);
        }
    }
    return result;
}

std::vector<ProtocolFrame> diagnosticEvents(const std::vector<ProtocolFrame>& events) {
    std::vector<ProtocolFrame> result;
    for (const auto& frame : events) {
        if (!frame.has_event()) continue;
        const auto& ev = frame.event();
        if (ev.has_diagnostic_added() || ev.has_diagnostic_removed() || ev.has_diagnostic_cleared()) {
            result.push_back(frame);
        }
    }
    return result;
}

} // namespace

TEST_SUITE("command processor") {
    TEST_CASE("create command produces a correlated state result and an opened event") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        RecordingSink sink;
        infraforge::domain::geo::GeoTransformService transforms;
        infraforge::application::CommandProcessor processor(store, transforms, sink);
        processor.start();

        processor.post("conn-1", createCommandFrame("req-create", scratch.path()));
        REQUIRE(sink.waitForResults(1, kWaitTimeout));

        const auto results = sink.results();
        REQUIRE(results.size() == 1);
        CHECK_EQ(results[0].request_id(), "req-create");
        REQUIRE(results[0].has_result());
        REQUIRE(results[0].result().has_project_state());
        const auto& summary = results[0].result().project_state().summary();
        CHECK_FALSE(summary.project_uuid().empty());
        CHECK_EQ(summary.display_name(), "Processor Test");
        CHECK_EQ(summary.revision(), 1);
        CHECK_FALSE(summary.dirty());
        CHECK_EQ(summary.georeference().horizontal_crs(), "EPSG:32633");
        CHECK_EQ(summary.traffic_side(), infraforge::protocol::v1::TRAFFIC_SIDE_RIGHT);

        const auto events = sink.events();
        const auto pEvents = projectEvents(events);
        REQUIRE(pEvents.size() == 1);
        REQUIRE(pEvents[0].has_event());
        REQUIRE(pEvents[0].event().has_project_opened());
        CHECK_FALSE(pEvents[0].event().event_id().empty());
        CHECK_EQ(pEvents[0].event().project_opened().summary().project_uuid(), summary.project_uuid());

        processor.shutdown();
        CHECK(store.isOpen());
        store.close();
    }

    TEST_CASE("already-open create fails with a typed error and the current revision") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        RecordingSink sink;
        infraforge::domain::geo::GeoTransformService transforms;
        infraforge::application::CommandProcessor processor(store, transforms, sink);
        processor.start();

        processor.post("conn-1", createCommandFrame("req-first", scratch.path()));
        REQUIRE(sink.waitForResults(1, kWaitTimeout));

        processor.post("conn-1", createCommandFrame("req-second", scratch.path()));
        REQUIRE(sink.waitForResults(2, kWaitTimeout));

        const auto results = sink.results();
        REQUIRE(results.size() == 2);
        CHECK_EQ(results[1].request_id(), "req-second");
        REQUIRE(results[1].result().has_error());
        CHECK_EQ(results[1].result().error().code(),
            infraforge::protocol::v1::COMMAND_ERROR_CODE_PROJECT_ALREADY_OPEN);
        CHECK(results[1].result().error().has_current_revision());

        processor.shutdown();
        store.close();
    }

    TEST_CASE("open with a non-project directory fails with PROJECT_DIRECTORY_INVALID") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        RecordingSink sink;
        infraforge::domain::geo::GeoTransformService transforms;
        infraforge::application::CommandProcessor processor(store, transforms, sink);
        processor.start();

        processor.post("conn-1", openCommandFrame("req-open", scratch.path() / "missing"));
        REQUIRE(sink.waitForResults(1, kWaitTimeout));

        const auto results = sink.results();
        REQUIRE(results.size() == 1);
        CHECK_EQ(results[0].request_id(), "req-open");
        REQUIRE(results[0].result().has_error());
        CHECK_EQ(results[0].result().error().code(),
            infraforge::protocol::v1::COMMAND_ERROR_CODE_PROJECT_DIRECTORY_INVALID);
        CHECK(projectEvents(sink.events()).empty());

        processor.shutdown();
    }

    TEST_CASE("save, close and get_summary round-trip through the processor") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        RecordingSink sink;
        infraforge::domain::geo::GeoTransformService transforms;
        infraforge::application::CommandProcessor processor(store, transforms, sink);
        processor.start();

        processor.post("conn-1", createCommandFrame("req-create", scratch.path()));
        REQUIRE(sink.waitForResults(1, kWaitTimeout));

        processor.post("conn-1", simpleCommandFrame(
            "req-summary", [](auto* envelope) { envelope->mutable_get_project_summary(); }));
        REQUIRE(sink.waitForResults(2, kWaitTimeout));

        processor.post("conn-1", simpleCommandFrame(
            "req-save", [](auto* envelope) { envelope->mutable_save_project(); }));
        REQUIRE(sink.waitForResults(3, kWaitTimeout));

        processor.post("conn-1", simpleCommandFrame(
            "req-close", [](auto* envelope) { envelope->mutable_close_project(); }));
        REQUIRE(sink.waitForTotal(20, kWaitTimeout));

        const auto results = sink.results();
        REQUIRE(results.size() == 4);
        CHECK_EQ(results[1].request_id(), "req-summary");
        CHECK(results[1].result().has_project_state());
        CHECK_EQ(results[2].request_id(), "req-save");
        CHECK(results[2].result().has_project_state());
        CHECK_EQ(results[3].request_id(), "req-close");
        REQUIRE(results[3].result().has_project_closed());
        CHECK_FALSE(results[3].result().project_closed().project_uuid().empty());

        const auto events = sink.events();
        const auto pEvents = projectEvents(events);
        REQUIRE(pEvents.size() == 2);
        CHECK(pEvents[0].event().has_project_opened());
        CHECK(pEvents[1].event().has_project_closed());

        // The project summary command now reports PROJECT_NOT_OPEN.
        processor.post("conn-1", simpleCommandFrame(
            "req-after-close", [](auto* envelope) { envelope->mutable_get_project_summary(); }));
        REQUIRE(sink.waitForResults(5, kWaitTimeout));
        const auto afterClose = sink.results();
        REQUIRE(afterClose.size() == 5);
        CHECK_EQ(afterClose[4].request_id(), "req-after-close");
        REQUIRE(afterClose[4].result().has_error());
        CHECK_EQ(afterClose[4].result().error().code(),
            infraforge::protocol::v1::COMMAND_ERROR_CODE_PROJECT_NOT_OPEN);

        processor.shutdown();
    }

    TEST_CASE("an empty command envelope yields an invalid-argument error result") {
        infraforge::persistence::SqliteProjectStore store;
        RecordingSink sink;
        infraforge::domain::geo::GeoTransformService transforms;
        infraforge::application::CommandProcessor processor(store, transforms, sink);
        processor.start();

        ProtocolFrame frame;
        frame.set_request_id("req-empty");
        frame.mutable_command();
        processor.post("conn-1", frame);
        REQUIRE(sink.waitForResults(1, kWaitTimeout));

        const auto results = sink.results();
        REQUIRE(results.size() == 1);
        REQUIRE(results[0].result().has_error());
        CHECK_EQ(results[0].result().error().code(),
            infraforge::protocol::v1::COMMAND_ERROR_CODE_INVALID_ARGUMENT);

        processor.shutdown();
    }
    TEST_CASE("a corrupt project database surfaces as COMMAND_ERROR_CODE_PERSISTENCE_FAILURE") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        RecordingSink sink;
        infraforge::domain::geo::GeoTransformService transforms;
        infraforge::application::CommandProcessor processor(store, transforms, sink);
        processor.start();

        processor.post("conn-1", createCommandFrame("req-create", scratch.path()));
        REQUIRE(sink.waitForResults(1, kWaitTimeout));

        processor.post("conn-1", simpleCommandFrame(
            "req-close", [](auto* envelope) { envelope->mutable_close_project(); }));
        REQUIRE(sink.waitForResults(2, kWaitTimeout));

        const auto results = sink.results();
        REQUIRE(results.size() == 2);
        REQUIRE(results[0].result().has_project_state());
        const std::filesystem::path projectDirectory =
            infraforge::runtime::pathFromUtf8(results[0].result().project_state().summary().directory());

        {
            std::ofstream database(projectDirectory / "project.db", std::ios::binary | std::ios::trunc);
            database << "this file is definitely not a SQLite database";
        }

        processor.post("conn-1", openCommandFrame("req-open-corrupt", projectDirectory));
        REQUIRE(sink.waitForResults(3, kWaitTimeout));

        const auto after = sink.results();
        REQUIRE(after.size() == 3);
        REQUIRE(after[2].result().has_error());
        CHECK_EQ(after[2].result().error().code(),
            infraforge::protocol::v1::COMMAND_ERROR_CODE_PERSISTENCE_FAILURE);

        processor.shutdown();
    }

    TEST_CASE("geo commands query, mutate, and transform through the canonical path") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        RecordingSink sink;
        infraforge::domain::geo::GeoTransformService transforms;
        infraforge::application::CommandProcessor processor(store, transforms, sink);
        processor.start();

        processor.post("conn-1", createCommandFrame("req-geo-create", scratch.path()));
        REQUIRE(sink.waitForResults(1, kWaitTimeout));

        processor.post("conn-1", simpleCommandFrame(
            "req-geo-get", [](auto* envelope) { envelope->mutable_get_georeference(); }));
        REQUIRE(sink.waitForResults(2, kWaitTimeout));

        processor.post("conn-1", simpleCommandFrame(
            "req-geo-transform",
            [](auto* envelope) {
                auto* command = envelope->mutable_transform_to_project_global();
                command->set_source_crs("EPSG:4326");
                auto* coordinate = command->add_coordinates();
                coordinate->set_x(15.0);
                coordinate->set_y(55.0);
                coordinate->set_z(3.0);
            }));
        REQUIRE(sink.waitForResults(3, kWaitTimeout));

        processor.post("conn-1", simpleCommandFrame(
            "req-geo-set",
            [](auto* envelope) {
                auto* command = envelope->mutable_set_georeference();
                auto* georeference = command->mutable_georeference();
                georeference->set_horizontal_crs("EPSG:32632");
                georeference->set_linear_unit("metre");
                georeference->set_axis_convention(
                    infraforge::protocol::v1::AXIS_CONVENTION_EASTING_NORTHING_UP);
                georeference->set_origin_easting(300000.0);
                georeference->set_origin_northing(5500000.0);
                georeference->set_origin_height(10.0);
                command->set_expected_revision(1);
            }));
        // Four results (create, get, transform, set) plus four events
        // (opened, georeference_changed, revision, dirty).
        REQUIRE(sink.waitForTotal(22, kWaitTimeout));

        const auto results = sink.results();
        REQUIRE(results.size() == 4);

        REQUIRE(results[1].result().has_georeference_state());
        const auto& info = results[1].result().georeference_state().georeference();
        CHECK_EQ(info.config().horizontal_crs(), "EPSG:32633");
        CHECK_EQ(info.horizontal_crs().identifier(), "EPSG:32633");
        CHECK_EQ(info.horizontal_crs().kind(), "PROJECTED_CRS");
        CHECK(info.horizontal_crs().axis_unit_to_metre() == doctest::Approx(1.0));
        CHECK_FALSE(info.vertical_reference().present());
        CHECK_EQ(results[1].result().georeference_state().revision(), 1);

        REQUIRE(results[2].result().has_transform_to_project_global());
        const auto& positions = results[2].result().transform_to_project_global();
        REQUIRE(positions.coordinates_size() == 1);
        CHECK(std::abs(positions.coordinates(0).easting() - 500000.0) < 0.001);
        CHECK(std::abs(positions.coordinates(0).northing() - 6094791.42) < 0.01);
        CHECK(positions.coordinates(0).height() == doctest::Approx(3.0));

        REQUIRE(results[3].result().has_georeference_state());
        const auto& updated = results[3].result().georeference_state().georeference();
        CHECK_EQ(updated.config().horizontal_crs(), "EPSG:32632");
        CHECK_EQ(updated.config().origin_height(), doctest::Approx(10.0));
        CHECK_EQ(results[3].result().georeference_state().revision(), 2);

        const auto events = sink.events();
        const auto pEvents = projectEvents(events);
        REQUIRE(pEvents.size() == 4);
        CHECK(pEvents[0].event().has_project_opened());
        REQUIRE(pEvents[1].event().has_georeference_changed());
        CHECK_EQ(pEvents[1].event().georeference_changed().georeference().config().horizontal_crs(),
            "EPSG:32632");
        CHECK_EQ(pEvents[1].event().georeference_changed().revision(), 2);
        CHECK(pEvents[2].event().has_project_revision_changed());
        CHECK(pEvents[3].event().has_project_dirty_state_changed());
        CHECK(pEvents[3].event().project_dirty_state_changed().dirty());

        processor.shutdown();
        store.close();
    }

    TEST_CASE("unsupported and invalid georeference input maps to typed error codes") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        RecordingSink sink;
        infraforge::domain::geo::GeoTransformService transforms;
        infraforge::application::CommandProcessor processor(store, transforms, sink);
        processor.start();

        processor.post("conn-1", createCommandFrame("req-create", scratch.path()));
        REQUIRE(sink.waitForResults(1, kWaitTimeout));

        const auto setGeoreferenceFrame = [](const std::string& requestId, const std::string& crs) {
            return simpleCommandFrame(requestId,
                [&crs](auto* envelope) {
                    auto* command = envelope->mutable_set_georeference();
                    auto* georeference = command->mutable_georeference();
                    georeference->set_horizontal_crs(crs);
                    georeference->set_linear_unit("metre");
                    georeference->set_axis_convention(
                        infraforge::protocol::v1::AXIS_CONVENTION_EASTING_NORTHING_UP);
                });
        };

        processor.post("conn-1", setGeoreferenceFrame("req-geo-unsupported", "EPSG:4326"));
        REQUIRE(sink.waitForResults(2, kWaitTimeout));

        processor.post("conn-1", setGeoreferenceFrame("req-geo-garbage", "junk"));
        REQUIRE(sink.waitForResults(3, kWaitTimeout));

        processor.post("conn-1", simpleCommandFrame(
            "req-geo-convention",
            [](auto* envelope) {
                auto* command = envelope->mutable_set_georeference();
                command->mutable_georeference()->set_horizontal_crs("EPSG:32633");
                command->mutable_georeference()->set_linear_unit("metre");
                command->mutable_georeference()->set_axis_convention(
                    infraforge::protocol::v1::AXIS_CONVENTION_UNSPECIFIED);
            }));
        REQUIRE(sink.waitForResults(4, kWaitTimeout));

        const auto results = sink.results();
        REQUIRE(results.size() == 4);
        REQUIRE(results[1].result().has_error());
        CHECK_EQ(results[1].result().error().code(),
            infraforge::protocol::v1::COMMAND_ERROR_CODE_GEO_UNSUPPORTED);
        REQUIRE(results[2].result().has_error());
        CHECK_EQ(results[2].result().error().code(),
            infraforge::protocol::v1::COMMAND_ERROR_CODE_INVALID_ARGUMENT);
        REQUIRE(results[3].result().has_error());
        CHECK_EQ(results[3].result().error().code(),
            infraforge::protocol::v1::COMMAND_ERROR_CODE_INVALID_ARGUMENT);

        REQUIRE(sink.waitForTotal(16, kWaitTimeout));
        CHECK_EQ(projectEvents(sink.events()).size(), 1);

        processor.shutdown();
        store.close();
    }
    TEST_CASE("job lifecycle events are emitted for each command") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        RecordingSink sink;
        infraforge::domain::geo::GeoTransformService transforms;
        infraforge::application::CommandProcessor processor(store, transforms, sink);
        processor.start();

        processor.post("conn-1", createCommandFrame("req-create", scratch.path()));
        REQUIRE(sink.waitForTotal(6, kWaitTimeout));

        const auto events = sink.events();
        const auto jobs = jobEvents(events);
        REQUIRE(jobs.size() == 3);
        REQUIRE(jobs[0].event().has_job_queued());
        CHECK_FALSE(jobs[0].event().job_queued().job_id().empty());
        CHECK_EQ(jobs[0].event().job_queued().operation(), "project.create");
        CHECK_FALSE(jobs[0].event().job_queued().cancellable());
        REQUIRE(jobs[1].event().has_job_started());
        CHECK_EQ(jobs[1].event().job_started().job_id(), jobs[0].event().job_queued().job_id());
        REQUIRE(jobs[2].event().has_job_completed());
        CHECK_EQ(jobs[2].event().job_completed().job_id(), jobs[0].event().job_queued().job_id());

        processor.shutdown();
        store.close();
    }

    TEST_CASE("job failed event carries structured error on command failure") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        RecordingSink sink;
        infraforge::domain::geo::GeoTransformService transforms;
        infraforge::application::CommandProcessor processor(store, transforms, sink);
        processor.start();

        // Create a project, then try to create another (fails: already open).
        processor.post("conn-1", createCommandFrame("req-first", scratch.path()));
        REQUIRE(sink.waitForTotal(6, kWaitTimeout));

        processor.post("conn-1", createCommandFrame("req-second", scratch.path()));
        REQUIRE(sink.waitForTotal(10, kWaitTimeout));

        // The second command should have job.queued, job.started, job.failed.
        const auto jobs = jobEvents(sink.events());
        // First command: queued + started + completed = 3
        // Second command: queued + started + failed = 3
        // First command: queued + started + completed = 3
        // Second command: queued + started + failed = 3
        REQUIRE(jobs.size() == 6);
        // First 3 events are from the first (successful) command
        REQUIRE(jobs[0].event().has_job_queued());
        REQUIRE(jobs[1].event().has_job_started());
        REQUIRE(jobs[2].event().has_job_completed());
        // Last 3 events are from the second (failed) command
        REQUIRE(jobs[3].event().has_job_queued());
        REQUIRE(jobs[4].event().has_job_started());
        REQUIRE(jobs[5].event().has_job_failed());
        CHECK_EQ(jobs[5].event().job_failed().error_code(), "PROJECT_ALREADY_OPEN");
        CHECK_FALSE(jobs[5].event().job_failed().error_message().empty());

        processor.shutdown();
        store.close();
    }

    TEST_CASE("diagnostic added event is emitted when no vertical reference is configured") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        RecordingSink sink;
        infraforge::domain::geo::GeoTransformService transforms;
        infraforge::application::CommandProcessor processor(store, transforms, sink);
        processor.start();

        // Create a project without a vertical CRS.
        processor.post("conn-1", createCommandFrame("req-create", scratch.path()));
        REQUIRE(sink.waitForTotal(6, kWaitTimeout));

        const auto diags = diagnosticEvents(sink.events());
        REQUIRE(diags.size() == 1);
        REQUIRE(diags[0].event().has_diagnostic_added());
        CHECK_FALSE(diags[0].event().diagnostic_added().diagnostic_id().empty());
        CHECK_EQ(diags[0].event().diagnostic_added().source(), "georeference");
        CHECK_EQ(diags[0].event().diagnostic_added().severity(),
            infraforge::protocol::v1::DIAGNOSTIC_SEVERITY_WARNING);
        CHECK_FALSE(diags[0].event().diagnostic_added().message().empty());

        processor.shutdown();
        store.close();
    }

    TEST_CASE("diagnostic cleared event is emitted when project closes") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        RecordingSink sink;
        infraforge::domain::geo::GeoTransformService transforms;
        infraforge::application::CommandProcessor processor(store, transforms, sink);
        processor.start();

        processor.post("conn-1", createCommandFrame("req-create", scratch.path()));
        REQUIRE(sink.waitForResults(1, kWaitTimeout));

        processor.post("conn-1", simpleCommandFrame(
            "req-close", [](auto* envelope) { envelope->mutable_close_project(); }));
        REQUIRE(sink.waitForTotal(12, kWaitTimeout));

        const auto diags = diagnosticEvents(sink.events());
        // Create without vertical CRS: diagnostic.added
        // Close: diagnostic.cleared
        REQUIRE(diags.size() == 2);
        REQUIRE(diags[0].event().has_diagnostic_added());
        REQUIRE(diags[1].event().has_diagnostic_cleared());
        CHECK_EQ(diags[1].event().diagnostic_cleared().source(), "georeference");

        processor.shutdown();
    }

    TEST_CASE("diagnostic removed event is emitted when vertical CRS is added via set_georeference") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        RecordingSink sink;
        infraforge::domain::geo::GeoTransformService transforms;
        infraforge::application::CommandProcessor processor(store, transforms, sink);
        processor.start();

        // Create without vertical CRS -> diagnostic.added
        processor.post("conn-1", createCommandFrame("req-create", scratch.path()));
        REQUIRE(sink.waitForResults(1, kWaitTimeout));

        // Set georeference with a vertical CRS -> diagnostic.removed
        processor.post("conn-1", simpleCommandFrame(
            "req-geo-set",
            [](auto* envelope) {
                auto* command = envelope->mutable_set_georeference();
                auto* georef = command->mutable_georeference();
                georef->set_horizontal_crs("EPSG:32633");
                georef->set_linear_unit("metre");
                georef->set_axis_convention(
                    infraforge::protocol::v1::AXIS_CONVENTION_EASTING_NORTHING_UP);
                georef->set_origin_easting(500000.0);
                georef->set_origin_northing(4649776.0);
                georef->set_vertical_crs("EPSG:3855");
                command->set_expected_revision(1);
            }));
        REQUIRE(sink.waitForTotal(14, kWaitTimeout));

        const auto diags = diagnosticEvents(sink.events());
        // Create without vertical CRS: diagnostic.added
        // Set georeference with vertical CRS: diagnostic.removed
        REQUIRE(diags.size() == 2);
        REQUIRE(diags[0].event().has_diagnostic_added());
        REQUIRE(diags[1].event().has_diagnostic_removed());
        CHECK_EQ(diags[1].event().diagnostic_removed().diagnostic_id(),
            diags[0].event().diagnostic_added().diagnostic_id());

        processor.shutdown();
        store.close();
    }
    // -----------------------------------------------------------------------
    // Validation-failure lifecycle tests (BLOCKER 1 fix):
    // Every command that emits job.queued must reach exactly one terminal
    // event, even when pre-execution validation fails. The lifecycle is:
    // queued -> started -> failed (with stable error code).
    // -----------------------------------------------------------------------

    TEST_CASE("invalid create command (bad traffic_side) reaches terminal failed") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        RecordingSink sink;
        infraforge::domain::geo::GeoTransformService transforms;
        infraforge::application::CommandProcessor processor(store, transforms, sink);
        processor.start();

        ProtocolFrame frame;
        frame.set_request_id("req-bad-traffic");
        auto* create = frame.mutable_command()->mutable_create_project();
        create->set_display_name("Bad Traffic");
        create->set_parent_directory(infraforge::runtime::utf8String(scratch.path()));
        auto* georef = create->mutable_georeference();
        georef->set_horizontal_crs("EPSG:32633");
        georef->set_linear_unit("metre");
        georef->set_axis_convention(infraforge::protocol::v1::AXIS_CONVENTION_EASTING_NORTHING_UP);
        georef->set_origin_easting(500000.0);
        georef->set_origin_northing(4649776.0);
        // traffic_side left as TRAFFIC_SIDE_UNSPECIFIED (0) = invalid

        processor.post("conn-1", std::move(frame));
        // 1 result + 3 job events (queued, started, failed) = 4 total
        REQUIRE(sink.waitForTotal(4, kWaitTimeout));

        const auto jobs = jobEvents(sink.events());
        REQUIRE(jobs.size() == 3);
        REQUIRE(jobs[0].event().has_job_queued());
        REQUIRE(jobs[1].event().has_job_started());
        REQUIRE(jobs[2].event().has_job_failed());
        CHECK_EQ(jobs[1].event().job_started().job_id(), jobs[0].event().job_queued().job_id());
        CHECK_EQ(jobs[2].event().job_failed().job_id(), jobs[0].event().job_queued().job_id());
        CHECK_EQ(jobs[2].event().job_failed().error_code(), "INVALID_ARGUMENT");
        for (const auto& j : jobs) {
            CHECK_FALSE(j.event().has_job_completed());
        }

        processor.shutdown();
        store.close();
    }

    TEST_CASE("invalid create command (bad axis_convention) reaches terminal failed") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        RecordingSink sink;
        infraforge::domain::geo::GeoTransformService transforms;
        infraforge::application::CommandProcessor processor(store, transforms, sink);
        processor.start();

        ProtocolFrame frame;
        frame.set_request_id("req-bad-axis");
        auto* create = frame.mutable_command()->mutable_create_project();
        create->set_display_name("Bad Axis");
        create->set_parent_directory(infraforge::runtime::utf8String(scratch.path()));
        auto* georef = create->mutable_georeference();
        georef->set_horizontal_crs("EPSG:32633");
        georef->set_linear_unit("metre");
        // axis_convention left as AXIS_CONVENTION_UNSPECIFIED (0) = invalid
        georef->set_origin_easting(500000.0);
        georef->set_origin_northing(4649776.0);
        create->set_traffic_side(infraforge::protocol::v1::TRAFFIC_SIDE_RIGHT);

        processor.post("conn-1", std::move(frame));
        REQUIRE(sink.waitForTotal(4, kWaitTimeout));

        const auto jobs = jobEvents(sink.events());
        REQUIRE(jobs.size() == 3);
        REQUIRE(jobs[0].event().has_job_queued());
        REQUIRE(jobs[1].event().has_job_started());
        REQUIRE(jobs[2].event().has_job_failed());
        CHECK_EQ(jobs[2].event().job_failed().error_code(), "INVALID_ARGUMENT");
        for (const auto& j : jobs) {
            CHECK_FALSE(j.event().has_job_completed());
        }

        processor.shutdown();
        store.close();
    }

    TEST_CASE("invalid set_georeference (bad axis_convention) reaches terminal failed") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        RecordingSink sink;
        infraforge::domain::geo::GeoTransformService transforms;
        infraforge::application::CommandProcessor processor(store, transforms, sink);
        processor.start();

        // First create a project so set_georeference can be attempted.
        processor.post("conn-1", createCommandFrame("req-create", scratch.path()));
        REQUIRE(sink.waitForTotal(6, kWaitTimeout));

        // Now send set_georeference with invalid axis_convention.
        ProtocolFrame frame;
        frame.set_request_id("req-bad-geo");
        auto* setGeo = frame.mutable_command()->mutable_set_georeference();
        auto* georef = setGeo->mutable_georeference();
        georef->set_horizontal_crs("EPSG:32633");
        georef->set_linear_unit("metre");
        // axis_convention left as UNSPECIFIED = invalid
        georef->set_origin_easting(500000.0);
        georef->set_origin_northing(4649776.0);

        processor.post("conn-1", std::move(frame));
        // 1 result + 3 job events = 4 total for the failed command
        REQUIRE(sink.waitForTotal(10, kWaitTimeout));

        const auto jobs = jobEvents(sink.events());
        // First command: 3 job events (queued, started, completed)
        // Second command: 3 job events (queued, started, failed)
        REQUIRE(jobs.size() == 6);
        REQUIRE(jobs[3].event().has_job_queued());
        REQUIRE(jobs[4].event().has_job_started());
        REQUIRE(jobs[5].event().has_job_failed());
        CHECK_EQ(jobs[5].event().job_failed().error_code(), "INVALID_ARGUMENT");
        CHECK_FALSE(jobs[3].event().has_job_completed());
        CHECK_FALSE(jobs[4].event().has_job_completed());
        CHECK_FALSE(jobs[5].event().has_job_completed());

        processor.shutdown();
        store.close();
    }

    TEST_CASE("oversized transform batch reaches terminal failed") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        RecordingSink sink;
        infraforge::domain::geo::GeoTransformService transforms;
        infraforge::application::CommandProcessor processor(store, transforms, sink);
        processor.start();

        // Create a project first.
        processor.post("conn-1", createCommandFrame("req-create", scratch.path()));
        REQUIRE(sink.waitForTotal(6, kWaitTimeout));

        // Send a transform with too many coordinates.
        ProtocolFrame frame;
        frame.set_request_id("req-oversized");
        auto* transform = frame.mutable_command()->mutable_transform_to_project_global();
        transform->set_source_crs("EPSG:4326");
        for (int i = 0; i <= static_cast<int>(infraforge::application::kMaxTransformCoordinates); ++i) {
            auto* coord = transform->add_coordinates();
            coord->set_x(0.0);
            coord->set_y(0.0);
            coord->set_z(0.0);
        }

        processor.post("conn-1", std::move(frame));
        REQUIRE(sink.waitForTotal(10, kWaitTimeout));

        const auto jobs = jobEvents(sink.events());
        REQUIRE(jobs.size() == 6);
        REQUIRE(jobs[3].event().has_job_queued());
        REQUIRE(jobs[4].event().has_job_started());
        REQUIRE(jobs[5].event().has_job_failed());
        CHECK_EQ(jobs[5].event().job_failed().error_code(), "INVALID_ARGUMENT");
        CHECK_FALSE(jobs[5].event().has_job_completed());

        processor.shutdown();
        store.close();
    }

    TEST_CASE("empty command envelope (COMMAND_NOT_SET) reaches terminal failed") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        RecordingSink sink;
        infraforge::domain::geo::GeoTransformService transforms;
        infraforge::application::CommandProcessor processor(store, transforms, sink);
        processor.start();

        ProtocolFrame frame;
        frame.set_request_id("req-empty");
        // command() is not set -> COMMAND_NOT_SET

        processor.post("conn-1", std::move(frame));
        REQUIRE(sink.waitForTotal(4, kWaitTimeout));

        const auto jobs = jobEvents(sink.events());
        REQUIRE(jobs.size() == 3);
        REQUIRE(jobs[0].event().has_job_queued());
        CHECK_EQ(jobs[0].event().job_queued().operation(), "unknown");
        REQUIRE(jobs[1].event().has_job_started());
        REQUIRE(jobs[2].event().has_job_failed());
        CHECK_EQ(jobs[2].event().job_failed().error_code(), "INVALID_ARGUMENT");
        CHECK_FALSE(jobs[2].event().has_job_completed());

        processor.shutdown();
        store.close();
    }

    TEST_CASE("exactly one terminal event per queued job (no double terminal)") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        RecordingSink sink;
        infraforge::domain::geo::GeoTransformService transforms;
        infraforge::application::CommandProcessor processor(store, transforms, sink);
        processor.start();

        processor.post("conn-1", createCommandFrame("req-ok", scratch.path()));
        REQUIRE(sink.waitForTotal(6, kWaitTimeout));

        const auto jobs = jobEvents(sink.events());
        REQUIRE(jobs.size() == 3);
        int completedCount = 0;
        int failedCount = 0;
        for (const auto& j : jobs) {
            if (j.event().has_job_completed()) ++completedCount;
            if (j.event().has_job_failed()) ++failedCount;
        }
        CHECK_EQ(completedCount, 1);
        CHECK_EQ(failedCount, 0);

        processor.shutdown();
        store.close();
    }

    TEST_CASE("multiple queued commands execute in order with correct lifecycles") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        RecordingSink sink;
        infraforge::domain::geo::GeoTransformService transforms;
        infraforge::application::CommandProcessor processor(store, transforms, sink);
        processor.start();

        processor.post("conn-1", createCommandFrame("req-1", scratch.path()));
        processor.post("conn-1", createCommandFrame("req-2", scratch.path()));

        // First: 1 result + 5 events = 6
        // Second: 1 result + 3 events (queued, started, failed) = 4
        // Total: 2 results + 8 events = 10
        REQUIRE(sink.waitForTotal(10, kWaitTimeout));

        const auto jobs = jobEvents(sink.events());
        REQUIRE(jobs.size() == 6);
        REQUIRE(jobs[0].event().has_job_queued());
        REQUIRE(jobs[1].event().has_job_started());
        REQUIRE(jobs[2].event().has_job_completed());
        REQUIRE(jobs[3].event().has_job_queued());
        REQUIRE(jobs[4].event().has_job_started());
        REQUIRE(jobs[5].event().has_job_failed());
        CHECK(jobs[0].event().job_queued().job_id() != jobs[3].event().job_queued().job_id());

        processor.shutdown();
        store.close();
    }

    TEST_CASE("shutdown does not create orphan pending jobs") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        RecordingSink sink;
        infraforge::domain::geo::GeoTransformService transforms;
        infraforge::application::CommandProcessor processor(store, transforms, sink);
        processor.start();

        // Post a command but shut down before it can be processed.
        // Since post() is enqueue-only (no job.queued emitted), the
        // discarded command never enters the job lifecycle.
        processor.post("conn-1", createCommandFrame("req-dropped", scratch.path()));

        // Shut down immediately.
        processor.shutdown();

        const auto jobs = jobEvents(sink.events());
        // If the command was processed before shutdown, it has a full
        // lifecycle (3 events). If it was dropped, it has 0 events.
        // Either way, there are no orphan pending jobs (no job.queued
        // without a terminal event).
        for (const auto& j : jobs) {
            if (j.event().has_job_queued()) {
                const auto& queuedJobId = j.event().job_queued().job_id();
                bool hasTerminal = false;
                for (const auto& j2 : jobs) {
                    if ((j2.event().has_job_completed() && j2.event().job_completed().job_id() == queuedJobId)
                        || (j2.event().has_job_failed() && j2.event().job_failed().job_id() == queuedJobId)) {
                        hasTerminal = true;
                        break;
                    }
                }
                CHECK(hasTerminal);
            }
        }

        store.close();
    }


}
