#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace infraforge::application {
class CommandProcessor;
}

namespace infraforge::network {

class WebSocketCommandRouter;

struct ServerConfig {
    std::string host;
    std::uint16_t port;
    std::string session_token;
};

class WebSocketServer final {
public:
    WebSocketServer(ServerConfig config, application::CommandProcessor& processor, WebSocketCommandRouter& router);

    WebSocketServer(const WebSocketServer&) = delete;
    WebSocketServer& operator=(const WebSocketServer&) = delete;

    // Blocks until requestStop() or a termination signal arrives.
    int run();

    // Cooperative stop for tests and supervised shutdown.
    void requestStop();

private:
    ServerConfig config_;
    application::CommandProcessor& processor_;
    WebSocketCommandRouter& router_;
    std::atomic_bool stopRequested_{false};
};

} // namespace infraforge::network
