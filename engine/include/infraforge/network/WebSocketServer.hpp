#pragma once

#include <cstdint>
#include <string>

namespace infraforge::network {

struct ServerConfig {
    std::string host;
    std::uint16_t port;
    std::string session_token;
};

class WebSocketServer final {
public:
    explicit WebSocketServer(ServerConfig config);

    WebSocketServer(const WebSocketServer&) = delete;
    WebSocketServer& operator=(const WebSocketServer&) = delete;

    int run();

private:
    ServerConfig config_;
};

} // namespace infraforge::network
