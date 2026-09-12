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

} // namespace

TEST_SUITE("command processor") {
    TEST_CASE("create command produces a correlated state result and an opened event") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        RecordingSink sink;
        infraforge::application::CommandProcessor processor(store, sink);
        processor.start();

        processor.post("conn-1", createCommandFrame("req-create", scratch.path()));
        REQUIRE(sink.waitForTotal(2, kWaitTimeout));

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
        REQUIRE(events.size() == 1);
        REQUIRE(events[0].has_event());
        REQUIRE(events[0].event().has_project_opened());
        CHECK_FALSE(events[0].event().event_id().empty());
        CHECK_EQ(events[0].event().project_opened().summary().project_uuid(), summary.project_uuid());

        processor.shutdown();
        CHECK(store.isOpen());
        store.close();
    }

    TEST_CASE("already-open create fails with a typed error and the current revision") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        RecordingSink sink;
        infraforge::application::CommandProcessor processor(store, sink);
        processor.start();

        processor.post("conn-1", createCommandFrame("req-first", scratch.path()));
        REQUIRE(sink.waitForTotal(2, kWaitTimeout));

        processor.post("conn-1", createCommandFrame("req-second", scratch.path()));
        REQUIRE(sink.waitForTotal(3, kWaitTimeout));

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
        infraforge::application::CommandProcessor processor(store, sink);
        processor.start();

        processor.post("conn-1", openCommandFrame("req-open", scratch.path() / "missing"));
        REQUIRE(sink.waitForTotal(1, kWaitTimeout));

        const auto results = sink.results();
        REQUIRE(results.size() == 1);
        CHECK_EQ(results[0].request_id(), "req-open");
        REQUIRE(results[0].result().has_error());
        CHECK_EQ(results[0].result().error().code(),
            infraforge::protocol::v1::COMMAND_ERROR_CODE_PROJECT_DIRECTORY_INVALID);
        CHECK(sink.events().empty());

        processor.shutdown();
    }

    TEST_CASE("save, close and get_summary round-trip through the processor") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        RecordingSink sink;
        infraforge::application::CommandProcessor processor(store, sink);
        processor.start();

        processor.post("conn-1", createCommandFrame("req-create", scratch.path()));
        REQUIRE(sink.waitForTotal(2, kWaitTimeout));

        processor.post("conn-1", simpleCommandFrame(
            "req-summary", [](auto* envelope) { envelope->mutable_get_project_summary(); }));
        REQUIRE(sink.waitForTotal(3, kWaitTimeout));

        processor.post("conn-1", simpleCommandFrame(
            "req-save", [](auto* envelope) { envelope->mutable_save_project(); }));
        REQUIRE(sink.waitForTotal(4, kWaitTimeout));

        processor.post("conn-1", simpleCommandFrame(
            "req-close", [](auto* envelope) { envelope->mutable_close_project(); }));
        REQUIRE(sink.waitForTotal(6, kWaitTimeout));

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
        REQUIRE(events.size() == 2);
        CHECK(events[0].event().has_project_opened());
        CHECK(events[1].event().has_project_closed());

        // The project summary command now reports PROJECT_NOT_OPEN.
        processor.post("conn-1", simpleCommandFrame(
            "req-after-close", [](auto* envelope) { envelope->mutable_get_project_summary(); }));
        REQUIRE(sink.waitForTotal(7, kWaitTimeout));
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
        infraforge::application::CommandProcessor processor(store, sink);
        processor.start();

        ProtocolFrame frame;
        frame.set_request_id("req-empty");
        frame.mutable_command();
        processor.post("conn-1", frame);
        REQUIRE(sink.waitForTotal(1, kWaitTimeout));

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
        infraforge::application::CommandProcessor processor(store, sink);
        processor.start();

        processor.post("conn-1", createCommandFrame("req-create", scratch.path()));
        REQUIRE(sink.waitForTotal(2, kWaitTimeout));

        processor.post("conn-1", simpleCommandFrame(
            "req-close", [](auto* envelope) { envelope->mutable_close_project(); }));
        REQUIRE(sink.waitForTotal(4, kWaitTimeout));

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
        REQUIRE(sink.waitForTotal(5, kWaitTimeout));

        const auto after = sink.results();
        REQUIRE(after.size() == 3);
        REQUIRE(after[2].result().has_error());
        CHECK_EQ(after[2].result().error().code(),
            infraforge::protocol::v1::COMMAND_ERROR_CODE_PERSISTENCE_FAILURE);

        processor.shutdown();
    }
}
