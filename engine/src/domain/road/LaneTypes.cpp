#include "infraforge/domain/road/LaneTypes.hpp"

#include <algorithm>
#include <cmath>

namespace infraforge::domain::road {

std::string_view laneSideName(const LaneSide side) noexcept {
    switch (side) {
    case LaneSide::Left:
        return "left";
    case LaneSide::Right:
        return "right";
    }
    return "right";
}

std::optional<LaneSide> laneSideFromName(const std::string_view name) noexcept {
    if (name == "left") return LaneSide::Left;
    if (name == "right") return LaneSide::Right;
    return std::nullopt;
}

std::string_view laneTypeName(const LaneType type) noexcept {
    switch (type) {
    case LaneType::Driving:
        return "driving";
    case LaneType::Shoulder:
        return "shoulder";
    case LaneType::Sidewalk:
        return "sidewalk";
    case LaneType::Biking:
        return "biking";
    case LaneType::Parking:
        return "parking";
    case LaneType::Median:
        return "median";
    case LaneType::Border:
        return "border";
    }
    return "driving";
}

std::optional<LaneType> laneTypeFromName(const std::string_view name) noexcept {
    if (name == "driving") return LaneType::Driving;
    if (name == "shoulder") return LaneType::Shoulder;
    if (name == "sidewalk") return LaneType::Sidewalk;
    if (name == "biking") return LaneType::Biking;
    if (name == "parking") return LaneType::Parking;
    if (name == "median") return LaneType::Median;
    if (name == "border") return LaneType::Border;
    return std::nullopt;
}

std::string_view laneDirectionName(const LaneDirection direction) noexcept {
    switch (direction) {
    case LaneDirection::Forward:
        return "forward";
    case LaneDirection::Backward:
        return "backward";
    case LaneDirection::Bidirectional:
        return "bidirectional";
    }
    return "forward";
}

std::optional<LaneDirection> laneDirectionFromName(const std::string_view name) noexcept {
    if (name == "forward") return LaneDirection::Forward;
    if (name == "backward") return LaneDirection::Backward;
    if (name == "bidirectional") return LaneDirection::Bidirectional;
    return std::nullopt;
}

std::vector<RoadDiagnostic> validateLaneSections(
    const double totalLength,
    const std::vector<RoadLaneSection>& sections) noexcept {
    std::vector<RoadDiagnostic> diagnostics;

    if (sections.empty()) {
        return diagnostics; // Empty is permitted (defaults are synthesized)
    }

    if (!std::isfinite(totalLength) || totalLength <= 0.0) {
        diagnostics.push_back({
            .code = RoadErrorCode::NonFiniteParameter,
            .message = "Road total length must be positive and finite for lane sections",
        });
        return diagnostics;
    }

    double previousEnd = 0.0;
    for (std::size_t i = 0; i < sections.size(); ++i) {
        const auto& sec = sections[i];
        if (!std::isfinite(sec.startStation) || !std::isfinite(sec.endStation)) {
            diagnostics.push_back({
                .code = RoadErrorCode::NonFiniteParameter,
                .message = "Lane section stations must be finite",
            });
            continue;
        }

        if (sec.startStation < -1e-4 || sec.endStation > totalLength + 1e-4) {
            diagnostics.push_back({
                .code = RoadErrorCode::AnchorOutOfRange,
                .message = "Lane section stations [" + std::to_string(sec.startStation) + ", " +
                    std::to_string(sec.endStation) + "] out of road bounds [0, " +
                    std::to_string(totalLength) + "]",
            });
        }

        if (sec.endStation < sec.startStation) {
            diagnostics.push_back({
                .code = RoadErrorCode::StationDiscontinuity,
                .message = "Lane section end station cannot precede start station",
            });
        }

        if (i > 0 && sec.startStation < previousEnd - 1e-4) {
            diagnostics.push_back({
                .code = RoadErrorCode::StationDiscontinuity,
                .message = "Lane section overlaps previous section",
            });
        }
        previousEnd = sec.endStation;

        auto validateLanes = [&](const std::vector<RoadLane>& lanes, LaneSide expectedSide) {
            std::uint32_t expectedIndex = 1;
            for (const auto& lane : lanes) {
                if (lane.side != expectedSide) {
                    diagnostics.push_back({
                        .code = RoadErrorCode::InvalidArgument,
                        .message = "Lane side mismatch in lane section",
                    });
                }
                if (lane.laneIndex != expectedIndex) {
                    diagnostics.push_back({
                        .code = RoadErrorCode::InvalidArgument,
                        .message = "Lane indices must be sequential 1-based outward from centerline",
                    });
                }
                ++expectedIndex;

                if (!std::isfinite(lane.width) || lane.width < 0.0) {
                    diagnostics.push_back({
                        .code = RoadErrorCode::NonFiniteParameter,
                        .message = "Lane width must be non-negative and finite",
                    });
                }
            }
        };

        validateLanes(sec.leftLanes, LaneSide::Left);
        validateLanes(sec.rightLanes, LaneSide::Right);
    }

    return diagnostics;
}

} // namespace infraforge::domain::road
