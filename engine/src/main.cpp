#include "infraforge/network/WebSocketServer.hpp"
#include "infraforge/version.hpp"
#include "infraforge/protocol/v1/foundation.pb.h"

#include <charconv>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

struct ServeArguments {
    std::string host;
    std::uint16_t port{};
    std::string session_token;
};

void print_usage() {
    std::cout
        << "InfraForge native engine\n\n"
        << "Usage:\n"
        << "  infraforge-engine --version\n"
        << "  infraforge-engine --self-check\n"
        << "  infraforge-engine --serve --host 127.0.0.1 --port <port> --session-token <64-hex-token>\n"
        << "  infraforge-engine --help\n";
}

bool is_hex_token(std::string_view token) {
    if (token.size() != 64) {
        return false;
    }
    for (const char value : token) {
        if (std::isxdigit(static_cast<unsigned char>(value)) == 0) {
            return false;
        }
    }
    return true;
}

std::optional<std::uint16_t> parse_port(std::string_view text) {
    unsigned int parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || parsed == 0 || parsed > 65535) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(parsed);
}

std::optional<ServeArguments> parse_serve_arguments(int argc, char** argv) {
    ServeArguments arguments;
    bool have_host = false;
    bool have_port = false;
    bool have_token = false;

    for (int index = 2; index < argc; index += 2) {
        if (index + 1 >= argc) {
            return std::nullopt;
        }

        const std::string_view key{argv[index]};
        const std::string_view value{argv[index + 1]};

        if (key == "--host" && !have_host) {
            arguments.host = value;
            have_host = true;
        } else if (key == "--port" && !have_port) {
            const auto port = parse_port(value);
            if (!port.has_value()) {
                return std::nullopt;
            }
            arguments.port = *port;
            have_port = true;
        } else if (key == "--session-token" && !have_token) {
            arguments.session_token = value;
            have_token = true;
        } else {
            return std::nullopt;
        }
    }

    if (!have_host || !have_port || !have_token) {
        return std::nullopt;
    }

    if (arguments.host != "127.0.0.1" || !is_hex_token(arguments.session_token)) {
        return std::nullopt;
    }

    return arguments;
}

int run_self_check() {
    infraforge::protocol::v1::Frame source;
    source.set_request_id("self-check");
    auto* hello = source.mutable_client_hello();
    auto* version = hello->mutable_protocol();
    version->set_major(infraforge::kProtocolMajor);
    version->set_minor(infraforge::kProtocolMinor);
    hello->set_session_token(std::string(64, 'a'));
    hello->set_client_name("infraforge-engine-self-check");
    hello->set_client_version(std::string{infraforge::kEngineVersion});

    std::string encoded;
    if (!source.SerializeToString(&encoded)) {
        std::cerr << "Protocol serialization self-check failed\n";
        return 1;
    }

    infraforge::protocol::v1::Frame decoded;
    if (!decoded.ParseFromString(encoded) || !decoded.has_client_hello() || decoded.request_id() != "self-check") {
        std::cerr << "Protocol parse self-check failed\n";
        return 1;
    }

    std::cout
        << "{\"component\":\"" << infraforge::kEngineExecutableName
        << "\",\"version\":\"" << infraforge::kEngineVersion
        << "\",\"protocolMajor\":" << infraforge::kProtocolMajor
        << ",\"protocolMinor\":" << infraforge::kProtocolMinor
        << ",\"status\":\"ok\"}\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return 2;
    }

    const std::string_view command{argv[1]};

    if (command == "--version" && argc == 2) {
        std::cout << infraforge::kEngineExecutableName << ' ' << infraforge::kEngineVersion << '\n';
        return 0;
    }

    if (command == "--self-check" && argc == 2) {
        return run_self_check();
    }

    if ((command == "--help" || command == "-h") && argc == 2) {
        print_usage();
        return 0;
    }

    if (command == "--serve") {
        const auto arguments = parse_serve_arguments(argc, argv);
        if (!arguments.has_value()) {
            std::cerr << "Invalid --serve arguments. Host must be 127.0.0.1 and session token must be 64 hexadecimal characters.\n";
            print_usage();
            return 2;
        }

        return infraforge::network::WebSocketServer({
            .host = arguments->host,
            .port = arguments->port,
            .session_token = arguments->session_token,
        }).run();
    }

    std::cerr << "Unknown command or invalid arguments\n";
    print_usage();
    return 2;
}
