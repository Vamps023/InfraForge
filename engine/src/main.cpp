#include "infraforge/application/CommandProcessor.hpp"
#include "infraforge/network/CommandRouter.hpp"
#include "infraforge/network/WebSocketServer.hpp"
#include "infraforge/persistence/SqliteProjectStore.hpp"
#include "infraforge/runtime/Logging.hpp"
#include "infraforge/version.hpp"
#include "infraforge/protocol/v1/foundation.pb.h"

#include <charconv>
#include <cctype>
#include <cstdint>
#include <exception>
#include <filesystem>
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
    // 1. Protocol frame round-trip including the project lifecycle envelope.
    infraforge::protocol::v1::Frame source;
    source.set_request_id("self-check");
    auto* command = source.mutable_command();
    auto* create = command->mutable_create_project();
    create->set_display_name("Self Check");
    create->set_parent_directory("/tmp/nonexistent");
    auto* georeference = create->mutable_georeference();
    georeference->set_horizontal_crs("EPSG:32633");
    georeference->set_linear_unit("metre");
    georeference->set_axis_convention(infraforge::protocol::v1::AXIS_CONVENTION_EASTING_NORTHING_UP);
    create->set_traffic_side(infraforge::protocol::v1::TRAFFIC_SIDE_RIGHT);

    std::string encoded;
    if (!source.SerializeToString(&encoded)) {
        std::cerr << "Protocol serialization self-check failed\n";
        return 1;
    }

    infraforge::protocol::v1::Frame decoded;
    if (!decoded.ParseFromString(encoded) || !decoded.has_command()
        || !decoded.command().has_create_project()
        || decoded.command().create_project().display_name() != "Self Check"
        || decoded.command().create_project().georeference().horizontal_crs() != "EPSG:32633") {
        std::cerr << "Protocol parse self-check failed\n";
        return 1;
    }

    // 2. Project persistence round-trip against a real temporary directory.
    std::error_code ioError;
    const auto scratchRoot = std::filesystem::temp_directory_path(ioError) / "infraforge-engine-self-check";
    if (ioError) {
        std::cerr << "Cannot resolve temporary directory for self-check\n";
        return 1;
    }
    std::filesystem::remove_all(scratchRoot, ioError);
    const auto parentDirectory = scratchRoot / "projects";
    std::filesystem::create_directories(parentDirectory, ioError);
    if (ioError) {
        std::cerr << "Cannot create scratch directory for self-check\n";
        return 1;
    }

    try {
        infraforge::persistence::SqliteProjectStore store;

        infraforge::domain::project::CreateProjectSpec spec;
        spec.displayName = "Self Check";
        spec.parentDirectory = parentDirectory;
        spec.trafficSide = infraforge::domain::project::TrafficSide::Right;
        spec.georeference.horizontalCrs = "EPSG:32633";
        spec.georeference.linearUnit = "metre";
        spec.georeference.axisConvention = infraforge::domain::project::AxisConvention::EastingNorthingUp;
        spec.georeference.originEasting = 500000.0;
        spec.georeference.originNorthing = 4649776.0;

        const auto created = store.create(spec);
        if (!store.isOpen() || created.uuid.empty() || created.revision != 1) {
            std::cerr << "Project creation self-check failed\n";
            return 1;
        }
        const auto projectDirectory = std::filesystem::path(created.directory);
        store.close();

        const auto reopened = store.open(projectDirectory);
        if (reopened.uuid != created.uuid || reopened.displayName != created.displayName
            || reopened.revision != created.revision
            || !(reopened.georeference == created.georeference)) {
            std::cerr << "Project reopen self-check failed\n";
            return 1;
        }

        store.close();
    } catch (const std::exception& error) {
        std::cerr << "Project persistence self-check failed: " << error.what() << '\n';
        std::filesystem::remove_all(scratchRoot, ioError);
        return 1;
    }

    std::filesystem::remove_all(scratchRoot, ioError);

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

        infraforge::persistence::SqliteProjectStore store;
        infraforge::network::WebSocketCommandRouter router;
        infraforge::application::CommandProcessor processor(store, router);
        processor.start();

        const int exitCode = infraforge::network::WebSocketServer(
            {
                .host = arguments->host,
                .port = arguments->port,
                .session_token = arguments->session_token,
            },
            processor,
            router).run();

        processor.shutdown();
        if (store.isOpen()) {
            // The graceful project-aware shutdown protocol is a later
            // milestone; flush and close the session so SQLite exits cleanly.
            const auto projectUuid = store.current().uuid;
            store.close();
            infraforge::runtime::logInfo("engine", "project.session_closed_at_shutdown", {{"projectUuid", projectUuid}});
        }
        return exitCode;
    }

    std::cerr << "Unknown command or invalid arguments\n";
    print_usage();
    return 2;
}
