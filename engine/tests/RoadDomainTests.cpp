#include <doctest/doctest.h>

#include "infraforge/domain/road/RoadTypes.hpp"

#include <optional>
#include <string>

TEST_SUITE("road domain foundation") {

TEST_CASE("road error codes round-trip through stable names") {
    using infraforge::domain::road::RoadErrorCode;
    for (std::size_t code = 0;
        code <= static_cast<std::size_t>(RoadErrorCode::AnchorOutOfRange); ++code) {
        const auto value = static_cast<RoadErrorCode>(code);
        const auto parsed = infraforge::domain::road::roadErrorCodeFromName(
            infraforge::domain::road::roadErrorCodeName(value));
        REQUIRE(parsed.has_value());
        CHECK(*parsed == value);
    }
    CHECK_FALSE(infraforge::domain::road::roadErrorCodeFromName("not_a_code").has_value());
}

TEST_CASE("anchor kinds round-trip through stable names") {
    using infraforge::domain::road::AnchorKind;
    for (std::size_t kind = 0;
        kind <= static_cast<std::size_t>(AnchorKind::Semantic); ++kind) {
        const auto value = static_cast<AnchorKind>(kind);
        const auto parsed = infraforge::domain::road::anchorKindFromName(
            infraforge::domain::road::anchorKindName(value));
        REQUIRE(parsed.has_value());
        CHECK(*parsed == value);
    }
    CHECK_FALSE(infraforge::domain::road::anchorKindFromName("not_a_kind").has_value());
}

TEST_CASE("source providers round-trip through stable names") {
    using infraforge::domain::road::SourceProvider;
    for (std::size_t provider = 0;
        provider <= static_cast<std::size_t>(SourceProvider::Other); ++provider) {
        const auto value = static_cast<SourceProvider>(provider);
        const auto parsed = infraforge::domain::road::sourceProviderFromName(
            infraforge::domain::road::sourceProviderName(value));
        REQUIRE(parsed.has_value());
        CHECK(*parsed == value);
    }
    CHECK_FALSE(infraforge::domain::road::sourceProviderFromName("not_a_provider").has_value());
}

TEST_CASE("road identity maps uuid text <-> road id") {
    const std::string uuid = "0f1e2d3c-4b5a-6978-8798-aabbccddeeff";
    const auto id = infraforge::domain::road::roadIdFromUuidText(uuid);
    CHECK_FALSE(id.isNull());
    CHECK(infraforge::domain::road::uuidTextFromRoadId(id) == uuid);
}

TEST_CASE("road display name validation rejects empty and overlong names") {
    using infraforge::domain::road::validateRoadDisplayName;
    CHECK_FALSE(validateRoadDisplayName("Highway 1").has_value());
    CHECK(validateRoadDisplayName("").has_value());
    CHECK(validateRoadDisplayName(std::string(infraforge::domain::road::kMaxRoadDisplayNameLength + 1, 'a')).has_value());
}

TEST_CASE("station range reports length and containment") {
    infraforge::domain::road::StationRange range{100.0, 145.0};
    CHECK(range.length() == doctest::Approx(45.0));
    CHECK(range.contains(100.0));
    CHECK(range.contains(145.0));
    CHECK(range.contains(120.0));
    CHECK_FALSE(range.contains(99.999));
    CHECK_FALSE(range.contains(145.001));
}

} // TEST_SUITE
