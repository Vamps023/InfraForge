#include "infraforge/domain/road/RoadTypes.hpp"

#include "infraforge/runtime/Uuid.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace infraforge::domain::road {
namespace {

std::string_view errorCodeName(RoadErrorCode code) noexcept {
    switch (code) {
    case RoadErrorCode::InvalidArgument:
        return "invalid_argument";
    case RoadErrorCode::NonFiniteParameter:
        return "non_finite_parameter";
    case RoadErrorCode::DegenerateSegment:
        return "degenerate_segment";
    case RoadErrorCode::InvalidCurvature:
        return "invalid_curvature";
    case RoadErrorCode::PositionDiscontinuity:
        return "position_discontinuity";
    case RoadErrorCode::HeadingDiscontinuity:
        return "heading_discontinuity";
    case RoadErrorCode::CurvatureDiscontinuity:
        return "curvature_discontinuity";
    case RoadErrorCode::StationDiscontinuity:
        return "station_discontinuity";
    case RoadErrorCode::EmptyAlignment:
        return "empty_alignment";
    case RoadErrorCode::InvalidProfile:
        return "invalid_profile";
    case RoadErrorCode::AnchorOutOfRange:
        return "anchor_out_of_range";
    }
    return "unknown";
}

std::string_view anchorName(AnchorKind kind) noexcept {
    switch (kind) {
    case AnchorKind::Junction:
        return "junction";
    case AnchorKind::Endpoint:
        return "endpoint";
    case AnchorKind::UserPinned:
        return "user_pinned";
    case AnchorKind::Semantic:
        return "semantic";
    }
    return "unknown";
}

std::string_view providerName(SourceProvider provider) noexcept {
    switch (provider) {
    case SourceProvider::Osm:
        return "osm";
    case SourceProvider::OpenDrive:
        return "opendrive";
    case SourceProvider::Authored:
        return "authored";
    case SourceProvider::Other:
        return "other";
    }
    return "unknown";
}

} // namespace

std::string_view roadErrorCodeName(const RoadErrorCode code) noexcept {
    return errorCodeName(code);
}

std::optional<RoadErrorCode> roadErrorCodeFromName(const std::string_view name) noexcept {
    for (std::size_t index = 0;
         index <= static_cast<std::size_t>(RoadErrorCode::AnchorOutOfRange); ++index) {
        const auto code = static_cast<RoadErrorCode>(index);
        if (errorCodeName(code) == name) {
            return code;
        }
    }
    return std::nullopt;
}

std::string_view anchorKindName(const AnchorKind kind) noexcept {
    return anchorName(kind);
}

std::optional<AnchorKind> anchorKindFromName(const std::string_view name) noexcept {
    for (std::size_t index = 0; index <= static_cast<std::size_t>(AnchorKind::Semantic); ++index) {
        const auto kind = static_cast<AnchorKind>(index);
        if (anchorName(kind) == name) {
            return kind;
        }
    }
    return std::nullopt;
}

std::string_view sourceProviderName(const SourceProvider provider) noexcept {
    return providerName(provider);
}

std::optional<SourceProvider> sourceProviderFromName(const std::string_view name) noexcept {
    for (std::size_t index = 0; index <= static_cast<std::size_t>(SourceProvider::Other); ++index) {
        const auto provider = static_cast<SourceProvider>(index);
        if (providerName(provider) == name) {
            return provider;
        }
    }
    return std::nullopt;
}

RoadId roadIdFromUuidText(const std::string_view uuidText) {
    const std::array<std::uint8_t, 16> bytes = runtime::uuidTextToBytes(uuidText);
    std::uint64_t high = 0;
    std::uint64_t low = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        high = (high << 8) | bytes[i];
    }
    for (std::size_t i = 8; i < 16; ++i) {
        low = (low << 8) | bytes[i];
    }
    return RoadId{.high = high, .low = low};
}

std::string uuidTextFromRoadId(const RoadId id) {
    std::array<std::uint8_t, 16> bytes{};
    for (std::size_t i = 0; i < 8; ++i) {
        bytes[i] = static_cast<std::uint8_t>(id.high >> (56 - 8 * i));
    }
    for (std::size_t i = 0; i < 8; ++i) {
        bytes[8 + i] = static_cast<std::uint8_t>(id.low >> (56 - 8 * i));
    }
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string text;
    text.reserve(36);
    for (std::size_t i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            text.push_back('-');
        }
        text.push_back(kDigits[bytes[i] >> 4]);
        text.push_back(kDigits[bytes[i] & 0x0f]);
    }
    return text;
}

std::optional<ValidationError> validateRoadDisplayName(const std::string_view name) {
    if (name.empty() || name.size() > kMaxRoadDisplayNameLength) {
        return ValidationError{
            .field = "displayName",
            .message = "road display name must be 1.."
                + std::to_string(kMaxRoadDisplayNameLength) + " characters",
        };
    }
    return std::nullopt;
}

} // namespace infraforge::domain::road
