#pragma once

#include "infraforge/application/ProjectService.hpp"
#include "infraforge/ports/ProjectStore.hpp"

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace infraforge::protocol::v1 {
class Frame;
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
    CommandProcessor(ports::ProjectStore& store, CommandSink& sink);
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
        std::unique_ptr<protocol::v1::Frame> frame;
    };

    void runExecutor();
    void processCommand(const std::string& connectionId, const protocol::v1::Frame& frame);

    void handleCreateProject(const std::string& connectionId, const protocol::v1::Frame& frame);
    void handleOpenProject(const std::string& connectionId, const protocol::v1::Frame& frame);
    void handleSaveProjectAs(const std::string& connectionId, const protocol::v1::Frame& frame);

    // Executes one service use case, then emits the correlated result frame
    // (state or closed) and the derived event frames. Argument-validation
    // failures are handled by the handle* methods before reaching this.
    template <typename UseCase>
    void runServiceCommand(const std::string& connectionId, const protocol::v1::Frame& frame, UseCase&& useCase);
    void publishEvents(const ProjectCommandResult& result);

    void sendFailureResult(const std::string& connectionId, const std::string& requestId, const CommandFailure& failure);
    void sendInternalErrorResult(
        const std::string& connectionId,
        const std::string& requestId,
        std::string_view commandLabel);

    ports::ProjectStore& store_;
    ProjectService service_;
    CommandSink& sink_;

    std::thread executor_;
    std::mutex mutex_;
    std::condition_variable signal_;
    std::deque<PostedCommand> queue_;
    bool started_{false};
    bool stopped_{false};
};

} // namespace infraforge::application
