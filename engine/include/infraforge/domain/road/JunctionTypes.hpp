#pragma once

#include "infraforge/domain/road/RoadTypes.hpp"
#include "infraforge/domain/world/EntityId.hpp"

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace infraforge::domain::road {

using JunctionId = world::EntityId;

[[nodiscard]] JunctionId junctionIdFromUuidText(std::string_view uuidText);
[[nodiscard]] std::string uuidTextFromJunctionId(JunctionId id);

enum class JunctionType : std::uint8_t {
    Custom,
    T,
    FourWay,
};

[[nodiscard]] std::string_view junctionTypeName(JunctionType type) noexcept;
[[nodiscard]] std::optional<JunctionType> junctionTypeFromName(std::string_view name) noexcept;

enum class JunctionMovementType : std::uint8_t {
    Straight,
    Left,
    Right,
    UTurn,
};

[[nodiscard]] std::string_view junctionMovementTypeName(JunctionMovementType type) noexcept;
[[nodiscard]] std::optional<JunctionMovementType> junctionMovementTypeFromName(std::string_view name) noexcept;

struct JunctionApproach {
    RoadId roadId{};
    std::string contactPoint{"end"}; // "start" or "end"
    AlignmentPoint entryPoint{};
    double heading{0.0};
    std::vector<std::string> laneIds;

    friend bool operator==(const JunctionApproach&, const JunctionApproach&) = default;
};

struct JunctionConnection {
    std::string id;
    RoadId fromRoadId{};
    std::string fromLaneId;
    RoadId toRoadId{};
    std::string toLaneId;
    JunctionMovementType movementType{JunctionMovementType::Straight};
    bool allowed{true};

    friend bool operator==(const JunctionConnection&, const JunctionConnection&) = default;
};

struct Junction {
    JunctionId id{};
    std::string name;
    JunctionType type{JunctionType::Custom};
    AlignmentPoint position{};
    double elevation{0.0};
    std::vector<JunctionApproach> approaches;
    std::vector<JunctionConnection> connections;
    std::uint64_t revision{1};

    friend bool operator==(const Junction&, const Junction&) = default;
};

} // namespace infraforge::domain::road
