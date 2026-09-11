#include "infraforge/network/WebSocketServer.hpp"
#include "infraforge/version.hpp"
#include "infraforge/protocol/v1/foundation.pb.h"

#include <ixwebsocket/IXConnectionState.h>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXWebSocketMessage.h>
#include <ixwebsocket/IXWebSocketServer.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace infraforge::network {
namespace {

using ProtocolFrame = infraforge::protocol::v1::Frame;
using ProtocolErrorCode = infraforge::protocol::v1::ErrorCode;

std::atomic_bool g_stop_requested{false};

void handle_termination_signal(int) {
    g_stop_requested.store(true, std::memory_order_relaxed);
}

class SessionConnectionState final : public ix::ConnectionState {
public:
    std::atomic_bool authenticated{false};
};

class NetworkSystemGuard final {
public:
    NetworkSystemGuard() { ix::initNetSystem(); }
    ~NetworkSystemGuard() { ix::uninitNetSystem(); }

    NetworkSystemGuard(const NetworkSystemGuard&) = delete;
    NetworkSystemGuard& operator=(const NetworkSystemGuard&) = delete;
};

bool constant_time_equal(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }

    unsigned char difference = 0;
    for (std::size_t index = 0; index < left.size(); ++index) {
        difference |= static_cast<unsigned char>(left[index]) ^ static_cast<unsigned char>(right[index]);
    }
    return difference == 0;
}

bool send_frame(ix::WebSocket& socket, const ProtocolFrame& frame) {
    std::string bytes;
    if (!frame.SerializeToString(&bytes)) {
        return false;
    }
    return socket.sendBinary(bytes).success;
}

void send_error(
    ix::WebSocket& socket,
    std::string_view request_id,
    ProtocolErrorCode code,
    std::string_view message) {
    ProtocolFrame response;
    response.set_request_id(std::string{request_id});
    auto* error = response.mutable_error();
    error->set_code(code);
    error->set_message(std::string{message});
    (void)send_frame(socket, response);
}

bool protocol_is_compatible(const infraforge::protocol::v1::ProtocolVersion& version) {
    return version.major() == infraforge::kProtocolMajor && version.minor() <= infraforge::kProtocolMinor;
}

void authenticate_connection(
    SessionConnectionState& state,
    ix::WebSocket& socket,
    const ProtocolFrame& frame,
    std::string_view expected_token,
    std::string_view session_id) {
    if (!frame.has_client_hello()) {
        socket.close(1008, "authentication required");
        return;
    }

    const auto& hello = frame.client_hello();
    if (!hello.has_protocol() || !protocol_is_compatible(hello.protocol())) {
        socket.close(1002, "protocol mismatch");
        return;
    }

    if (!constant_time_equal(hello.session_token(), expected_token)) {
        socket.close(1008, "authentication failed");
        return;
    }

    state.authenticated.store(true, std::memory_order_release);

    ProtocolFrame response;
    response.set_request_id(frame.request_id());
    auto* server_hello = response.mutable_server_hello();
    auto* version = server_hello->mutable_protocol();
    version->set_major(infraforge::kProtocolMajor);
    version->set_minor(infraforge::kProtocolMinor);
    server_hello->set_engine_version(std::string{infraforge::kEngineVersion});
    server_hello->set_session_id(std::string{session_id});

    if (!send_frame(socket, response)) {
        socket.close(1011, "failed to send server hello");
    }
}

void handle_authenticated_frame(ix::WebSocket& socket, const ProtocolFrame& frame) {
    if (frame.has_ping()) {
        ProtocolFrame response;
        response.set_request_id(frame.request_id());
        response.mutable_pong()->set_monotonic_millis(frame.ping().monotonic_millis());
        if (!send_frame(socket, response)) {
            socket.close(1011, "failed to send pong");
        }
        return;
    }

    send_error(
        socket,
        frame.request_id(),
        ProtocolErrorCode::ERROR_CODE_UNSUPPORTED_MESSAGE,
        "message is not supported by the current engine foundation");
}

} // namespace

WebSocketServer::WebSocketServer(ServerConfig config)
    : config_(std::move(config)) {}

int WebSocketServer::run() {
    NetworkSystemGuard network_system;
    g_stop_requested.store(false, std::memory_order_relaxed);

    std::signal(SIGINT, handle_termination_signal);
#ifdef SIGTERM
    std::signal(SIGTERM, handle_termination_signal);
#endif

    ix::WebSocketServer server(static_cast<int>(config_.port), config_.host);
    server.disablePerMessageDeflate();
    server.setConnectionStateFactory([] {
        return std::make_shared<SessionConnectionState>();
    });

    const std::string expected_token = config_.session_token;
    server.setOnClientMessageCallback(
        [expected_token](
            std::shared_ptr<ix::ConnectionState> base_state,
            ix::WebSocket& socket,
            const ix::WebSocketMessagePtr& message) {
            auto state = std::static_pointer_cast<SessionConnectionState>(base_state);

            if (message->type == ix::WebSocketMessageType::Message) {
                if (!message->binary) {
                    socket.close(1003, "binary protobuf frames required");
                    return;
                }

                ProtocolFrame frame;
                if (!frame.ParseFromString(message->str)) {
                    socket.close(1002, "malformed protobuf frame");
                    return;
                }

                if (!state->authenticated.load(std::memory_order_acquire)) {
                    authenticate_connection(*state, socket, frame, expected_token, state->getId());
                    return;
                }

                handle_authenticated_frame(socket, frame);
            }
        });

    const auto listen_result = server.listen();
    if (!listen_result.first) {
        std::cerr << "Failed to bind InfraForge engine WebSocket server: " << listen_result.second << '\n';
        return 1;
    }

    std::cout
        << "INFRAFORGE_ENGINE_READY {\"host\":\"" << config_.host
        << "\",\"port\":" << config_.port
        << ",\"protocolMajor\":" << infraforge::kProtocolMajor
        << ",\"protocolMinor\":" << infraforge::kProtocolMinor
        << ",\"engineVersion\":\"" << infraforge::kEngineVersion << "\"}\n"
        << std::flush;

    server.start();

    while (!g_stop_requested.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }

    server.stop();
    return 0;
}

} // namespace infraforge::network
