#include <doctest/doctest.h>

#include "TestHelpers.hpp"
#include "infraforge/domain/project/ProjectModel.hpp"

#include <limits>
#include <string>

TEST_SUITE("project domain model") {
    TEST_CASE("display name validation accepts portable names") {
        using infraforge::domain::project::validateDisplayName;

        CHECK_FALSE(validateDisplayName("My Project").has_value());
        CHECK_FALSE(validateDisplayName("Route A-1_b").has_value());
        CHECK_FALSE(validateDisplayName(std::string(128, 'a')).has_value());
    }

    TEST_CASE("display name validation rejects unsafe names") {
        using infraforge::domain::project::validateDisplayName;

        CHECK(validateDisplayName("").has_value());
        CHECK(validateDisplayName(" leading").has_value());
        CHECK(validateDisplayName("trailing ").has_value());
        CHECK(validateDisplayName(std::string(129, 'a')).has_value());
        CHECK(validateDisplayName("bad/name").has_value());
        CHECK(validateDisplayName("bad\\name").has_value());
        CHECK(validateDisplayName("bad:name").has_value());
        CHECK(validateDisplayName("CON").has_value());
        CHECK(validateDisplayName("com1").has_value());
    }

    TEST_CASE("georeference validation requires canonical configuration") {
        using infraforge::domain::geo::validateGeoreference;

        auto valid = infraforge::testhelpers::sampleGeoreference();
        CHECK_FALSE(validateGeoreference(valid).has_value());

        auto emptyCrs = valid;
        emptyCrs.horizontalCrs = "";
        CHECK(validateGeoreference(emptyCrs).has_value());

        auto emptyUnit = valid;
        emptyUnit.linearUnit = "";
        CHECK(validateGeoreference(emptyUnit).has_value());

        auto infiniteOrigin = valid;
        infiniteOrigin.originEasting = std::numeric_limits<double>::infinity();
        CHECK(validateGeoreference(infiniteOrigin).has_value());
    }

    TEST_CASE("traffic side and axis convention names round-trip") {
        namespace project = infraforge::domain::project;

        for (const auto side : {project::TrafficSide::Left, project::TrafficSide::Right}) {
            const auto parsed = project::trafficSideFromName(project::trafficSideName(side));
            REQUIRE(parsed.has_value());
            CHECK(*parsed == side);
        }
        CHECK_FALSE(project::trafficSideFromName("middle").has_value());

        namespace geo = infraforge::domain::geo;
        const auto axis = geo::AxisConvention::EastingNorthingUp;
        const auto parsedAxis = geo::axisConventionFromName(geo::axisConventionName(axis));
        REQUIRE(parsedAxis.has_value());
        CHECK(*parsedAxis == axis);
        CHECK_FALSE(geo::axisConventionFromName("northing_easting").has_value());
    }
}
