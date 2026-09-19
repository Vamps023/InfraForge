#include "infraforge/domain/road/JunctionRecord.hpp"

namespace infraforge::domain::road {

JunctionRecord toRecord(const Junction& junction) {
    JunctionRecord record;
    record.id = junction.id;
    record.name = junction.name;
    record.type = std::string{junctionTypeName(junction.type)};
    record.posX = junction.position.easting;
    record.posY = junction.position.northing;
    record.elevation = junction.elevation;
    record.revision = junction.revision;

    record.approaches.reserve(junction.approaches.size());
    for (const auto& app : junction.approaches) {
        JunctionApproachRecord ar;
        ar.roadId = uuidTextFromRoadId(app.roadId);
        ar.contactPoint = app.contactPoint;
        ar.entryPointX = app.entryPoint.easting;
        ar.entryPointY = app.entryPoint.northing;
        ar.heading = app.heading;
        ar.laneIds = app.laneIds;
        record.approaches.push_back(std::move(ar));
    }

    record.connections.reserve(junction.connections.size());
    for (const auto& conn : junction.connections) {
        JunctionConnectionRecord cr;
        cr.id = conn.id;
        cr.fromRoadId = uuidTextFromRoadId(conn.fromRoadId);
        cr.fromLaneId = conn.fromLaneId;
        cr.toRoadId = uuidTextFromRoadId(conn.toRoadId);
        cr.toLaneId = conn.toLaneId;
        cr.movementType = std::string{junctionMovementTypeName(conn.movementType)};
        cr.allowed = conn.allowed;
        record.connections.push_back(std::move(cr));
    }

    return record;
}

Junction fromRecord(const JunctionRecord& record) {
    Junction junction;
    junction.id = record.id;
    junction.name = record.name;
    junction.type = junctionTypeFromName(record.type).value_or(JunctionType::Custom);
    junction.position = {record.posX, record.posY};
    junction.elevation = record.elevation;
    junction.revision = record.revision;

    junction.approaches.reserve(record.approaches.size());
    for (const auto& ar : record.approaches) {
        JunctionApproach app;
        app.roadId = roadIdFromUuidText(ar.roadId);
        app.contactPoint = ar.contactPoint;
        app.entryPoint = {ar.entryPointX, ar.entryPointY};
        app.heading = ar.heading;
        app.laneIds = ar.laneIds;
        junction.approaches.push_back(std::move(app));
    }

    junction.connections.reserve(record.connections.size());
    for (const auto& cr : record.connections) {
        JunctionConnection conn;
        conn.id = cr.id;
        conn.fromRoadId = roadIdFromUuidText(cr.fromRoadId);
        conn.fromLaneId = cr.fromLaneId;
        conn.toRoadId = roadIdFromUuidText(cr.toRoadId);
        conn.toLaneId = cr.toLaneId;
        conn.movementType = junctionMovementTypeFromName(cr.movementType).value_or(JunctionMovementType::Straight);
        conn.allowed = cr.allowed;
        junction.connections.push_back(std::move(conn));
    }

    return junction;
}

} // namespace infraforge::domain::road
