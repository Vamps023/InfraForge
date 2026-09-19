#pragma once

#include "infraforge/domain/road/RoadTypes.hpp"

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace infraforge::domain::road {

enum class LaneSide : std::uint8_t {
    Left,
    Right,
};

enum class LaneType : std::uint8_t {
    Driving,
    Shoulder,
    Sidewalk,
    Biking,
    Parking,
    Median,
    Border,
};

enum class LaneDirection : std::uint8_t {
    Forward,
    Backward,
    Bidirectional,
};

[[nodiscard]] std::string_view laneSideName(LaneSide side) noexcept;
[[nodiscard]] std::optional<LaneSide> laneSideFromName(std::string_view name) noexcept;

[[nodiscard]] std::string_view laneTypeName(LaneType type) noexcept;
[[nodiscard]] std::optional<LaneType> laneTypeFromName(std::string_view name) noexcept;

[[nodiscard]] std::string_view laneDirectionName(LaneDirection direction) noexcept;
[[nodiscard]] std::optional<LaneDirection> laneDirectionFromName(std::string_view name) noexcept;

struct RoadLane {
    std::string id;
    LaneSide side{LaneSide::Right};
    // 1-based index from centerline outward (1 = closest to centerline)
    std::uint32_t laneIndex{1};
    LaneType type{LaneType::Driving};
    LaneDirection direction{LaneDirection::Forward};
    double width{3.5};

    friend bool operator==(const RoadLane&, const RoadLane&) = default;
};

struct RoadLaneSection {
    Station startStation{0.0};
    Station endStation{0.0};
    std::vector<RoadLane> leftLanes;
    std::vector<RoadLane> rightLanes;

    [[nodiscard]] double totalLeftWidth() const noexcept {
        double w = 0.0;
        for (const auto& lane : leftLanes) {
            w += lane.width;
        }
        return w;
    }

    [[nodiscard]] double totalRightWidth() const noexcept {
        double w = 0.0;
        for (const auto& lane : rightLanes) {
            w += lane.width;
        }
        return w;
    }

    friend bool operator==(const RoadLaneSection&, const RoadLaneSection&) = default;
};

// Validates lane sections for a road of the given total length.
// Validates station ranges (continuous, sorted, bounds [0, totalLength]),
// lane properties (positive finite widths, valid indices).
[[nodiscard]] std::vector<RoadDiagnostic> validateLaneSections(
    double totalLength,
    const std::vector<RoadLaneSection>& sections) noexcept;

} // namespace infraforge::domain::road
