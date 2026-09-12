#include <doctest/doctest.h>

#include "infraforge/viewport/ViewportApplication.hpp"

#include <cstring>
#include <utility>

namespace {

using infraforge::viewport::ApplicationArguments;
using infraforge::viewport::RendererStatus;
using infraforge::viewport::parseApplicationArguments;
using infraforge::viewport::visibilityStatusReport;

RendererStatus statusIn(std::string state) {
    RendererStatus status;
    status.state = std::move(state);
    status.detail = "last renderer diagnostic";
    status.gpuName = "Test GPU";
    status.vulkanVersion = "1.3";
    return status;
}

bool parseArgs(const char* const* argv, const int argc, ApplicationArguments& arguments) {
    std::string errorMessage;
    return parseApplicationArguments(argc, const_cast<char**>(argv), arguments, errorMessage);
}

const char* kVisibleBase[] = {
    "infraforge-viewport", "--parent-window", "1A", "--screen-x", "0", "--screen-y", "0",
    "--width", "100", "--height", "100", "--dpi-scale", "100"};

} // namespace

TEST_SUITE("viewport argument parsing") {
    TEST_CASE("startup visibility defaults to visible without --initial-visible") {
        ApplicationArguments arguments;
        REQUIRE(parseArgs(kVisibleBase, 13, arguments));
        CHECK(arguments.initialVisible);
    }

    TEST_CASE("--initial-visible 0 requests a hidden startup surface") {
        const char* argv[] = {
            "infraforge-viewport", "--parent-window", "1A", "--screen-x", "0", "--screen-y", "0",
            "--width", "100", "--height", "100", "--dpi-scale", "100", "--initial-visible", "0"};
        ApplicationArguments arguments;
        REQUIRE(parseArgs(argv, 15, arguments));
        CHECK_FALSE(arguments.initialVisible);
    }

    TEST_CASE("--initial-visible 1 keeps the startup surface visible") {
        const char* argv[] = {
            "infraforge-viewport", "--parent-window", "1A", "--screen-x", "0", "--screen-y", "0",
            "--width", "100", "--height", "100", "--dpi-scale", "100", "--initial-visible", "1"};
        ApplicationArguments arguments;
        REQUIRE(parseArgs(argv, 15, arguments));
        CHECK(arguments.initialVisible);
    }

    TEST_CASE("invalid --initial-visible values are rejected explicitly") {
        for (const char* bad : {"2", "true", ""}) {
            const char* argv[] = {
                "infraforge-viewport", "--parent-window", "1A", "--screen-x", "0", "--screen-y", "0",
                "--width", "100", "--height", "100", "--dpi-scale", "100", "--initial-visible", bad};
            ApplicationArguments arguments;
            std::string errorMessage;
            CHECK_FALSE(parseApplicationArguments(15, const_cast<char**>(argv), arguments, errorMessage));
            CHECK_FALSE(errorMessage.empty());
        }
    }

    TEST_CASE("duplicate --initial-visible is rejected") {
        const char* argv[] = {
            "infraforge-viewport", "--parent-window", "1A", "--screen-x", "0", "--screen-y", "0",
            "--width", "100", "--height", "100", "--dpi-scale", "100",
            "--initial-visible", "0", "--initial-visible", "1"};
        ApplicationArguments arguments;
        std::string errorMessage;
        CHECK_FALSE(parseApplicationArguments(17, const_cast<char**>(argv), arguments, errorMessage));
    }
}

TEST_SUITE("visibility status reporting") {
    TEST_CASE("a live renderer reports nothing for visibility commands") {
        CHECK_FALSE(visibilityStatusReport(true, statusIn("ready")).has_value());
        CHECK_FALSE(visibilityStatusReport(true, statusIn("suspended")).has_value());
        CHECK_FALSE(visibilityStatusReport(true, statusIn("recreating")).has_value());
    }

    TEST_CASE("a dead renderer re-asserts its last failure instead of implying readiness") {
        const auto failed = visibilityStatusReport(false, statusIn("failed"));
        REQUIRE(failed.has_value());
        CHECK(failed->state == "failed");
        CHECK(failed->state != "ready");
        CHECK(failed->detail == "last renderer diagnostic");

        const auto deviceLost = visibilityStatusReport(false, statusIn("device_lost"));
        REQUIRE(deviceLost.has_value());
        CHECK(deviceLost->state == "device_lost");
        CHECK(deviceLost->state != "ready");
    }
}
