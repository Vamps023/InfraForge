#include <doctest/doctest.h>

#include "../../engine/tests/TestHelpers.hpp"
#include "infraforge/viewport/control/ControlProtocol.hpp"

#include <nlohmann/json.hpp>

#include <functional>
#include <string>

namespace {

using namespace infraforge::viewport;
using infraforge::testhelpers::captureException;

} // namespace

TEST_SUITE("viewport control protocol") {
    TEST_CASE("place commands parse all placement fields") {
        const auto command = parseControlCommand(
            R"({"type":"place","screenX":-120,"screenY":640,"width":1024,"height":768,"dpiScale":1.5})");
        const auto* place = std::get_if<PlaceCommand>(&command);
        REQUIRE(place != nullptr);
        CHECK_EQ(place->placement.screenX, -120);
        CHECK_EQ(place->placement.screenY, 640);
        CHECK_EQ(place->placement.width, 1024U);
        CHECK_EQ(place->placement.height, 768U);
        CHECK_EQ(place->placement.dpiScale, doctest::Approx(1.5));
    }

    TEST_CASE("visibility and shutdown commands parse") {
        const auto show = parseControlCommand(R"({"type":"visibility","visible":true})");
        const auto* showCommand = std::get_if<VisibilityCommand>(&show);
        REQUIRE(showCommand != nullptr);
        CHECK(showCommand->visible);

        const auto hide = parseControlCommand(R"({"type":"visibility","visible":false})");
        const auto* hideCommand = std::get_if<VisibilityCommand>(&hide);
        REQUIRE(hideCommand != nullptr);
        CHECK_FALSE(hideCommand->visible);

        const auto shutdown = parseControlCommand(R"({"type":"shutdown"})");
        CHECK(std::holds_alternative<ShutdownCommand>(shutdown));
    }

    TEST_CASE("malformed commands are rejected explicitly") {
        using ParseCheck = std::function<void()>;
        const auto rejects = [](const std::string& line) {
            return captureException<CommandParseError>([&] { (void)parseControlCommand(line); });
        };

        CHECK(rejects("not json").has_value());
        CHECK(rejects("[1,2,3]").has_value());
        CHECK(rejects(R"({"type":"explode"})").has_value());
        CHECK(rejects(R"({"type":"place","screenX":0})").has_value());
        CHECK(rejects(R"({"type":"place","screenX":0,"screenY":0,"width":-4,"height":10,"dpiScale":1})").has_value());
        CHECK(rejects(R"({"type":"visibility","visible":"yes"})").has_value());
        CHECK(rejects(R"({"type":"shutdown","extra":1})").has_value());

        auto error = rejects(R"({"type":"place","screenX":0})");
        REQUIRE(error.has_value());
        CHECK_FALSE(error->message.empty());
    }

    TEST_CASE("status and ready records are machine-parseable") {
        const std::string ready = formatReadyRecord(
            SurfacePlacement{.screenX = 0, .screenY = 0, .width = 800, .height = 600, .dpiScale = 1.25}, "win32");
        CHECK(ready.rfind(kReadyPrefix, 0) == 0);
        const auto readyJson = nlohmann::json::parse(ready.substr(kReadyPrefix.size()));
        CHECK_EQ(readyJson.at("width").get<int>(), 800);
        CHECK_EQ(readyJson.at("platform").get<std::string>(), "win32");

        const std::string status = formatStatusRecord("ready", "all good", "GPU", "1.3", true);
        CHECK(status.rfind(kStatusPrefix, 0) == 0);
        const auto statusJson = nlohmann::json::parse(status.substr(kStatusPrefix.size()));
        CHECK_EQ(statusJson.at("state").get<std::string>(), "ready");
        CHECK_EQ(statusJson.at("gpu").get<std::string>(), "GPU");
        CHECK_EQ(statusJson.at("vulkan").get<std::string>(), "1.3");
        CHECK(statusJson.at("validation").get<bool>());
    }
}
