#pragma once

#include "infraforge/application/CommandProcessor.hpp"

#include <ixwebsocket/IXWebSocket.h>

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace infraforge::protocol::v1 {
class Frame;
}

namespace infraforge::network {

// Transport-side delivery of result/event frames to authenticated
// connections. Tracks live connections by id; sends from the application
// executor thread rely on ix::WebSocket::send being thread-safe (frames are
// queued internally).
class WebSocketCommandRouter final : public application::CommandSink {
public:
    // Called from the server accept path when a socket connects.
    void registerConnection(const std::string& connectionId, std::weak_ptr<ix::WebSocket> socket);
    // Called when the socket closes (any reason).
    void unregisterConnection(const std::string& connectionId);

    void sendToConnection(std::string_view connectionId, const protocol::v1::Frame& frame) override;
    void broadcastEvent(const protocol::v1::Frame& frame) override;

private:
    void deliver(const std::weak_ptr<ix::WebSocket>& socket, const protocol::v1::Frame& frame) const;

    std::mutex mutex_;
    std::map<std::string, std::weak_ptr<ix::WebSocket>> connections_;
};

} // namespace infraforge::network
