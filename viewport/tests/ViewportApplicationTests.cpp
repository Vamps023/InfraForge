#include <doctest/doctest.h>

#include "infraforge/viewport/ViewportApplication.hpp"

#include <utility>

namespace {

using infraforge::viewport::RendererStatus;
using infraforge::viewport::visibilityStatusReport;

RendererStatus statusIn(std::string state) {
    RendererStatus status;
    status.state = std::move(state);
    status.detail = "last renderer diagnostic";
    status.gpuName = "Test GPU";
    status.vulkanVersion = "1.3";
    return status;
}


} // namespace

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
