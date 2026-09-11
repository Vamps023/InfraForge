#include "infraforge/version.hpp"

#include <iostream>
#include <string_view>

namespace {

void print_usage() {
    std::cout
        << "InfraForge native engine foundation\n\n"
        << "Usage:\n"
        << "  infraforge-engine --version\n"
        << "  infraforge-engine --self-check\n"
        << "  infraforge-engine --help\n\n"
        << "The network service is not exposed until the authenticated WebSocket implementation is committed.\n";
}

int run_self_check() {
    std::cout
        << "{\"component\":\"" << infraforge::kEngineExecutableName
        << "\",\"version\":\"" << infraforge::kEngineVersion
        << "\",\"status\":\"ok\"}\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        print_usage();
        return 2;
    }

    const std::string_view argument{argv[1]};

    if (argument == "--version") {
        std::cout << infraforge::kEngineExecutableName << ' ' << infraforge::kEngineVersion << '\n';
        return 0;
    }

    if (argument == "--self-check") {
        return run_self_check();
    }

    if (argument == "--help" || argument == "-h") {
        print_usage();
        return 0;
    }

    std::cerr << "Unknown argument: " << argument << '\n';
    print_usage();
    return 2;
}
