#include "infraforge/network/CommandRouter.hpp"

#include "infraforge/runtime/Logging.hpp"

#include <infraforge/protocol/v1/foundation.pb.h>

#include <string_view>
#include <utility>
#include <vector>

namespace infraforge::network {

void WebSocketCommandRouter::registerConnection(const std::string& connectionId, std::weak_ptr<ix::WebSocket> socket) {
    std::lock_guard lock{mutex_};
    connections_.insert_or_assign(connectionId, std::move(socket));
}

void WebSocketCommandRouter::unregisterConnection(const std::string& connectionId) {
    std::lock_guard lock{mutex_};
    connections_.erase(connectionId);
}

void WebSocketCommandRouter::sendToConnection(std::string_view connectionId, const protocol::v1::Frame& frame) {
    std::weak_ptr<ix::WebSocket> target;
    {
        std::lock_guard lock{mutex_};
        if (const auto iterator = connections_.find(std::string{connectionId}); iterator != connections_.end()) {
            target = iterator->second;
        }
    }
    if (target.expired()) {
        runtime::logWarn("network", "frame.dropped_connection_gone", {{"connectionId", connectionId}});
        return;
    }
    deliver(target, frame);
}

void WebSocketCommandRouter::broadcastEvent(const protocol::v1::Frame& frame) {
    std::vector<std::weak_ptr<ix::WebSocket>> targets;
    {
        std::lock_guard lock{mutex_};
        targets.reserve(connections_.size());
        for (const auto& [id, socket] : connections_) {
            targets.push_back(socket);
        }
    }
    for (const auto& target : targets) {
        deliver(target, frame);
    }
}

void WebSocketCommandRouter::deliver(const std::weak_ptr<ix::WebSocket>& socket, const protocol::v1::Frame& frame) const {
    const auto shared = socket.lock();
    if (!shared) {
        return;
    }
    std::string bytes;
    if (!frame.SerializeToString(&bytes)) {
        runtime::logError("network", "frame.serialize_failed");
        return;
    }
    if (!shared->sendBinary(bytes).success) {
        runtime::logWarn("network", "frame.send_failed");
    }
}

} // namespace infraforge::network
