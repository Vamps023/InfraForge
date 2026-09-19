#include "infraforge/domain/road/JunctionTypes.hpp"

namespace infraforge::domain::road {

JunctionId junctionIdFromUuidText(const std::string_view uuidText) {
    return roadIdFromUuidText(uuidText);
}

std::string uuidTextFromJunctionId(const JunctionId id) {
    return uuidTextFromRoadId(id);
}

std::string_view junctionTypeName(const JunctionType type) noexcept {
    switch (type) {
    case JunctionType::Custom:
        return "custom";
    case JunctionType::T:
        return "T";
    case JunctionType::FourWay:
        return "4way";
    }
    return "custom";
}

std::optional<JunctionType> junctionTypeFromName(const std::string_view name) noexcept {
    if (name == "custom") return JunctionType::Custom;
    if (name == "T" || name == "t") return JunctionType::T;
    if (name == "4way" || name == "four_way") return JunctionType::FourWay;
    return std::nullopt;
}

std::string_view junctionMovementTypeName(const JunctionMovementType type) noexcept {
    switch (type) {
    case JunctionMovementType::Straight:
        return "straight";
    case JunctionMovementType::Left:
        return "left";
    case JunctionMovementType::Right:
        return "right";
    case JunctionMovementType::UTurn:
        return "uturn";
    }
    return "straight";
}

std::optional<JunctionMovementType> junctionMovementTypeFromName(const std::string_view name) noexcept {
    if (name == "straight") return JunctionMovementType::Straight;
    if (name == "left") return JunctionMovementType::Left;
    if (name == "right") return JunctionMovementType::Right;
    if (name == "uturn" || name == "u_turn") return JunctionMovementType::UTurn;
    return std::nullopt;
}

} // namespace infraforge::domain::road
