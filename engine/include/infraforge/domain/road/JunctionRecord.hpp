#pragma once

#include "infraforge/domain/road/JunctionTypes.hpp"

#include <string>
#include <vector>

namespace infraforge::domain::road {

struct JunctionApproachRecord {
    std::string roadId;
    std::string contactPoint{"end"};
    double entryPointX{0.0};
    double entryPointY{0.0};
    double heading{0.0};
    std::vector<std::string> laneIds;

    friend bool operator==(const JunctionApproachRecord&, const JunctionApproachRecord&) = default;
};

struct JunctionConnectionRecord {
    std::string id;
    std::string fromRoadId;
    std::string fromLaneId;
    std::string toRoadId;
    std::string toLaneId;
    std::string movementType{"straight"};
    bool allowed{true};

    friend bool operator==(const JunctionConnectionRecord&, const JunctionConnectionRecord&) = default;
};

struct JunctionRecord {
    JunctionId id{};
    std::string name;
    std::string type{"custom"};
    double posX{0.0};
    double posY{0.0};
    double elevation{0.0};
    std::uint64_t revision{1};
    std::vector<JunctionApproachRecord> approaches;
    std::vector<JunctionConnectionRecord> connections;

    friend bool operator==(const JunctionRecord&, const JunctionRecord&) = default;
};

[[nodiscard]] JunctionRecord toRecord(const Junction& junction);
[[nodiscard]] Junction fromRecord(const JunctionRecord& record);

} // namespace infraforge::domain::road
