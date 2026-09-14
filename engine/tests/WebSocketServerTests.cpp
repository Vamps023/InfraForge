#include <doctest/doctest.h>

#include "TestHelpers.hpp"
#include "infraforge/application/CommandProcessor.hpp"
#include "infraforge/network/CommandRouter.hpp"
#include "infraforge/network/WebSocketServer.hpp"
#include "infraforge/persistence/SqliteProjectStore.hpp"
#include "infraforge/runtime/FileSystemUtf8.hpp"
#include "infraforge/version.hpp"

#include <infraforge/protocol/v1/foundation.pb.h>

#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace {

using ProtocolFrame = infraforge::protocol::v1::Frame;

constexpr std::chrono::milliseconds kConnectTimeout{5000};
constexpr std::chrono::milliseconds kFrameTimeout{30000};
constexpr std::chrono::milliseconds kQuietWindow{600};

bool waitUntil(const std::function<bool()>& predicate, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    return predicate();
}

// Probes the OS for an unused loopback TCP port; the port is released before
// the engine binds it (the desktop supervisor uses the same strategy).
std::uint16_t freeLoopbackPort() {
    const int fd = static_cast<int>(socket(AF_INET, SOCK_STREAM, 0));
    REQUIRE(fd >= 0);

    struct sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;

    int result = bind(fd, reinterpret_cast<struct sockaddr*>(&address), sizeof(address));
    REQUIRE(result == 0);
    result = listen(fd, 1);
    REQUIRE(result == 0);

    socklen_t length = sizeof(address);
    result = getsockname(fd, reinterpret_cast<struct sockaddr*>(&address), &length);
    REQUIRE(result == 0);
    const std::uint16_t port = ntohs(address.sin_port);

#ifdef _WIN32
    (void)closesocket(fd);
#else
    (void)::close(fd);
#endif
    REQUIRE(port != 0);
    return port;
}

class NetSystemGuard final {
public:
    NetSystemGuard() { ix::initNetSystem(); }
    ~NetSystemGuard() { ix::uninitNetSystem(); }

    NetSystemGuard(const NetSystemGuard&) = delete;
    NetSystemGuard& operator=(const NetSystemGuard&) = delete;
};

// Records every binary frame and close event a client observes.
class TestClient final {
public:
    explicit TestClient(const std::string& url) {
        socket_ = std::make_shared<ix::WebSocket>();
        socket_->disableAutomaticReconnection();
        socket_->setUrl(url);
        socket_->setOnMessageCallback([this](const ix::WebSocketMessagePtr& message) {
            if (message->type == ix::WebSocketMessageType::Message) {
                if (!message->binary) {
                    return;
                }
                ProtocolFrame frame;
                if (!frame.ParseFromString(message->str)) {
                    malformedFrames_.fetch_add(1, std::memory_order_relaxed);
                    return;
                }
                std::lock_guard lock{mutex_};
                frames_.push_back(std::move(frame));
                return;
            }
            if (message->type == ix::WebSocketMessageType::Close) {
                closeCode_.store(static_cast<int>(message->closeInfo.code), std::memory_order_release);
                closed_.store(true, std::memory_order_release);
            }
        });
        socket_->start();
    }

    ~TestClient() {
        socket_->stop();
    }

    [[nodiscard]] bool isOpen() const {
        return socket_->getReadyState() == ix::ReadyState::Open;
    }

    void send(const ProtocolFrame& frame) {
        std::string bytes;
        REQUIRE(frame.SerializeToString(&bytes));
        socket_->sendBinary(bytes);
    }

    [[nodiscard]] bool closed() const {
        return closed_.load(std::memory_order_acquire);
    }

    [[nodiscard]] int closeCode() const {
        return closeCode_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::size_t binaryFrameCount() const {
        std::lock_guard lock{mutex_};
        return frames_.size();
    }

    [[nodiscard]] bool hasServerHello() const {
        std::lock_guard lock{mutex_};
        for (const ProtocolFrame& frame : frames_) {
            if (frame.has_server_hello()) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool hasResult(const std::string& requestId) const {
        std::lock_guard lock{mutex_};
        for (const ProtocolFrame& frame : frames_) {
            if (frame.has_result() && frame.request_id() == requestId) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] std::size_t eventCount() const {
        std::lock_guard lock{mutex_};
        std::size_t count = 0;
        for (const ProtocolFrame& frame : frames_) {
            if (frame.has_event()) {
                ++count;
            }
        }
        return count;
    }

private:
    std::shared_ptr<ix::WebSocket> socket_;
    mutable std::mutex mutex_;
    std::deque<ProtocolFrame> frames_;
    std::atomic_bool closed_{false};
    std::atomic_int closeCode_{0};
    std::atomic_int malformedFrames_{0};
};

// In-process engine: real store + executor + router + WebSocket server on a
// loopback port, running on its own thread like the supervised process does.
struct ServerHarness {
    NetSystemGuard netSystem;
    infraforge::testhelpers::ScratchDirectory scratch;
    infraforge::persistence::SqliteProjectStore store;
    infraforge::network::WebSocketCommandRouter router;
    infraforge::domain::geo::GeoTransformService transforms;
    infraforge::application::CommandProcessor processor{store, transforms, router};
    std::uint16_t port{freeLoopbackPort()};
    std::string token{std::string(64, 'a')};
    infraforge::network::WebSocketServer server{
        {"127.0.0.1", port, token}, processor, router};
    std::thread thread{[this] { (void)server.run(); }};

    ServerHarness() {
        processor.start();
    }

    ~ServerHarness() {
        server.requestStop();
        thread.join();
        processor.shutdown();
    }

    [[nodiscard]] std::string url() const {
        return "ws://127.0.0.1:" + std::to_string(port) + "/";
    }
};

ProtocolFrame helloFrame(const std::string& requestId, const std::string& token) {
    ProtocolFrame frame;
    frame.set_request_id(requestId);
    auto* hello = frame.mutable_client_hello();
    hello->mutable_protocol()->set_major(infraforge::kProtocolMajor);
    hello->mutable_protocol()->set_minor(infraforge::kProtocolMinor);
    hello->set_session_token(token);
    hello->set_client_name("websocket-server-tests");
    hello->set_client_version("0.2.0");
    return frame;
}

ProtocolFrame createCommandFrame(const std::string& requestId, const std::filesystem::path& parent) {
    ProtocolFrame frame;
    frame.set_request_id(requestId);
    auto* create = frame.mutable_command()->mutable_create_project();
    create->set_display_name("Network Test");
    create->set_parent_directory(infraforge::runtime::utf8String(parent));
    auto* georeference = create->mutable_georeference();
    georeference->set_horizontal_crs("EPSG:32633");
    georeference->set_linear_unit("metre");
    georeference->set_axis_convention(infraforge::protocol::v1::AXIS_CONVENTION_EASTING_NORTHING_UP);
    create->set_traffic_side(infraforge::protocol::v1::TRAFFIC_SIDE_RIGHT);
    return frame;
}

// Connects, authenticates, and drives one full lifecycle so events flow.
void runLifecycle(TestClient& member, ServerHarness& harness) {
    member.send(helloFrame("net-hello", harness.token));
    REQUIRE(waitUntil([&] { return member.hasServerHello(); }, kFrameTimeout));

    member.send(createCommandFrame("net-create", harness.scratch.path()));
    REQUIRE(waitUntil([&] { return member.hasResult("net-create"); }, kFrameTimeout));
    REQUIRE(waitUntil([&] { return member.eventCount() >= 1; }, kFrameTimeout));

    ProtocolFrame saveFrame;
    saveFrame.set_request_id("net-save");
    saveFrame.mutable_command()->mutable_save_project();
    member.send(saveFrame);
    REQUIRE(waitUntil([&] { return member.hasResult("net-save"); }, kFrameTimeout));

    ProtocolFrame closeFrame;
    closeFrame.set_request_id("net-close");
    closeFrame.mutable_command()->mutable_close_project();
    member.send(closeFrame);
    REQUIRE(waitUntil([&] { return member.hasResult("net-close"); }, kFrameTimeout));
    REQUIRE(waitUntil([&] { return member.eventCount() >= 2; }, kFrameTimeout));
}

} // namespace

TEST_SUITE("websocket server authentication") {
    TEST_CASE("unauthenticated connections never receive project events") {
        ServerHarness harness;

        // The intruder connects but never authenticates.
        TestClient intruder(harness.url());
        REQUIRE(waitUntil([&] { return intruder.isOpen(); }, kConnectTimeout));

        // A legitimate client exercises the real lifecycle.
        TestClient member(harness.url());
        REQUIRE(waitUntil([&] { return member.isOpen(); }, kConnectTimeout));
        runLifecycle(member, harness);

        // Give any (incorrect) broadcast a generous window to arrive before
        // judging silence.
        std::this_thread::sleep_for(kQuietWindow);

        CHECK_EQ(intruder.binaryFrameCount(), 0);
        CHECK_FALSE(intruder.closed());
        CHECK_GE(member.binaryFrameCount(), 5);
    }

    TEST_CASE("an invalid session token is rejected without event registration") {
        ServerHarness harness;

        TestClient intruder(harness.url());
        REQUIRE(waitUntil([&] { return intruder.isOpen(); }, kConnectTimeout));
        intruder.send(helloFrame("bad-hello", std::string(64, 'b')));

        REQUIRE(waitUntil([&] { return intruder.closed(); }, kConnectTimeout));
        CHECK_EQ(intruder.closeCode(), 1008);
        CHECK_EQ(intruder.binaryFrameCount(), 0);

        // A subsequent legitimate session must still receive its events,
        // proving the rejection did not disturb the router.
        TestClient member(harness.url());
        REQUIRE(waitUntil([&] { return member.isOpen(); }, kConnectTimeout));
        runLifecycle(member, harness);
        CHECK_GE(member.eventCount(), 2);
    }
}
