#pragma once

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

    int run();

private:
    ServerConfig config_;
    application::CommandProcessor& processor_;
    WebSocketCommandRouter& router_;
};

} // namespace infraforge::network
