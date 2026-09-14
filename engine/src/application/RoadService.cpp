#include "infraforge/application/RoadService.hpp"

#include "infraforge/domain/road/AlignmentFitter.hpp"
#include "infraforge/domain/road/Road.hpp"
#include "infraforge/domain/road/RoadRecord.hpp"
#include "infraforge/runtime/Logging.hpp"
#include "infraforge/runtime/Uuid.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace infraforge::application {
namespace {

using namespace infraforge::domain::road;
using namespace infraforge::domain::world;

// Converts a RoadSegmentRecord to a segment info string.
std::string segmentKindName(AlignmentSegmentKind kind) {
    switch (kind) {
    case AlignmentSegmentKind::Line: return "line";
    case AlignmentSegmentKind::CircularArc: return "arc";
    case AlignmentSegmentKind::Clothoid: return "clothoid";
    }
    return "unknown";
}

// Converts a SourceProvider to a display string.
std::string providerName(SourceProvider provider) {
    switch (provider) {
    case SourceProvider::Authored: return "authored";
    case SourceProvider::Osm: return "osm";
    case SourceProvider::OpenDrive: return "opendrive";
    case SourceProvider::Other: return "other";
    }
    return "unknown";
}

// Generates a new RoadId.
RoadId generateRoadId() {
    return roadIdFromUuidText(runtime::generateUuidV4());
}

} // namespace

RoadService::RoadService(ports::ProjectStore& store, WorldState& world, EventSink eventSink)
    : store_(store), world_(world), eventSink_(std::move(eventSink)) {}

void RoadService::onProjectOpened() {
    // Load existing roads into the world partition index.
    auto roads = store_.roads();
    for (const auto& road : roads) {
        const auto bounds = computeRoadBounds(road);
        if (world_.isReady()) {
            (void)world_.insert(
                road.id,
                bounds,
                InvalidationMask::of(InvalidationClass::Road));
        }
    }
}

void RoadService::onProjectClosed() {
    undoStacks_.clear();
    redoStacks_.clear();
}

RoadSummary RoadService::createRoad(const CreateRoadInput& input) {
    // Validate input.
    if (input.sourcePoints.size() < 2) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "road create requires at least 2 source points"};
    }
    for (const auto& pt : input.sourcePoints) {
        if (!std::isfinite(pt.easting) || !std::isfinite(pt.northing)) {
            throw CommandFailure{CommandFailureCode::InvalidArgument,
                "road source point has non-finite coordinates"};
        }
    }
    if (input.positionTolerance <= 0.0 || !std::isfinite(input.positionTolerance)) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "position tolerance must be positive and finite"};
    }

    // Build conditioned polyline.
    std::vector<ConditionedVertex> polyline;
    polyline.reserve(input.sourcePoints.size());
    for (std::size_t i = 0; i < input.sourcePoints.size(); ++i) {
        ConditionedVertex v;
        v.position = input.sourcePoints[i];
        polyline.push_back(v);
    }

    // Build protected anchors from indices.
    std::vector<ProtectedAnchor> anchors;
    for (std::uint32_t idx : input.protectedAnchorIndices) {
        if (idx >= input.sourcePoints.size()) {
            throw CommandFailure{CommandFailureCode::InvalidArgument,
                "protected anchor index out of range"};
        }
        ProtectedAnchor anchor;
        anchor.position = input.sourcePoints[idx];
        anchor.kind = AnchorKind::Endpoint;
        anchors.push_back(anchor);
    }

    // Fit the road.
    auto record = fitRoad(input.name, polyline, anchors,
        input.positionTolerance, input.maxCurvature);

    // Persist.
    record = store_.insertRoad(record);

    // Register in world partition.
    const auto bounds = computeRoadBounds(record);
    std::vector<ChunkCoord> affectedChunks;
    if (world_.isReady()) {
        auto mutation = world_.insert(
            record.id,
            bounds,
            InvalidationMask::of(InvalidationClass::Road));
        affectedChunks = mutation.dirtyChunks;
    }

    // Record history (create: nothing before).
    recordHistory(HistoryEntry{
        uuidTextFromRoadId(record.id), std::nullopt, record, false});

    // Emit event.
    eventSink_(RoadServiceEvent{
        RoadServiceEvent::Kind::Created, uuidTextFromRoadId(record.id),
        store_.current().revision, affectedChunks});

    return toSummary(record);
}

RoadSummary RoadService::deleteRoad(const std::string& roadId) {
    auto roads = store_.roads();
    auto it = std::find_if(roads.begin(), roads.end(),
        [&](const RoadRecord& r) { return uuidTextFromRoadId(r.id) == roadId; });
    if (it == roads.end()) {
        throw CommandFailure{CommandFailureCode::NotFound,
            "road not found: " + roadId};
    }

    auto record = *it;
    store_.removeRoad(roadId);

    // Remove from world partition.
    std::vector<ChunkCoord> affectedChunks;
    if (world_.isReady()) {
        auto mutation = world_.remove(
            record.id,
            InvalidationMask::of(InvalidationClass::Road));
        affectedChunks = mutation.dirtyChunks;
    }

    // Record history (delete: road existed before).
    recordHistory(HistoryEntry{
        roadId, record, std::nullopt, true});

    // Emit event.
    eventSink_(RoadServiceEvent{
        RoadServiceEvent::Kind::Removed, roadId,
        store_.current().revision, affectedChunks});

    return toSummary(record);
}

RoadSummary RoadService::renameRoad(const std::string& roadId, const std::string& name) {
    auto roads = store_.roads();
    auto it = std::find_if(roads.begin(), roads.end(),
        [&](const RoadRecord& r) { return uuidTextFromRoadId(r.id) == roadId; });
    if (it == roads.end()) {
        throw CommandFailure{CommandFailureCode::NotFound,
            "road not found: " + roadId};
    }

    auto before = *it;
    auto record = before;
    record.displayName = name;
    record = store_.updateRoad(record);

    recordHistory(HistoryEntry{roadId, before, record, true});

    eventSink_(RoadServiceEvent{
        RoadServiceEvent::Kind::Updated, roadId,
        store_.current().revision, {}});

    return toSummary(record);
}

RoadSummary RoadService::insertControl(const InsertControlInput& input) {
    auto roads = store_.roads();
    auto it = std::find_if(roads.begin(), roads.end(),
        [&](const RoadRecord& r) { return uuidTextFromRoadId(r.id) == input.roadId; });
    if (it == roads.end()) {
        throw CommandFailure{CommandFailureCode::NotFound,
            "road not found: " + input.roadId};
    }

    auto before = *it;
    auto record = before;

    // Insert the new control point into the source vertices.
    if (input.insertBeforeIndex > record.sourceVertices.size()) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "insert index out of range"};
    }

    // Re-index source vertices.
    std::vector<RoadSourceVertexRecord> newVertices;
    for (std::size_t i = 0; i < input.insertBeforeIndex; ++i) {
        newVertices.push_back(record.sourceVertices[i]);
    }
    RoadSourceVertexRecord newVtx;
    newVtx.index = input.insertBeforeIndex;
    newVtx.x = input.position.easting;
    newVtx.y = input.position.northing;
    newVtx.z = input.elevation;
    newVertices.push_back(newVtx);
    for (std::size_t i = input.insertBeforeIndex; i < record.sourceVertices.size(); ++i) {
        auto v = record.sourceVertices[i];
        v.index = i + 1;
        newVertices.push_back(v);
    }
    record.sourceVertices = newVertices;

    // Refit the road.
    record = refitRoad(record, 1.0, std::nullopt);
    record = store_.updateRoad(record);

    // Update world partition.
    std::vector<ChunkCoord> affectedChunks;
    if (world_.isReady()) {
        auto bounds = computeRoadBounds(record);
        auto mutation = world_.update(
            record.id,
            bounds,
            InvalidationMask::of(InvalidationClass::Road));
        affectedChunks = mutation.dirtyChunks;
    }

    recordHistory(HistoryEntry{input.roadId, before, record, true});

    eventSink_(RoadServiceEvent{
        RoadServiceEvent::Kind::GeometryChanged, input.roadId,
        store_.current().revision, affectedChunks});

    return toSummary(record);
}

RoadSummary RoadService::moveControl(const MoveControlInput& input) {
    auto roads = store_.roads();
    auto it = std::find_if(roads.begin(), roads.end(),
        [&](const RoadRecord& r) { return uuidTextFromRoadId(r.id) == input.roadId; });
    if (it == roads.end()) {
        throw CommandFailure{CommandFailureCode::NotFound,
            "road not found: " + input.roadId};
    }

    auto before = *it;
    auto record = before;

    if (input.controlIndex >= record.sourceVertices.size()) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "control index out of range"};
    }

    record.sourceVertices[input.controlIndex].x = input.position.easting;
    record.sourceVertices[input.controlIndex].y = input.position.northing;
    record.sourceVertices[input.controlIndex].z = input.elevation;

    // Refit the road.
    record = refitRoad(record, 1.0, std::nullopt);
    record = store_.updateRoad(record);

    // Update world partition.
    std::vector<ChunkCoord> affectedChunks;
    if (world_.isReady()) {
        auto bounds = computeRoadBounds(record);
        auto mutation = world_.update(
            record.id,
            bounds,
            InvalidationMask::of(InvalidationClass::Road));
        affectedChunks = mutation.dirtyChunks;
    }

    recordHistory(HistoryEntry{input.roadId, before, record, true});

    eventSink_(RoadServiceEvent{
        RoadServiceEvent::Kind::GeometryChanged, input.roadId,
        store_.current().revision, affectedChunks});

    return toSummary(record);
}

RoadSummary RoadService::deleteControl(const DeleteControlInput& input) {
    auto roads = store_.roads();
    auto it = std::find_if(roads.begin(), roads.end(),
        [&](const RoadRecord& r) { return uuidTextFromRoadId(r.id) == input.roadId; });
    if (it == roads.end()) {
        throw CommandFailure{CommandFailureCode::NotFound,
            "road not found: " + input.roadId};
    }

    auto before = *it;
    auto record = before;

    if (record.sourceVertices.size() <= 2) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "cannot delete control: road must have at least 2 control points"};
    }
    if (input.controlIndex >= record.sourceVertices.size()) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "control index out of range"};
    }

    // Remove the control point and re-index.
    std::vector<RoadSourceVertexRecord> newVertices;
    for (std::size_t i = 0; i < record.sourceVertices.size(); ++i) {
        if (i == input.controlIndex) continue;
        auto v = record.sourceVertices[i];
        v.index = newVertices.size();
        newVertices.push_back(v);
    }
    record.sourceVertices = newVertices;

    // Refit the road.
    record = refitRoad(record, 1.0, std::nullopt);
    record = store_.updateRoad(record);

    // Update world partition.
    std::vector<ChunkCoord> affectedChunks;
    if (world_.isReady()) {
        auto bounds = computeRoadBounds(record);
        auto mutation = world_.update(
            record.id,
            bounds,
            InvalidationMask::of(InvalidationClass::Road));
        affectedChunks = mutation.dirtyChunks;
    }

    recordHistory(HistoryEntry{input.roadId, before, record, true});

    eventSink_(RoadServiceEvent{
        RoadServiceEvent::Kind::GeometryChanged, input.roadId,
        store_.current().revision, affectedChunks});

    return toSummary(record);
}

RoadSummary RoadService::fitSource(const FitSourceInput& input) {
    auto roads = store_.roads();
    auto it = std::find_if(roads.begin(), roads.end(),
        [&](const RoadRecord& r) { return uuidTextFromRoadId(r.id) == input.roadId; });
    if (it == roads.end()) {
        throw CommandFailure{CommandFailureCode::NotFound,
            "road not found: " + input.roadId};
    }

    auto before = *it;
    auto record = refitRoad(before, input.positionTolerance, input.maxCurvature);
    record = store_.updateRoad(record);

    // Update world partition.
    std::vector<ChunkCoord> affectedChunks;
    if (world_.isReady()) {
        auto bounds = computeRoadBounds(record);
        auto mutation = world_.update(
            record.id,
            bounds,
            InvalidationMask::of(InvalidationClass::Road));
        affectedChunks = mutation.dirtyChunks;
    }

    recordHistory(HistoryEntry{input.roadId, before, record, true});

    eventSink_(RoadServiceEvent{
        RoadServiceEvent::Kind::GeometryChanged, input.roadId,
        store_.current().revision, affectedChunks});

    return toSummary(record);
}

RoadSummary RoadService::updateElevation(const UpdateElevationInput& input) {
    auto roads = store_.roads();
    auto it = std::find_if(roads.begin(), roads.end(),
        [&](const RoadRecord& r) { return uuidTextFromRoadId(r.id) == input.roadId; });
    if (it == roads.end()) {
        throw CommandFailure{CommandFailureCode::NotFound,
            "road not found: " + input.roadId};
    }

    if (input.stations.size() != input.elevations.size()) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "stations and elevations must have the same length"};
    }

    auto before = *it;
    auto record = before;
    record.elevationBreakpoints.clear();
    for (std::size_t i = 0; i < input.stations.size(); ++i) {
        record.elevationBreakpoints.push_back(
            ProfileBreakpoint{input.stations[i], input.elevations[i]});
    }
    record = store_.updateRoad(record);

    recordHistory(HistoryEntry{input.roadId, before, record, true});

    eventSink_(RoadServiceEvent{
        RoadServiceEvent::Kind::Updated, input.roadId,
        store_.current().revision, {}});

    return toSummary(record);
}

RoadSummary RoadService::updateSuperelevation(const UpdateSuperelevationInput& input) {
    auto roads = store_.roads();
    auto it = std::find_if(roads.begin(), roads.end(),
        [&](const RoadRecord& r) { return uuidTextFromRoadId(r.id) == input.roadId; });
    if (it == roads.end()) {
        throw CommandFailure{CommandFailureCode::NotFound,
            "road not found: " + input.roadId};
    }

    if (input.stations.size() != input.superelevations.size()) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "stations and superelevations must have the same length"};
    }

    auto before = *it;
    auto record = before;
    record.superelevationBreakpoints.clear();
    for (std::size_t i = 0; i < input.stations.size(); ++i) {
        record.superelevationBreakpoints.push_back(
            ProfileBreakpoint{input.stations[i], input.superelevations[i]});
    }
    record = store_.updateRoad(record);

    recordHistory(HistoryEntry{input.roadId, before, record, true});

    eventSink_(RoadServiceEvent{
        RoadServiceEvent::Kind::Updated, input.roadId,
        store_.current().revision, {}});

    return toSummary(record);
}

bool RoadService::undo(const std::string& roadId) {
    auto& undoStack = undoStacks_[roadId];
    if (undoStack.empty()) return false;

    auto entry = std::move(undoStack.back());
    undoStack.pop_back();

    if (!entry.existedBefore && entry.after.has_value()) {
        // Undo of a create: remove the road.
        store_.removeRoad(roadId);
        if (world_.isReady()) {
            (void)world_.remove(
                roadIdFromUuidText(entry.roadId),
                InvalidationMask::of(InvalidationClass::Road));
        }
    } else if (entry.existedBefore && entry.before.has_value() && !entry.after.has_value()) {
        // Undo of a delete: re-insert the road.
        (void)store_.insertRoad(*entry.before);
        if (world_.isReady()) {
            auto bounds = computeRoadBounds(*entry.before);
            (void)world_.insert(
                entry.before->id,
                bounds,
                InvalidationMask::of(InvalidationClass::Road));
        }
    } else if (entry.existedBefore && entry.before.has_value()) {
        // Undo of an update: restore the previous state.
        (void)store_.updateRoad(*entry.before);
        if (world_.isReady()) {
            auto bounds = computeRoadBounds(*entry.before);
            (void)world_.update(
                entry.before->id,
                bounds,
                InvalidationMask::of(InvalidationClass::Road));
        }
    }

    redoStacks_[roadId].push_back(std::move(entry));

    eventSink_(RoadServiceEvent{
        RoadServiceEvent::Kind::Updated, roadId,
        store_.current().revision, {}});

    return true;
}

bool RoadService::redo(const std::string& roadId) {
    auto& redoStack = redoStacks_[roadId];
    if (redoStack.empty()) return false;

    auto entry = std::move(redoStack.back());
    redoStack.pop_back();

    if (entry.after.has_value()) {
        // Re-apply the after state.
        if (entry.existedBefore) {
            (void)store_.updateRoad(*entry.after);
        } else {
            // Road was created — re-insert.
            (void)store_.insertRoad(*entry.after);
        }
        if (world_.isReady()) {
            auto bounds = computeRoadBounds(*entry.after);
            if (entry.existedBefore) {
                (void)world_.update(
                    entry.after->id,
                    bounds,
                    InvalidationMask::of(InvalidationClass::Road));
            } else {
                (void)world_.insert(
                    entry.after->id,
                    bounds,
                    InvalidationMask::of(InvalidationClass::Road));
            }
        }
    } else {
        // The command was a delete — remove the road again.
        store_.removeRoad(roadId);
        if (world_.isReady()) {
            (void)world_.remove(
                roadIdFromUuidText(entry.roadId),
                InvalidationMask::of(InvalidationClass::Road));
        }
    }

    undoStacks_[roadId].push_back(std::move(entry));

    eventSink_(RoadServiceEvent{
        RoadServiceEvent::Kind::Updated, roadId,
        store_.current().revision, {}});

    return true;
}

bool RoadService::canUndo(const std::string& roadId) const {
    auto it = undoStacks_.find(roadId);
    return it != undoStacks_.end() && !it->second.empty();
}

bool RoadService::canRedo(const std::string& roadId) const {
    auto it = redoStacks_.find(roadId);
    return it != redoStacks_.end() && !it->second.empty();
}

std::vector<RoadSummary> RoadService::listRoads() const {
    auto roads = store_.roads();
    std::vector<RoadSummary> summaries;
    summaries.reserve(roads.size());
    for (const auto& road : roads) {
        summaries.push_back(toSummary(road));
    }
    return summaries;
}

std::optional<RoadDetails> RoadService::getRoad(const std::string& roadId) const {
    auto roads = store_.roads();
    auto it = std::find_if(roads.begin(), roads.end(),
        [&](const RoadRecord& r) { return uuidTextFromRoadId(r.id) == roadId; });
    if (it == roads.end()) return std::nullopt;
    return toDetails(*it);
}

std::optional<RoadSummary> RoadService::getRoadSummary(const std::string& roadId) const {
    auto roads = store_.roads();
    auto it = std::find_if(roads.begin(), roads.end(),
        [&](const RoadRecord& r) { return uuidTextFromRoadId(r.id) == roadId; });
    if (it == roads.end()) return std::nullopt;
    return toSummary(*it);
}

std::optional<domain::road::RoadTessellation> RoadService::getRoadTessellation(
    const std::string& roadId,
    const domain::road::RoadTessellationParams& params) const {
    auto roads = store_.roads();
    auto it = std::find_if(roads.begin(), roads.end(),
        [&](const RoadRecord& r) { return uuidTextFromRoadId(r.id) == roadId; });
    if (it == roads.end()) return std::nullopt;

    auto road = rebuildRoad(*it);
    return domain::road::tessellateRoad(
        road.alignment(), road.elevation(), road.superelevation(), params);
}

RoadSceneProjection RoadService::roadSceneProjection() const {
    RoadSceneProjection projection;
    const auto& record = store_.current();
    projection.originEasting = record.georeference.originEasting;
    projection.originNorthing = record.georeference.originNorthing;
    projection.originHeight = record.georeference.originHeight;
    projection.revision = record.revision;

    const auto roads = store_.roads();
    for (const auto& roadRecord : roads) {
        auto road = rebuildRoad(roadRecord);
        auto tess = domain::road::tessellateRoad(
            road.alignment(), road.elevation(), road.superelevation(), {});

        if (tess.isEmpty()) continue;

        RoadSceneMesh mesh;
        mesh.roadId = uuidTextFromRoadId(roadRecord.id);

        // Build vertices: left and right edge of each cross-section.
        // Vertex layout: for cross-section i, left = 2*i, right = 2*i+1.
        mesh.vertices.reserve(tess.crossSections.size() * 2);
        for (const auto& cs : tess.crossSections) {
            // Left edge vertex.
            RoadSceneVertex left;
            left.x = static_cast<float>(cs.leftEdge.easting - projection.originEasting);
            left.y = static_cast<float>(cs.leftEdge.northing - projection.originNorthing);
            left.z = static_cast<float>(cs.leftHeight - projection.originHeight);
            left.nx = 0.0f;
            left.ny = 0.0f;
            left.nz = 1.0f;
            mesh.vertices.push_back(left);

            // Right edge vertex.
            RoadSceneVertex right;
            right.x = static_cast<float>(cs.rightEdge.easting - projection.originEasting);
            right.y = static_cast<float>(cs.rightEdge.northing - projection.originNorthing);
            right.z = static_cast<float>(cs.rightHeight - projection.originHeight);
            right.nx = 0.0f;
            right.ny = 0.0f;
            right.nz = 1.0f;
            mesh.vertices.push_back(right);
        }

        mesh.indices = tess.indices;
        projection.meshes.push_back(std::move(mesh));
    }

    return projection;
}

void RoadService::recordHistory(HistoryEntry entry) {
    const auto roadId = entry.roadId;
    undoStacks_[roadId].push_back(std::move(entry));
    // Clear redo stack on new command.
    redoStacks_[roadId].clear();
}

SpatialBounds RoadService::computeRoadBounds(const RoadRecord& road) const {
    if (road.segments.empty()) {
        return SpatialBounds{0, 0, 0, 0};
    }
    double minE = std::numeric_limits<double>::max();
    double minN = std::numeric_limits<double>::max();
    double maxE = std::numeric_limits<double>::lowest();
    double maxN = std::numeric_limits<double>::lowest();
    for (const auto& seg : road.segments) {
        minE = std::min(minE, seg.start.easting);
        minN = std::min(minN, seg.start.northing);
        maxE = std::max(maxE, seg.start.easting);
        maxN = std::max(maxN, seg.start.northing);
        // Also include the segment end point for the last segment.
        if (&seg == &road.segments.back()) {
            // Compute the end point from the segment start + length * heading.
            // For accurate bounds, we use the segment's end point if available.
            // The segment start is the beginning; the end is start + length
            // along the segment direction. For bounds, we add the length as a
            // margin to ensure the end is covered.
            maxE = std::max(maxE, seg.start.easting + seg.length);
            maxN = std::max(maxN, seg.start.northing + seg.length);
            minE = std::min(minE, seg.start.easting - seg.length);
            minN = std::min(minN, seg.start.northing - seg.length);
        }
    }
    // Add a margin for the road width.
    const double margin = 10.0;
    return SpatialBounds{minE - margin, minN - margin, maxE + margin, maxN + margin};
}

RoadSummary RoadService::toSummary(const RoadRecord& road) const {
    RoadSummary s;
    s.roadId = uuidTextFromRoadId(road.id);
    s.name = road.displayName;
    s.alignmentSegmentCount = static_cast<std::uint32_t>(road.segments.size());
    s.sourceProvider = providerName(road.provider);
    s.sourceId = road.sourceId;
    s.protectedAnchorCount = static_cast<std::uint32_t>(road.protectedAnchors.size());
    s.revision = store_.current().revision;

    // Compute total length.
    double totalLength = 0.0;
    for (const auto& seg : road.segments) {
        totalLength += seg.length;
    }
    s.length = totalLength;
    return s;
}

RoadDetails RoadService::toDetails(const RoadRecord& road) const {
    RoadDetails d;
    d.roadId = uuidTextFromRoadId(road.id);
    d.name = road.displayName;
    d.alignmentSegmentCount = static_cast<std::uint32_t>(road.segments.size());
    d.hasElevationProfile = !road.elevationBreakpoints.empty();
    d.elevationBreakpointCount = static_cast<std::uint32_t>(road.elevationBreakpoints.size());
    d.hasSuperelevationProfile = !road.superelevationBreakpoints.empty();
    d.superelevationBreakpointCount = static_cast<std::uint32_t>(road.superelevationBreakpoints.size());
    d.sourceProvider = providerName(road.provider);
    d.sourceId = road.sourceId;
    d.sourceCrs = road.sourceCrs;
    d.protectedAnchorCount = static_cast<std::uint32_t>(road.protectedAnchors.size());
    d.revision = store_.current().revision;

    // Compute total length and segment info.
    double totalLength = 0.0;
    double station = 0.0;
    for (const auto& seg : road.segments) {
        RoadAlignmentSegmentInfo info;
        info.kind = segmentKindName(seg.kind);
        info.startStation = station;
        info.length = seg.length;
        info.startCurvature = seg.startCurvature;
        info.endCurvature = seg.endCurvature;
        if (seg.kind == AlignmentSegmentKind::CircularArc) {
            info.startCurvature = seg.curvature;
            info.endCurvature = seg.curvature;
        }
        d.alignmentSegments.push_back(info);
        totalLength += seg.length;
        station += seg.length;
    }
    d.length = totalLength;

    // Validate the road.
    try {
        auto roadObj = rebuildRoad(road);
        d.isValid = true;
        (void)roadObj;

        // Check for geometry issues.
        if (road.segments.empty()) {
            d.diagnostics.push_back({
                RoadErrorCode::EmptyAlignment,
                "Road has no alignment segments"});
        }
        if (totalLength < 1e-6) {
            d.diagnostics.push_back({
                RoadErrorCode::DegenerateSegment,
                "Road has zero length"});
        }
        // Check for curvature discontinuities at segment boundaries.
        for (std::size_t i = 1; i < road.segments.size(); ++i) {
            const auto& prev = road.segments[i - 1];
            const auto& curr = road.segments[i];
            const double prevEndCurv = (prev.kind == AlignmentSegmentKind::CircularArc)
                ? prev.curvature : prev.endCurvature;
            const double currStartCurv = (curr.kind == AlignmentSegmentKind::CircularArc)
                ? curr.curvature : curr.startCurvature;
            if (std::abs(prevEndCurv - currStartCurv) > 1e-3) {
                d.diagnostics.push_back({
                    RoadErrorCode::CurvatureDiscontinuity,
                    "Curvature discontinuity at station " + std::to_string(d.alignmentSegments[i].startStation)});
            }
        }
    } catch (const std::exception& e) {
        d.isValid = false;
        d.diagnostics.push_back({
            RoadErrorCode::PositionDiscontinuity,
            std::string("Road rebuild failed: ") + e.what()});
    }

    return d;
}

Road RoadService::rebuildRoad(const RoadRecord& record) const {
    auto result = fromRecord(record);
    if (!result.has_value()) {
        throw std::runtime_error("road rebuild failed");
    }
    return std::move(*result);
}

RoadRecord RoadService::fitRoad(
    const std::string& name,
    const std::vector<ConditionedVertex>& polyline,
    const std::vector<ProtectedAnchor>& anchors,
    double positionTolerance,
    std::optional<double> maxCurvature) const {

    AlignmentFitInput fitInput;
    fitInput.polyline = polyline;
    fitInput.protectedAnchors = anchors;
    fitInput.positionTolerance = positionTolerance;
    fitInput.maxCurvature = maxCurvature;

    auto fitResult = fitAlignment(fitInput);
    if (!fitResult.alignment.has_value()) {
        std::string msg = "road fit failed: ";
        for (const auto& d : fitResult.diagnostics) {
            msg += std::string(roadErrorCodeName(d.code)) + ": " + d.message + "; ";
        }
        throw CommandFailure{CommandFailureCode::InvalidArgument, msg};
    }

    // Build the Road domain object.
    Road::BuildInput roadInput;
    roadInput.id = generateRoadId();
    roadInput.displayName = name;
    roadInput.alignment = std::move(*fitResult.alignment);

    // Store source vertices.
    for (std::size_t i = 0; i < polyline.size(); ++i) {
        SourceVertex sv;
        sv.x = polyline[i].position.easting;
        sv.y = polyline[i].position.northing;
        roadInput.source.geometry.vertices.push_back(sv);
    }
    roadInput.source.provenance.provider = SourceProvider::Authored;
    roadInput.source.protectedAnchors = anchors;

    auto road = Road::build(std::move(roadInput));
    if (!road.has_value()) {
        std::string msg = "road build failed: ";
        for (const auto& d : road.error()) {
            msg += std::string(roadErrorCodeName(d.code)) + ": " + d.message + "; ";
        }
        throw CommandFailure{CommandFailureCode::InvalidArgument, msg};
    }

    return toRecord(*road);
}

RoadRecord RoadService::refitRoad(
    const RoadRecord& existing,
    double positionTolerance,
    std::optional<double> maxCurvature) const {

    // Rebuild conditioned polyline from source vertices.
    std::vector<ConditionedVertex> polyline;
    for (const auto& v : existing.sourceVertices) {
        ConditionedVertex cv;
        cv.position = AlignmentPoint{v.x, v.y};
        polyline.push_back(cv);
    }

    // Rebuild protected anchors.
    std::vector<ProtectedAnchor> anchors;
    for (const auto& a : existing.protectedAnchors) {
        ProtectedAnchor pa;
        pa.station = a.station;
        pa.position = a.position;
        pa.kind = a.kind;
        anchors.push_back(pa);
    }

    return fitRoad(existing.displayName, polyline, anchors,
        positionTolerance, maxCurvature);
}

} // namespace infraforge::application
