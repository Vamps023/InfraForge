#include "infraforge/application/RoadService.hpp"

#include "infraforge/domain/road/AlignmentFitter.hpp"
#include "infraforge/domain/road/AlignmentPrimitives.hpp"
#include "infraforge/domain/road/Road.hpp"
#include "infraforge/domain/road/RoadRecord.hpp"
#include "infraforge/domain/road/VerticalProfiles.hpp"
#include "infraforge/runtime/Logging.hpp"
#include "infraforge/runtime/Uuid.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
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

// Checks whether two road records have identical render-affecting geometry and profiles.
bool hasSameRenderGeometry(const RoadRecord& a, const RoadRecord& b) noexcept {
    return a.segments == b.segments
        && a.elevationBreakpoints == b.elevationBreakpoints
        && a.superelevationBreakpoints == b.superelevationBreakpoints
        && a.widthBreakpoints == b.widthBreakpoints;
}

} // namespace

RoadService::RoadService(ports::ProjectStore& store, WorldState& world, EventSink eventSink)
    : store_(store), world_(world), eventSink_(std::move(eventSink)) {}

void RoadService::onProjectOpened() {
    sceneMeshCache_.clear();
    // Load existing roads into the world partition index.
    auto roads = store_.roads();
    for (const auto& road : roads) {
        const auto bounds = computeRoadBounds(road);
        if (world_.isReady() && !world_.boundsOf(road.id).has_value()) {
            (void)world_.insert(
                road.id,
                bounds,
                InvalidationMask::of(InvalidationClass::Road));
        }
    }
}

void RoadService::onProjectClosed() {
    undoHistory_.clear();
    redoHistory_.clear();
    nextSequence_ = 1;
    sceneMeshCache_.clear();
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
    for (std::size_t i = 1; i < input.sourcePoints.size(); ++i) {
        const double dx = input.sourcePoints[i].easting - input.sourcePoints[i - 1].easting;
        const double dy = input.sourcePoints[i].northing - input.sourcePoints[i - 1].northing;
        if (std::hypot(dx, dy) < 1e-4) {
            throw CommandFailure{CommandFailureCode::InvalidArgument,
                "adjacent road source points are coincident at index " + std::to_string(i)};
        }
    }
    if (input.positionTolerance <= 0.0 || !std::isfinite(input.positionTolerance)) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "position tolerance must be positive and finite"};
    }
    if (input.maxCurvature.has_value() && (!std::isfinite(*input.maxCurvature) || *input.maxCurvature <= 0.0)) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "max curvature must be positive and finite"};
    }
    // Blocker 16: reject non-empty elevation arrays whose length does not
    // equal the control/source point count.
    if (!input.sourceElevations.empty() &&
        input.sourceElevations.size() != input.sourcePoints.size()) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "source elevations length must equal source point count when non-empty"};
    }
    // Blocker 16: validate supplied elevations are finite.
    for (std::size_t i = 0; i < input.sourceElevations.size(); ++i) {
        if (input.sourceElevations[i].has_value() &&
            !std::isfinite(*input.sourceElevations[i])) {
            throw CommandFailure{CommandFailureCode::InvalidArgument,
                "road source elevation " + std::to_string(i) +
                " has non-finite value"};
        }
    }

    // Build conditioned polyline.
    std::vector<ConditionedVertex> polyline;
    polyline.reserve(input.sourcePoints.size());
    for (std::size_t i = 0; i < input.sourcePoints.size(); ++i) {
        ConditionedVertex v;
        v.position = input.sourcePoints[i];
        polyline.push_back(v);
    }

    // Build protected anchors from indices, assigning cumulative source
    // stations so anchors have meaningful reference data before fitting
    // (Blocker 4: anchors need deterministic station information).
    std::vector<ProtectedAnchor> anchors;
    for (std::uint32_t idx : input.protectedAnchorIndices) {
        if (idx >= input.sourcePoints.size()) {
            throw CommandFailure{CommandFailureCode::InvalidArgument,
                "protected anchor index out of range"};
        }
        ProtectedAnchor anchor;
        anchor.position = input.sourcePoints[idx];
        anchor.kind = AnchorKind::Endpoint;
        // Compute cumulative station along the source polyline up to this
        // index so the anchor's station is meaningful for validation.
        double cumulativeStation = 0.0;
        for (std::size_t j = 1; j <= idx && j < input.sourcePoints.size(); ++j) {
            const double dx = input.sourcePoints[j].easting -
                input.sourcePoints[j - 1].easting;
            const double dy = input.sourcePoints[j].northing -
                input.sourcePoints[j - 1].northing;
            cumulativeStation += std::sqrt(dx * dx + dy * dy);
        }
        anchor.station = cumulativeStation;
        anchors.push_back(anchor);
    }

    // Fit the alignment (pure geometry, no entity creation).
    auto fitResult = fitAlignmentOnly(polyline, anchors,
        input.positionTolerance, input.maxCurvature);
    if (!fitResult.alignment.has_value()) {
        std::string msg = "road fit failed: ";
        for (const auto& d : fitResult.diagnostics) {
            msg += std::string(roadErrorCodeName(d.code)) + ": " + d.message + "; ";
        }
        throw CommandFailure{CommandFailureCode::InvalidArgument, msg};
    }

    // Build the new road record (mints a new RoadId — the ONLY place that does).
    auto record = buildNewRoadRecord(input.name, *fitResult.alignment,
        polyline, anchors, input.sourceElevations, SourceProvider::Authored,
        input.positionTolerance, input.maxCurvature,
        fitResult.anchorBoundarySegments);

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
        .roadId = uuidTextFromRoadId(record.id), .before = std::nullopt, .after = record, .existedBefore = false});

    // Emit event.
    eventSink_(RoadServiceEvent{
        RoadServiceEvent::Kind::Created, uuidTextFromRoadId(record.id),
        store_.current().revision, affectedChunks});

    return toSummary(record);
}

RoadSummary RoadService::createStraightRoad(
    const CreateStraightRoadInput& input, const TerrainHeightSampler& sampleHeight) {
    if (input.name.empty()) {
        throw CommandFailure{CommandFailureCode::InvalidArgument, "road name must not be empty"};
    }
    auto segmentRes = constructStraightSegment(input.start, input.end);
    if (!segmentRes.has_value()) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "failed to construct straight segment: " + segmentRes.error().message};
    }
    std::vector<AlignmentSegment> segments;
    segments.emplace_back(std::move(*segmentRes));
    auto alignmentRes = ReferenceAlignment::build(std::move(segments));
    if (!alignmentRes.has_value()) {
        std::string msg = "failed to build straight alignment: ";
        for (const auto& d : alignmentRes.error()) {
            msg += d.message + "; ";
        }
        throw CommandFailure{CommandFailureCode::InvalidArgument, msg};
    }

    std::vector<ConditionedVertex> polyline;
    ConditionedVertex v0; v0.position = input.start; v0.sourceStation = 0.0; polyline.push_back(v0);
    ConditionedVertex v1; v1.position = input.end; v1.sourceStation = alignmentRes->totalLength(); polyline.push_back(v1);

    std::vector<ProtectedAnchor> anchors;

    std::vector<ProfileBreakpoint> conformedElevation;
    if (input.stickToTerrain) {
        conformedElevation = sampleElevationAlongAlignment(
            *alignmentRes, input.stationInterval, input.verticalOffset, sampleHeight);
    }

    auto record = buildNewRoadRecord(input.name, *alignmentRes, polyline, anchors,
        {}, SourceProvider::Authored, 1.0, std::nullopt, {}, conformedElevation,
        RoadConstructionKind::Straight);
    record = store_.insertRoad(record);

    const auto bounds = computeRoadBounds(record);
    std::vector<ChunkCoord> affectedChunks;
    if (world_.isReady()) {
        auto mutation = world_.insert(record.id, bounds, InvalidationMask::of(InvalidationClass::Road));
        affectedChunks = mutation.dirtyChunks;
    }
    recordHistory(HistoryEntry{
        .roadId = uuidTextFromRoadId(record.id), .before = std::nullopt, .after = record, .existedBefore = false});
    eventSink_(RoadServiceEvent{
        RoadServiceEvent::Kind::Created, uuidTextFromRoadId(record.id),
        store_.current().revision, affectedChunks});
    return toSummary(record);
}

RoadSummary RoadService::createArcRoad(
    const CreateArcRoadInput& input, const TerrainHeightSampler& sampleHeight) {
    if (input.name.empty()) {
        throw CommandFailure{CommandFailureCode::InvalidArgument, "road name must not be empty"};
    }
    auto segmentRes = constructCircularArcThroughPoints(input.p0, input.p1, input.p2);
    if (!segmentRes.has_value()) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "failed to construct circular arc: " + segmentRes.error().message};
    }
    std::vector<AlignmentSegment> segments;
    segments.emplace_back(std::move(*segmentRes));
    auto alignmentRes = ReferenceAlignment::build(std::move(segments));
    if (!alignmentRes.has_value()) {
        std::string msg = "failed to build arc alignment: ";
        for (const auto& d : alignmentRes.error()) {
            msg += d.message + "; ";
        }
        throw CommandFailure{CommandFailureCode::InvalidArgument, msg};
    }

    std::vector<ConditionedVertex> polyline;
    ConditionedVertex v0; v0.position = input.p0; v0.sourceStation = 0.0; polyline.push_back(v0);
    ConditionedVertex v1; v1.position = input.p1; v1.sourceStation = alignmentRes->totalLength() * 0.5; polyline.push_back(v1);
    ConditionedVertex v2; v2.position = input.p2; v2.sourceStation = alignmentRes->totalLength(); polyline.push_back(v2);

    std::vector<ProtectedAnchor> anchors;

    std::vector<ProfileBreakpoint> conformedElevation;
    if (input.stickToTerrain) {
        conformedElevation = sampleElevationAlongAlignment(
            *alignmentRes, input.stationInterval, input.verticalOffset, sampleHeight);
    }

    auto record = buildNewRoadRecord(input.name, *alignmentRes, polyline, anchors,
        {}, SourceProvider::Authored, 1.0, std::nullopt, {}, conformedElevation,
        RoadConstructionKind::ArcThreePoint);
    record = store_.insertRoad(record);

    const auto bounds = computeRoadBounds(record);
    std::vector<ChunkCoord> affectedChunks;
    if (world_.isReady()) {
        auto mutation = world_.insert(record.id, bounds, InvalidationMask::of(InvalidationClass::Road));
        affectedChunks = mutation.dirtyChunks;
    }
    recordHistory(HistoryEntry{
        .roadId = uuidTextFromRoadId(record.id), .before = std::nullopt, .after = record, .existedBefore = false});
    eventSink_(RoadServiceEvent{
        RoadServiceEvent::Kind::Created, uuidTextFromRoadId(record.id),
        store_.current().revision, affectedChunks});
    return toSummary(record);
}

RoadSummary RoadService::createClothoidRoad(
    const CreateClothoidRoadInput& input, const TerrainHeightSampler& sampleHeight) {
    if (input.name.empty()) {
        throw CommandFailure{CommandFailureCode::InvalidArgument, "road name must not be empty"};
    }
    auto segmentRes = constructClothoidSegment(
        input.start, input.startHeading, input.startCurvature, input.endCurvature, input.length);
    if (!segmentRes.has_value()) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "failed to construct clothoid segment: " + segmentRes.error().message};
    }
    std::vector<AlignmentSegment> segments;
    segments.emplace_back(std::move(*segmentRes));
    auto alignmentRes = ReferenceAlignment::build(std::move(segments));
    if (!alignmentRes.has_value()) {
        std::string msg = "failed to build clothoid alignment: ";
        for (const auto& d : alignmentRes.error()) {
            msg += d.message + "; ";
        }
        throw CommandFailure{CommandFailureCode::InvalidArgument, msg};
    }

    const auto endSample = alignmentRes->evaluate(alignmentRes->totalLength());
    std::vector<ConditionedVertex> polyline;
    ConditionedVertex v0; v0.position = input.start; v0.sourceStation = 0.0; polyline.push_back(v0);
    ConditionedVertex v1; v1.position = endSample.position; v1.sourceStation = alignmentRes->totalLength(); polyline.push_back(v1);

    std::vector<ProtectedAnchor> anchors;

    std::vector<ProfileBreakpoint> conformedElevation;
    if (input.stickToTerrain) {
        conformedElevation = sampleElevationAlongAlignment(
            *alignmentRes, input.stationInterval, input.verticalOffset, sampleHeight);
    }

    auto record = buildNewRoadRecord(input.name, *alignmentRes, polyline, anchors,
        {}, SourceProvider::Authored, 1.0, std::nullopt, {}, conformedElevation,
        RoadConstructionKind::Clothoid);
    record = store_.insertRoad(record);

    const auto bounds = computeRoadBounds(record);
    std::vector<ChunkCoord> affectedChunks;
    if (world_.isReady()) {
        auto mutation = world_.insert(record.id, bounds, InvalidationMask::of(InvalidationClass::Road));
        affectedChunks = mutation.dirtyChunks;
    }
    recordHistory(HistoryEntry{
        .roadId = uuidTextFromRoadId(record.id), .before = std::nullopt, .after = record, .existedBefore = false});
    eventSink_(RoadServiceEvent{
        RoadServiceEvent::Kind::Created, uuidTextFromRoadId(record.id),
        store_.current().revision, affectedChunks});
    return toSummary(record);
}

RoadSummary RoadService::deleteRoad(const std::string& roadId) {
    auto found = findRoad(roadId);
    if (!found.has_value()) {
        throw CommandFailure{CommandFailureCode::NotFound,
            "road not found: " + roadId};
    }

    auto record = *found;
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
        .roadId = roadId, .before = record, .after = std::nullopt, .existedBefore = true});

    // Emit event.
    eventSink_(RoadServiceEvent{
        RoadServiceEvent::Kind::Removed, roadId,
        store_.current().revision, affectedChunks});

    return toSummary(record);
}

RoadSummary RoadService::renameRoad(const std::string& roadId, const std::string& name) {
    auto found = findRoad(roadId);
    if (!found.has_value()) {
        throw CommandFailure{CommandFailureCode::NotFound,
            "road not found: " + roadId};
    }

    auto before = *found;
    auto record = before;
    record.displayName = name;
    record = store_.updateRoad(record);

    recordHistory(HistoryEntry{.roadId = roadId, .before = before, .after = record, .existedBefore = true});

    // Rename is a metadata-only change: no geometry rebuild, no chunk
    // dirtying (Blocker 7: rename-only changes must not rebuild geometry).
    eventSink_(RoadServiceEvent{
        RoadServiceEvent::Kind::Updated, roadId,
        store_.current().revision, {}});

    return toSummary(record);
}

RoadSummary RoadService::insertControl(const InsertControlInput& input) {
    auto found = findRoad(input.roadId);
    if (!found.has_value()) {
        throw CommandFailure{CommandFailureCode::NotFound,
            "road not found: " + input.roadId};
    }

    if (!std::isfinite(input.position.easting) || !std::isfinite(input.position.northing)) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "insert control point has non-finite coordinates"};
    }
    if (input.elevation.has_value() && !std::isfinite(*input.elevation)) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "insert control point has non-finite elevation"};
    }

    auto before = *found;
    auto record = before;

    // Insert the new control point into the editable control vertices
    // (not the immutable source evidence). Source vertices are preserved
    // unchanged as the original imported/authored evidence (Blocker 6).
    if (input.insertBeforeIndex > record.controlVertices.size()) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "insert index out of range"};
    }

    if (input.insertBeforeIndex > 0) {
        const auto& prev = record.controlVertices[input.insertBeforeIndex - 1];
        if (std::hypot(input.position.easting - prev.x, input.position.northing - prev.y) < 1e-4) {
            throw CommandFailure{CommandFailureCode::InvalidArgument,
                "inserted control point is coincident with previous control point"};
        }
    }
    if (input.insertBeforeIndex < record.controlVertices.size()) {
        const auto& next = record.controlVertices[input.insertBeforeIndex];
        if (std::hypot(input.position.easting - next.x, input.position.northing - next.y) < 1e-4) {
            throw CommandFailure{CommandFailureCode::InvalidArgument,
                "inserted control point is coincident with next control point"};
        }
    }

    // Re-index control vertices.
    std::vector<RoadSourceVertexRecord> newVertices;
    for (std::size_t i = 0; i < input.insertBeforeIndex; ++i) {
        newVertices.push_back(record.controlVertices[i]);
    }
    RoadSourceVertexRecord newVtx;
    newVtx.index = input.insertBeforeIndex;
    newVtx.x = input.position.easting;
    newVtx.y = input.position.northing;
    newVtx.z = input.elevation;
    newVertices.push_back(newVtx);
    for (std::size_t i = input.insertBeforeIndex; i < record.controlVertices.size(); ++i) {
        auto v = record.controlVertices[i];
        v.index = i + 1;
        newVertices.push_back(v);
    }
    record.controlVertices = newVertices;

    // Refit the road — preserves RoadId, profiles, provenance (Blocker 1+2).
    // Blocker 7: reuse the road's persisted fitting contract instead of a
    // magic constant. Control edits change the source polyline, so the
    // refit uses the same position tolerance the road was created with.
    record = refitRoad(record, record.positionTolerance, record.maxCurvature);
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

    recordHistory(HistoryEntry{.roadId = input.roadId, .before = before, .after = record, .existedBefore = true});

    eventSink_(RoadServiceEvent{
        RoadServiceEvent::Kind::GeometryChanged, input.roadId,
        store_.current().revision, affectedChunks});

    return toSummary(record);
}

RoadSummary RoadService::moveControl(const MoveControlInput& input) {
    auto found = findRoad(input.roadId);
    if (!found.has_value()) {
        throw CommandFailure{CommandFailureCode::NotFound,
            "road not found: " + input.roadId};
    }

    if (!std::isfinite(input.position.easting) || !std::isfinite(input.position.northing)) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "move control point has non-finite coordinates"};
    }
    if (input.elevation.has_value() && !std::isfinite(*input.elevation)) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "move control point has non-finite elevation"};
    }

    auto before = *found;
    auto record = before;

    if (input.controlIndex >= record.controlVertices.size()) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "control index out of range"};
    }

    if (input.controlIndex > 0) {
        const auto& prev = record.controlVertices[input.controlIndex - 1];
        if (std::hypot(input.position.easting - prev.x, input.position.northing - prev.y) < 1e-4) {
            throw CommandFailure{CommandFailureCode::InvalidArgument,
                "moved control point is coincident with previous control point"};
        }
    }
    if (input.controlIndex + 1 < record.controlVertices.size()) {
        const auto& next = record.controlVertices[input.controlIndex + 1];
        if (std::hypot(input.position.easting - next.x, input.position.northing - next.y) < 1e-4) {
            throw CommandFailure{CommandFailureCode::InvalidArgument,
                "moved control point is coincident with next control point"};
        }
    }

    // Blocker 4: do not permit normal move operations to move a protected
    // topology anchor unless the command explicitly represents a topology
    // mutation. A move of a protected anchor is rejected.
    for (const auto& anchor : record.protectedAnchors) {
        // Check if this control index corresponds to a protected anchor by
        // matching position. Protected anchors are reference data; moving
        // the control vertex at the same position would displace the anchor.
        const auto& vtx = record.controlVertices[input.controlIndex];
        const double dx = vtx.x - anchor.position.easting;
        const double dy = vtx.y - anchor.position.northing;
        if (std::sqrt(dx * dx + dy * dy) < 1e-9) {
            throw CommandFailure{CommandFailureCode::InvalidArgument,
                "cannot move a protected anchor (index " +
                std::to_string(input.controlIndex) + ")"};
        }
    }

    record.controlVertices[input.controlIndex].x = input.position.easting;
    record.controlVertices[input.controlIndex].y = input.position.northing;
    record.controlVertices[input.controlIndex].z = input.elevation;

    // Refit the road — preserves RoadId, profiles, provenance (Blocker 1+2).
    // Blocker 7: reuse the road's persisted fitting contract.
    record = refitRoad(record, record.positionTolerance, record.maxCurvature);
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

    recordHistory(HistoryEntry{.roadId = input.roadId, .before = before, .after = record, .existedBefore = true});

    eventSink_(RoadServiceEvent{
        RoadServiceEvent::Kind::GeometryChanged, input.roadId,
        store_.current().revision, affectedChunks});

    return toSummary(record);
}

RoadSummary RoadService::deleteControl(const DeleteControlInput& input) {
    auto found = findRoad(input.roadId);
    if (!found.has_value()) {
        throw CommandFailure{CommandFailureCode::NotFound,
            "road not found: " + input.roadId};
    }

    auto before = *found;
    auto record = before;

    if (record.controlVertices.size() <= 2) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "cannot delete control: road must have at least 2 control points"};
    }
    if (input.controlIndex >= record.controlVertices.size()) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "control index out of range"};
    }

    // Blocker 4: do not permit deleting a protected topology anchor.
    for (const auto& anchor : record.protectedAnchors) {
        const auto& vtx = record.controlVertices[input.controlIndex];
        const double dx = vtx.x - anchor.position.easting;
        const double dy = vtx.y - anchor.position.northing;
        if (std::sqrt(dx * dx + dy * dy) < 1e-9) {
            throw CommandFailure{CommandFailureCode::InvalidArgument,
                "cannot delete a protected anchor (index " +
                std::to_string(input.controlIndex) + ")"};
        }
    }

    // Remove the control point from the editable control vertices (not
    // the immutable source evidence) and re-index.
    std::vector<RoadSourceVertexRecord> newVertices;
    for (std::size_t i = 0; i < record.controlVertices.size(); ++i) {
        if (i == input.controlIndex) continue;
        auto v = record.controlVertices[i];
        v.index = newVertices.size();
        newVertices.push_back(v);
    }
    record.controlVertices = newVertices;

    // Refit the road — preserves RoadId, profiles, provenance (Blocker 1+2).
    // Blocker 7: reuse the road's persisted fitting contract.
    record = refitRoad(record, record.positionTolerance, record.maxCurvature);
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

    recordHistory(HistoryEntry{.roadId = input.roadId, .before = before, .after = record, .existedBefore = true});

    eventSink_(RoadServiceEvent{
        RoadServiceEvent::Kind::GeometryChanged, input.roadId,
        store_.current().revision, affectedChunks});

    return toSummary(record);
}

RoadSummary RoadService::fitSource(const FitSourceInput& input) {
    auto found = findRoad(input.roadId);
    if (!found.has_value()) {
        throw CommandFailure{CommandFailureCode::NotFound,
            "road not found: " + input.roadId};
    }

    auto before = *found;
    const double positionTolerance = input.positionTolerance.value_or(before.positionTolerance);
    if (positionTolerance <= 0.0 || !std::isfinite(positionTolerance)) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "position tolerance must be positive and finite"};
    }
    const auto maxCurvature = input.replaceMaxCurvature
        ? input.maxCurvature : before.maxCurvature;
    if (maxCurvature.has_value() && (!std::isfinite(*maxCurvature) || *maxCurvature <= 0.0)) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "max curvature must be positive and finite"};
    }
    auto record = refitRoad(before, positionTolerance, maxCurvature);
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

    recordHistory(HistoryEntry{.roadId = input.roadId, .before = before, .after = record, .existedBefore = true});

    eventSink_(RoadServiceEvent{
        RoadServiceEvent::Kind::GeometryChanged, input.roadId,
        store_.current().revision, affectedChunks});

    return toSummary(record);
}

RoadSummary RoadService::updateElevation(const UpdateElevationInput& input) {
    auto found = findRoad(input.roadId);
    if (!found.has_value()) {
        throw CommandFailure{CommandFailureCode::NotFound,
            "road not found: " + input.roadId};
    }

    if (input.stations.size() != input.elevations.size()) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "stations and elevations must have the same length"};
    }

    // Blocker 15/19: validate profile data before persistence. Stations
    // must be within the alignment's station range [0, totalLength].
    // Compute total length from segments (RoadRecord has no alignment).
    double alignmentLength = 0.0;
    for (const auto& seg : found->segments) {
        alignmentLength += seg.length;
    }
    for (std::size_t i = 0; i < input.stations.size(); ++i) {
        if (!std::isfinite(input.stations[i])) {
            throw CommandFailure{CommandFailureCode::InvalidArgument,
                "elevation station " + std::to_string(i) + " is not finite"};
        }
        if (input.stations[i] < 0.0 || input.stations[i] > alignmentLength) {
            throw CommandFailure{CommandFailureCode::InvalidArgument,
                "elevation station " + std::to_string(i) + " (" +
                std::to_string(input.stations[i]) + ") is outside the alignment range [0, " +
                std::to_string(alignmentLength) + "]"};
        }
        if (!std::isfinite(input.elevations[i])) {
            throw CommandFailure{CommandFailureCode::InvalidArgument,
                "elevation value " + std::to_string(i) + " is not finite"};
        }
        if (i > 0 && input.stations[i] <= input.stations[i - 1]) {
            throw CommandFailure{CommandFailureCode::InvalidArgument,
                "elevation stations must be strictly increasing (failure at index " +
                std::to_string(i) + ")"};
        }
    }

    auto before = *found;
    auto record = before;
    record.elevationBreakpoints.clear();
    for (std::size_t i = 0; i < input.stations.size(); ++i) {
        record.elevationBreakpoints.push_back(
            ProfileBreakpoint{input.stations[i], input.elevations[i]});
    }

    // Blocker 15: rebuild/validate the canonical Road to check invariants.
    try {
        auto road = rebuildRoad(record);
        (void)road;
    } catch (const std::exception& e) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            std::string("elevation profile invalidates road: ") + e.what()};
    }

    record = store_.updateRoad(record);

    recordHistory(HistoryEntry{.roadId = input.roadId, .before = before, .after = record, .existedBefore = true});

    // Elevation changes affect rendered geometry: emit GeometryChanged
    // with affected chunks (Blocker 14: correct event semantics).
    std::vector<ChunkCoord> affectedChunks;
    if (world_.isReady()) {
        auto bounds = computeRoadBounds(record);
        auto mutation = world_.update(
            record.id,
            bounds,
            InvalidationMask::of(InvalidationClass::Road));
        affectedChunks = mutation.dirtyChunks;
    }

    eventSink_(RoadServiceEvent{
        RoadServiceEvent::Kind::GeometryChanged, input.roadId,
        store_.current().revision, affectedChunks});

    return toSummary(record);
}

RoadSummary RoadService::updateSuperelevation(const UpdateSuperelevationInput& input) {
    auto found = findRoad(input.roadId);
    if (!found.has_value()) {
        throw CommandFailure{CommandFailureCode::NotFound,
            "road not found: " + input.roadId};
    }

    if (input.stations.size() != input.superelevations.size()) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "stations and superelevations must have the same length"};
    }

    // Blocker 15/19: validate profile data before persistence. Stations
    // must be within the alignment's station range [0, totalLength].
    double alignmentLength = 0.0;
    for (const auto& seg : found->segments) {
        alignmentLength += seg.length;
    }
    for (std::size_t i = 0; i < input.stations.size(); ++i) {
        if (!std::isfinite(input.stations[i])) {
            throw CommandFailure{CommandFailureCode::InvalidArgument,
                "superelevation station " + std::to_string(i) + " is not finite"};
        }
        if (input.stations[i] < 0.0 || input.stations[i] > alignmentLength) {
            throw CommandFailure{CommandFailureCode::InvalidArgument,
                "superelevation station " + std::to_string(i) + " (" +
                std::to_string(input.stations[i]) + ") is outside the alignment range [0, " +
                std::to_string(alignmentLength) + "]"};
        }
        if (!std::isfinite(input.superelevations[i])) {
            throw CommandFailure{CommandFailureCode::InvalidArgument,
                "superelevation value " + std::to_string(i) + " is not finite"};
        }
        if (i > 0 && input.stations[i] <= input.stations[i - 1]) {
            throw CommandFailure{CommandFailureCode::InvalidArgument,
                "superelevation stations must be strictly increasing (failure at index " +
                std::to_string(i) + ")"};
        }
    }

    auto before = *found;
    auto record = before;
    record.superelevationBreakpoints.clear();
    for (std::size_t i = 0; i < input.stations.size(); ++i) {
        record.superelevationBreakpoints.push_back(
            ProfileBreakpoint{input.stations[i], input.superelevations[i]});
    }

    // Blocker 15: rebuild/validate the canonical Road to check invariants.
    try {
        auto road = rebuildRoad(record);
        (void)road;
    } catch (const std::exception& e) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            std::string("superelevation profile invalidates road: ") + e.what()};
    }

    record = store_.updateRoad(record);

    recordHistory(HistoryEntry{.roadId = input.roadId, .before = before, .after = record, .existedBefore = true});

    // Superelevation changes affect rendered geometry: emit GeometryChanged
    // with affected chunks (Blocker 14: correct event semantics).
    std::vector<ChunkCoord> affectedChunks;
    if (world_.isReady()) {
        auto bounds = computeRoadBounds(record);
        auto mutation = world_.update(
            record.id,
            bounds,
            InvalidationMask::of(InvalidationClass::Road));
        affectedChunks = mutation.dirtyChunks;
    }

    eventSink_(RoadServiceEvent{
        RoadServiceEvent::Kind::GeometryChanged, input.roadId,
        store_.current().revision, affectedChunks});

    return toSummary(record);
}

RoadSummary RoadService::updateWidth(const UpdateWidthInput& input) {
    auto found = findRoad(input.roadId);
    if (!found.has_value()) {
        throw CommandFailure{CommandFailureCode::NotFound, "road not found: " + input.roadId};
    }
    if (input.stations.size() != input.leftWidths.size()
        || input.stations.size() != input.rightWidths.size()) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "stations, left widths, and right widths must have the same length"};
    }
    double alignmentLength = 0.0;
    for (const auto& segment : found->segments) alignmentLength += segment.length;
    std::vector<domain::road::RoadWidthBreakpoint> breakpoints;
    breakpoints.reserve(input.stations.size());
    for (std::size_t i = 0; i < input.stations.size(); ++i) {
        if (!std::isfinite(input.stations[i]) || !std::isfinite(input.leftWidths[i])
            || !std::isfinite(input.rightWidths[i])) {
            throw CommandFailure{CommandFailureCode::InvalidArgument,
                "road width breakpoint values must be finite"};
        }
        if (input.stations[i] < 0.0 || input.stations[i] > alignmentLength) {
            throw CommandFailure{CommandFailureCode::InvalidArgument,
                "road width station is outside the alignment range"};
        }
        if (input.leftWidths[i] < 0.0 || input.rightWidths[i] < 0.0) {
            throw CommandFailure{CommandFailureCode::InvalidArgument,
                "road side widths must be non-negative"};
        }
        if (i > 0 && input.stations[i] <= input.stations[i - 1]) {
            throw CommandFailure{CommandFailureCode::InvalidArgument,
                "road width stations must be strictly increasing"};
        }
        breakpoints.push_back({input.stations[i], input.leftWidths[i], input.rightWidths[i]});
    }
    const auto validated = domain::road::buildRoadWidthProfile(breakpoints);
    if (!validated.has_value()) {
        throw CommandFailure{CommandFailureCode::InvalidArgument, validated.error().message};
    }

    const auto before = *found;
    auto record = before;
    record.widthBreakpoints = std::move(breakpoints);
    (void)rebuildRoad(record);
    record = store_.updateRoad(record);
    recordHistory(HistoryEntry{.roadId = input.roadId, .before = before,
        .after = record, .existedBefore = true});

    std::vector<ChunkCoord> affectedChunks;
    if (world_.isReady()) {
        auto mutation = world_.update(record.id, computeRoadBounds(record),
            InvalidationMask::of(InvalidationClass::Road));
        affectedChunks = mutation.dirtyChunks;
        const auto oldChunks = world_.chunksIntersecting(computeRoadBounds(before));
        affectedChunks.insert(affectedChunks.end(), oldChunks.begin(), oldChunks.end());
        std::sort(affectedChunks.begin(), affectedChunks.end());
        affectedChunks.erase(std::unique(affectedChunks.begin(), affectedChunks.end()), affectedChunks.end());
    }
    sceneMeshCache_.erase(input.roadId);
    eventSink_(RoadServiceEvent{RoadServiceEvent::Kind::GeometryChanged, input.roadId,
        store_.current().revision, affectedChunks});
    return toSummary(record);
}

std::vector<ProfileBreakpoint> RoadService::sampleElevationAlongAlignment(
    const ReferenceAlignment& alignment,
    double stationInterval,
    double verticalOffset,
    const TerrainHeightSampler& sampleHeight) const {
    if (!std::isfinite(stationInterval) || stationInterval <= 0.0) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "terrain conformance station interval must be finite and positive"};
    }
    if (!std::isfinite(verticalOffset)) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "terrain conformance vertical offset must be finite"};
    }
    if (!sampleHeight) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "terrain conformance requires a height sampler"};
    }
    if (alignment.isEmpty() || !std::isfinite(alignment.totalLength()) || alignment.totalLength() <= 0.0) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "road alignment is empty or degenerate"};
    }

    // Safely estimate sample count to prevent pathological allocations, integer overflow,
    // or executor blocking before station generation or sampling begins.
    const double roughIntervalSamples = alignment.totalLength() / stationInterval;
    if (!std::isfinite(roughIntervalSamples) || roughIntervalSamples < 0.0) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "invalid terrain conformance sampling interval"};
    }
    const double estimatedTotalSamples = roughIntervalSamples + static_cast<double>(alignment.segments().size()) + 2.0;
    if (estimatedTotalSamples > static_cast<double>(kMaximumTerrainConformanceSamples)) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "terrain conformance requested sample count ("
                + std::to_string(static_cast<std::size_t>(estimatedTotalSamples))
                + ") exceeds maximum allowed limit of "
                + std::to_string(kMaximumTerrainConformanceSamples)};
    }

    // Generate conformance stations independently from the data being replaced:
    // canonical centerline/alignment boundaries + the requested sampling interval.
    // Use integer-index-based generation to prevent floating-point accumulation or infinite loops.
    std::set<double> uniqueStations;
    uniqueStations.insert(0.0);
    double segStation = 0.0;
    for (const auto& seg : alignment.segments()) {
        const double segStart = segStation;
        const double segLen = segmentLength(seg.segment);
        const double segEnd = segStation + segLen;
        segStation = segEnd;
        uniqueStations.insert(segEnd);

        if (segLen > 1e-4) {
            const double maxSpan = segLen - 1e-4;
            const auto count = static_cast<std::size_t>(std::floor(maxSpan / stationInterval));
            for (std::size_t i = 1; i <= count; ++i) {
                const double s = segStart + static_cast<double>(i) * stationInterval;
                if (s < segEnd - 1e-4) {
                    uniqueStations.insert(s);
                }
            }
        }
    }
    uniqueStations.insert(alignment.totalLength());

    // Deduplicate coincident stations within numerical tolerance and enforce exact totalLength end
    std::vector<double> stations;
    stations.reserve(uniqueStations.size());
    for (const double s : uniqueStations) {
        if (stations.empty()) {
            stations.push_back(s);
        } else if (s - stations.back() > 1e-4) {
            stations.push_back(s);
        } else {
            // Close to previous station; preserve the higher (boundary) station
            stations.back() = s;
        }
    }
    if (stations.back() < alignment.totalLength() - 1e-4) {
        stations.push_back(alignment.totalLength());
    } else {
        stations.back() = alignment.totalLength();
    }

    if (stations.size() > kMaximumTerrainConformanceSamples) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "terrain conformance unique station count (" + std::to_string(stations.size())
                + ") exceeds maximum allowed limit of "
                + std::to_string(kMaximumTerrainConformanceSamples)};
    }

    std::vector<ProfileBreakpoint> breakpoints;
    breakpoints.reserve(stations.size());
    for (const double station : stations) {
        const auto sample = alignment.evaluate(station);
        auto sampled = sampleHeight(sample.position);
        if (!sampled.has_value()) {
            throw CommandFailure{CommandFailureCode::InvalidArgument,
                "terrain conformance failed at station " + std::to_string(station)
                    + ": " + sampled.error()};
        }
        const double conformedHeight = *sampled + verticalOffset;
        if (!std::isfinite(conformedHeight)) {
            throw CommandFailure{CommandFailureCode::InvalidArgument,
                "terrain conformance produced a non-finite height at station "
                    + std::to_string(station)};
        }
        breakpoints.push_back(ProfileBreakpoint{station, conformedHeight});
    }

    const auto profileRes = buildElevationProfile(breakpoints);
    if (!profileRes.has_value()) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "failed to build valid elevation profile from terrain samples: " + profileRes.error().message};
    }

    return breakpoints;
}

RoadSummary RoadService::conformToTerrain(
    const ConformRoadToTerrainInput& input, const TerrainHeightSampler& sampleHeight) {
    const auto found = findRoad(input.roadId);
    if (!found.has_value()) {
        throw CommandFailure{CommandFailureCode::NotFound, "road not found: " + input.roadId};
    }
    const auto road = rebuildRoad(*found);
    const auto breakpoints = sampleElevationAlongAlignment(
        road.alignment(), input.stationInterval, input.verticalOffset, sampleHeight);

    UpdateElevationInput elevation;
    elevation.roadId = input.roadId;
    elevation.stations.reserve(breakpoints.size());
    elevation.elevations.reserve(breakpoints.size());
    for (const auto& bp : breakpoints) {
        elevation.stations.push_back(bp.station);
        elevation.elevations.push_back(bp.value);
    }
    return updateElevation(elevation);
}

RoadSummary RoadService::updateLanes(const UpdateLanesInput& input) {
    auto found = findRoad(input.roadId);
    if (!found.has_value()) {
        throw CommandFailure{CommandFailureCode::NotFound, "road not found: " + input.roadId};
    }

    if (input.laneSections.empty()) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "road must have at least one lane section"};
    }

    double alignmentLength = 0.0;
    for (const auto& segment : found->segments) alignmentLength += segment.length;

    for (const auto& sec : input.laneSections) {
        if (sec.startStation < 0.0 || sec.endStation < sec.startStation || sec.endStation > alignmentLength + 1e-3) {
            throw CommandFailure{CommandFailureCode::InvalidArgument,
                "lane section stations are invalid or out of alignment range"};
        }
    }

    for (const auto& lane : input.lanes) {
        if (lane.laneId.empty() || lane.laneIndex < 1 || !std::isfinite(lane.width) || lane.width < 0.0) {
            throw CommandFailure{CommandFailureCode::InvalidArgument,
                "lane properties are invalid: lane_id, lane_index, or width"};
        }
        if (lane.side != "left" && lane.side != "right") {
            throw CommandFailure{CommandFailureCode::InvalidArgument,
                "lane side must be 'left' or 'right'"};
        }
    }

    const auto before = *found;
    auto record = before;
    record.laneSections = input.laneSections;
    record.lanes = input.lanes;

    auto validated = domain::road::fromRecord(record);
    if (!validated.has_value()) {
        std::string msg = "road validation failed";
        if (!validated.error().empty()) {
            msg += ": " + validated.error().front().message;
        }
        throw CommandFailure{CommandFailureCode::InvalidArgument, msg};
    }

    record = store_.updateRoad(record);
    recordHistory(HistoryEntry{.roadId = input.roadId, .before = before,
        .after = record, .existedBefore = true});

    sceneMeshCache_.erase(input.roadId);
    eventSink_(RoadServiceEvent{RoadServiceEvent::Kind::Updated, input.roadId,
        store_.current().revision});
    return toSummary(record);
}

std::vector<domain::road::JunctionRecord> RoadService::listJunctions() const {
    if (!store_.isOpen()) {
        return {};
    }
    return store_.junctions();
}

std::optional<domain::road::JunctionRecord> RoadService::getJunction(const std::string& junctionId) const {
    if (!store_.isOpen()) {
        return std::nullopt;
    }
    const auto all = store_.junctions();
    for (const auto& j : all) {
        if (domain::road::uuidTextFromJunctionId(j.id) == junctionId) {
            return j;
        }
    }
    return std::nullopt;
}

domain::road::JunctionRecord RoadService::createJunction(const domain::road::JunctionRecord& input) {
    if (!store_.isOpen()) {
        throw CommandFailure{CommandFailureCode::ProjectNotOpen,
            "cannot create junction without an open project"};
    }
    if (input.name.empty()) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "junction name must not be empty"};
    }
    auto record = input;
    if (record.id.isNull()) {
        record.id = domain::road::junctionIdFromUuidText(runtime::generateUuidV4());
    }
    record.revision = 1;

    for (const auto& app : record.approaches) {
        if (app.roadId.empty()) {
            throw CommandFailure{CommandFailureCode::InvalidArgument,
                "junction approach roadId must not be empty"};
        }
        if (app.contactPoint != "start" && app.contactPoint != "end") {
            throw CommandFailure{CommandFailureCode::InvalidArgument,
                "junction approach contactPoint must be 'start' or 'end'"};
        }
    }

    auto saved = store_.insertJunction(record);
    const std::string junctionIdText = domain::road::uuidTextFromJunctionId(saved.id);
    RoadServiceEvent createdEvent;
    createdEvent.kind = RoadServiceEvent::Kind::JunctionCreated;
    createdEvent.revision = store_.current().revision;
    createdEvent.junctionId = junctionIdText;
    createdEvent.junctionName = saved.name;
    eventSink_(std::move(createdEvent));
    return saved;
}

domain::road::JunctionRecord RoadService::updateJunction(const domain::road::JunctionRecord& input) {
    if (!store_.isOpen()) {
        throw CommandFailure{CommandFailureCode::ProjectNotOpen,
            "cannot update junction without an open project"};
    }
    if (input.id.isNull()) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "junction id must not be null"};
    }
    const std::string junctionIdText = domain::road::uuidTextFromJunctionId(input.id);
    auto existing = getJunction(junctionIdText);
    if (!existing.has_value()) {
        throw CommandFailure{CommandFailureCode::NotFound,
            "junction not found: " + junctionIdText};
    }

    auto record = input;
    record.revision = existing->revision + 1;

    auto saved = store_.updateJunction(record);
    RoadServiceEvent updatedEvent;
    updatedEvent.kind = RoadServiceEvent::Kind::JunctionUpdated;
    updatedEvent.revision = store_.current().revision;
    updatedEvent.junctionId = junctionIdText;
    updatedEvent.junctionName = saved.name;
    eventSink_(std::move(updatedEvent));
    return saved;
}

void RoadService::deleteJunction(const std::string& junctionId) {
    if (!store_.isOpen()) {
        throw CommandFailure{CommandFailureCode::ProjectNotOpen,
            "cannot delete junction without an open project"};
    }
    auto existing = getJunction(junctionId);
    if (!existing.has_value()) {
        throw CommandFailure{CommandFailureCode::NotFound,
            "junction not found: " + junctionId};
    }

    store_.removeJunction(junctionId);
    RoadServiceEvent removedEvent;
    removedEvent.kind = RoadServiceEvent::Kind::JunctionRemoved;
    removedEvent.revision = store_.current().revision;
    removedEvent.junctionId = junctionId;
    removedEvent.junctionName = existing->name;
    eventSink_(std::move(removedEvent));
}

RoadHistoryResult RoadService::undo(const std::string& roadId) {
    // Blocker 10: global chronological undo. An empty road ID means undo the
    // most recent road command across ALL roads, determined by sequence
    // number — NOT by per-road stack depth or unordered_map iteration order.
    // A non-empty road ID undoes the most recent command for that specific
    // road, still using the global chronological history.
    auto undoIdx = findLastUndo(roadId);
    if (!undoIdx.has_value()) return {};

    auto entry = std::move(undoHistory_[*undoIdx]);
    undoHistory_.erase(undoHistory_.begin() + *undoIdx);
    const auto effectiveRoadId = entry.roadId;

    std::vector<ChunkCoord> affectedChunks;
    if (!entry.existedBefore && entry.after.has_value()) {
        // Undo of a create: remove the road.
        store_.removeRoad(effectiveRoadId);
        if (world_.isReady()) {
            auto mutation = world_.remove(
                roadIdFromUuidText(effectiveRoadId),
                InvalidationMask::of(InvalidationClass::Road));
            affectedChunks = mutation.dirtyChunks;
        }
    } else if (entry.existedBefore && entry.before.has_value() && !entry.after.has_value()) {
        // Undo of a delete: re-insert the road.
        (void)store_.insertRoad(*entry.before);
        if (world_.isReady()) {
            auto bounds = computeRoadBounds(*entry.before);
            auto mutation = world_.insert(
                entry.before->id,
                bounds,
                InvalidationMask::of(InvalidationClass::Road));
            affectedChunks = mutation.dirtyChunks;
        }
    } else if (entry.existedBefore && entry.before.has_value()) {
        // Undo of an update: restore the previous state.
        (void)store_.updateRoad(*entry.before);
        if (world_.isReady()) {
            auto bounds = computeRoadBounds(*entry.before);
            auto mutation = world_.update(
                entry.before->id,
                bounds,
                InvalidationMask::of(InvalidationClass::Road));
            affectedChunks = mutation.dirtyChunks;
        }
    }

    redoHistory_.push_back(std::move(entry));

    // Blocker 14: emit correct event semantics. Undo of a create = Removed;
    // undo of a delete = Created; undo of an update = GeometryChanged with
    // affected chunks (not generic Updated with empty chunks).
    auto& redoEntry = redoHistory_.back();
    if (!redoEntry.existedBefore && redoEntry.after.has_value()) {
        eventSink_(RoadServiceEvent{
            RoadServiceEvent::Kind::Removed, effectiveRoadId,
            store_.current().revision, affectedChunks});
    } else if (redoEntry.existedBefore && redoEntry.before.has_value() && !redoEntry.after.has_value()) {
        eventSink_(RoadServiceEvent{
            RoadServiceEvent::Kind::Created, effectiveRoadId,
            store_.current().revision, affectedChunks});
    } else {
        eventSink_(RoadServiceEvent{
            RoadServiceEvent::Kind::GeometryChanged, effectiveRoadId,
            store_.current().revision, affectedChunks});
    }

    auto summary = getRoadSummary(effectiveRoadId);
    return RoadHistoryResult{effectiveRoadId, summary, summary.has_value()};
}

RoadHistoryResult RoadService::redo(const std::string& roadId) {
    // Blocker 10: global chronological redo, mirroring the undo contract.
    auto redoIdx = findLastRedo(roadId);
    if (!redoIdx.has_value()) return {};

    auto entry = std::move(redoHistory_[*redoIdx]);
    redoHistory_.erase(redoHistory_.begin() + *redoIdx);
    const auto effectiveRoadId = entry.roadId;

    std::vector<ChunkCoord> affectedChunks;
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
                auto mutation = world_.update(
                    entry.after->id,
                    bounds,
                    InvalidationMask::of(InvalidationClass::Road));
                affectedChunks = mutation.dirtyChunks;
            } else {
                auto mutation = world_.insert(
                    entry.after->id,
                    bounds,
                    InvalidationMask::of(InvalidationClass::Road));
                affectedChunks = mutation.dirtyChunks;
            }
        }
    } else {
        // The command was a delete — remove the road again.
        store_.removeRoad(effectiveRoadId);
        if (world_.isReady()) {
            auto mutation = world_.remove(
                roadIdFromUuidText(effectiveRoadId),
                InvalidationMask::of(InvalidationClass::Road));
            affectedChunks = mutation.dirtyChunks;
        }
    }

    undoHistory_.push_back(std::move(entry));

    // Blocker 14: emit correct event semantics matching the redone command.
    auto& undoEntry = undoHistory_.back();
    if (undoEntry.after.has_value() && !undoEntry.existedBefore) {
        eventSink_(RoadServiceEvent{
            RoadServiceEvent::Kind::Created, effectiveRoadId,
            store_.current().revision, affectedChunks});
    } else if (!undoEntry.after.has_value()) {
        eventSink_(RoadServiceEvent{
            RoadServiceEvent::Kind::Removed, effectiveRoadId,
            store_.current().revision, affectedChunks});
    } else {
        eventSink_(RoadServiceEvent{
            RoadServiceEvent::Kind::GeometryChanged, effectiveRoadId,
            store_.current().revision, affectedChunks});
    }

    auto summary = getRoadSummary(effectiveRoadId);
    return RoadHistoryResult{effectiveRoadId, summary, summary.has_value()};
}

bool RoadService::canUndo(const std::string& roadId) const {
    return findLastUndo(roadId).has_value();
}

bool RoadService::canRedo(const std::string& roadId) const {
    return findLastRedo(roadId).has_value();
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
    auto found = findRoad(roadId);
    if (!found.has_value()) return std::nullopt;
    return toDetails(*found);
}

std::optional<RoadSummary> RoadService::getRoadSummary(const std::string& roadId) const {
    auto found = findRoad(roadId);
    if (!found.has_value()) return std::nullopt;
    return toSummary(*found);
}

std::optional<domain::road::RoadTessellation> RoadService::getRoadTessellation(
    const std::string& roadId,
    const domain::road::RoadTessellationParams& params) const {
    auto found = findRoad(roadId);
    if (!found.has_value()) return std::nullopt;

    auto road = rebuildRoad(*found);
    return domain::road::tessellateRoad(
        road.alignment(), road.elevation(), road.superelevation(), road.width(), params);
}

RoadSceneProjection RoadService::roadSceneProjection() const {
    RoadSceneProjection projection;
    const auto& record = store_.current();
    projection.originEasting = record.georeference.originEasting;
    projection.originNorthing = record.georeference.originNorthing;
    projection.originHeight = record.georeference.originHeight;
    projection.revision = record.revision;

    const auto roads = store_.roads();
    std::set<std::string> liveRoadIds;
    for (const auto& roadRecord : roads) {
        const auto roadId = uuidTextFromRoadId(roadRecord.id);
        liveRoadIds.insert(roadId);
        const auto cached = sceneMeshCache_.find(roadId);
        if (cached != sceneMeshCache_.end() && hasSameRenderGeometry(cached->second.record, roadRecord)) {
            projection.meshes.insert(projection.meshes.end(), cached->second.meshes.begin(),
                cached->second.meshes.end());
            continue;
        }
        auto road = rebuildRoad(roadRecord);
        auto tess = domain::road::tessellateRoad(
            road.alignment(), road.elevation(), road.superelevation(), road.width(), {});

        if (tess.isEmpty()) continue;

        RoadSceneMesh mesh;
        mesh.roadId = roadId;

        // Build vertices: left and right edge of each cross-section.
        // Vertex layout: for cross-section i, left = 2*i, right = 2*i+1.
        mesh.vertices.reserve(tess.crossSections.size() * 2);
        for (std::size_t ci = 0; ci < tess.crossSections.size(); ++ci) {
            const auto& cs = tess.crossSections[ci];

            // Blocker 16: compute the road surface normal from the actual
            // cross-section geometry. The normal is perpendicular to the
            // road surface, accounting for:
            //   - road heading (tangent direction)
            //   - longitudinal elevation grade
            //   - superelevation/cross-slope (bank)
            // The tangent vector is along the heading; the cross-section
            // vector is perpendicular to the heading in the horizontal
            // plane, tilted by the cross-slope. The normal is the cross
            // product of tangent × cross-section, normalized.
            const double heading = cs.heading;
            const double cosH = std::cos(heading);
            const double sinH = std::sin(heading);

            // Tangent direction (horizontal, along heading).
            // For the longitudinal grade, estimate the elevation change
            // between this cross-section and the next (or previous).
            double grade = 0.0;
            if (ci + 1 < tess.crossSections.size()) {
                const auto& next = tess.crossSections[ci + 1];
                const double ds = next.station - cs.station;
                if (ds > 1e-9) {
                    grade = (next.height - cs.height) / ds;
                }
            } else if (ci > 0) {
                const auto& prev = tess.crossSections[ci - 1];
                const double ds = cs.station - prev.station;
                if (ds > 1e-9) {
                    grade = (cs.height - prev.height) / ds;
                }
            }

            // Tangent vector includes the longitudinal grade.
            double tx = cosH;
            double ty = sinH;
            double tz = grade;

            // Cross-section direction: perpendicular to heading, tilted by
            // cross-slope (superelevation). The canonical value is a bank
            // angle in radians, so its vertical rise/run is tan(angle).
            const double crossSlope = std::tan(cs.crossSlope);
            // Perpendicular to heading in the horizontal plane.
            double px = -sinH;
            double py = cosH;
            // The cross-section vector tilts vertically by the cross-slope.
            // Positive cross-slope means the left edge is higher.
            double pz = crossSlope;

            // Normal = tangent × cross-section, normalized.
            double nx = ty * pz - tz * py;
            double ny = tz * px - tx * pz;
            double nz = tx * py - ty * px;
            const double nlen = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (nlen > 1e-12) {
                nx /= nlen;
                ny /= nlen;
                nz /= nlen;
            } else {
                nx = 0.0;
                ny = 0.0;
                nz = 1.0;
            }

            // Left edge vertex.
            RoadSceneVertex left;
            left.x = static_cast<float>(cs.leftEdge.easting - projection.originEasting);
            left.y = static_cast<float>(cs.leftEdge.northing - projection.originNorthing);
            left.z = static_cast<float>(cs.leftHeight - projection.originHeight);
            left.nx = static_cast<float>(nx);
            left.ny = static_cast<float>(ny);
            left.nz = static_cast<float>(nz);
            mesh.vertices.push_back(left);

            // Right edge vertex.
            RoadSceneVertex right;
            right.x = static_cast<float>(cs.rightEdge.easting - projection.originEasting);
            right.y = static_cast<float>(cs.rightEdge.northing - projection.originNorthing);
            right.z = static_cast<float>(cs.rightHeight - projection.originHeight);
            right.nx = static_cast<float>(nx);
            right.ny = static_cast<float>(ny);
            right.nz = static_cast<float>(nz);
            mesh.vertices.push_back(right);
        }

        // Partition the derived ribbon into deterministic road/chunk meshes.
        // Each station interval is owned by the chunk containing its
        // centerline midpoint. Adjacent chunk meshes duplicate only their
        // shared boundary cross-section, so both sides use bit-identical
        // canonical samples and cannot develop a positional crack.
        std::map<ChunkCoord, RoadSceneMesh> chunks;
        for (std::size_t i = 0; i + 1 < tess.crossSections.size(); ++i) {
            const auto& a = tess.crossSections[i];
            const auto& b = tess.crossSections[i + 1];
            const auto chunk = world_.grid().chunkAt(
                (a.center.easting + b.center.easting) * 0.5,
                (a.center.northing + b.center.northing) * 0.5);
            auto& chunkMesh = chunks[chunk];
            chunkMesh.roadId = mesh.roadId;
            chunkMesh.chunkX = chunk.x;
            chunkMesh.chunkY = chunk.y;
            const auto base = static_cast<std::uint32_t>(chunkMesh.vertices.size());
            chunkMesh.vertices.push_back(mesh.vertices[i * 2]);
            chunkMesh.vertices.push_back(mesh.vertices[i * 2 + 1]);
            chunkMesh.vertices.push_back(mesh.vertices[(i + 1) * 2]);
            chunkMesh.vertices.push_back(mesh.vertices[(i + 1) * 2 + 1]);
            chunkMesh.indices.insert(chunkMesh.indices.end(),
                {base, base + 1, base + 2, base + 1, base + 3, base + 2});
        }
        std::vector<RoadSceneMesh> derivedMeshes;
        derivedMeshes.reserve(chunks.size());
        for (auto& [chunk, chunkMesh] : chunks) {
            (void)chunk;
            derivedMeshes.push_back(std::move(chunkMesh));
        }
        projection.meshes.insert(projection.meshes.end(), derivedMeshes.begin(), derivedMeshes.end());
        sceneMeshCache_[roadId] = CachedRoadMeshes{roadRecord, std::move(derivedMeshes)};
    }

    std::erase_if(sceneMeshCache_, [&liveRoadIds](const auto& entry) {
        return !liveRoadIds.contains(entry.first);
    });

    return projection;
}

void RoadService::recordHistory(HistoryEntry entry) {
    entry.sequence = nextSequence_++;
    undoHistory_.push_back(std::move(entry));
    // Clear redo history on new command (global, not per-road).
    redoHistory_.clear();
}

std::optional<std::size_t> RoadService::findLastUndo(
    const std::string& roadId) const {
    if (roadId.empty()) {
        if (undoHistory_.empty()) return std::nullopt;
        return undoHistory_.size() - 1;
    }
    // Find the most recent entry for this specific road (highest sequence).
    std::optional<std::size_t> result;
    std::uint64_t bestSeq = 0;
    for (std::size_t i = 0; i < undoHistory_.size(); ++i) {
        if (undoHistory_[i].roadId == roadId && undoHistory_[i].sequence >= bestSeq) {
            bestSeq = undoHistory_[i].sequence;
            result = i;
        }
    }
    return result;
}

std::optional<std::size_t> RoadService::findLastRedo(
    const std::string& roadId) const {
    if (roadId.empty()) {
        if (redoHistory_.empty()) return std::nullopt;
        return redoHistory_.size() - 1;
    }
    std::optional<std::size_t> result;
    std::uint64_t bestSeq = 0;
    for (std::size_t i = 0; i < redoHistory_.size(); ++i) {
        if (redoHistory_[i].roadId == roadId && redoHistory_[i].sequence >= bestSeq) {
            bestSeq = redoHistory_[i].sequence;
            result = i;
        }
    }
    return result;
}

SpatialBounds RoadService::computeRoadBounds(const RoadRecord& road) const {
    if (road.segments.empty()) {
        return SpatialBounds{0, 0, 0, 0};
    }
    double minE = std::numeric_limits<double>::max();
    double minN = std::numeric_limits<double>::max();
    double maxE = std::numeric_limits<double>::lowest();
    double maxN = std::numeric_limits<double>::lowest();

    // Blocker 14: derive conservative but accurate bounds from actual
    // canonical geometry. For each segment, evaluate the mathematical
    // curve to find the true extrema, including arc axis-extrema angles
    // that fall within the finite arc interval. Then expand by the
    // actual road cross-section half-width, not an unexplained constant.
    for (const auto& seg : road.segments) {
        // Reconstruct the segment variant for evaluation.
        AlignmentSegment segment;
        switch (seg.kind) {
        case AlignmentSegmentKind::Line:
            segment = LineSegment{seg.start, seg.startHeading, seg.length};
            break;
        case AlignmentSegmentKind::CircularArc:
            segment = CircularArcSegment{
                seg.start, seg.startHeading, seg.curvature, seg.length};
            break;
        case AlignmentSegmentKind::Clothoid:
            segment = ClothoidSegment{
                seg.start, seg.startHeading,
                seg.startCurvature, seg.endCurvature, seg.length};
            break;
        }

        // Always include the segment start point.
        minE = std::min(minE, seg.start.easting);
        minN = std::min(minN, seg.start.northing);
        maxE = std::max(maxE, seg.start.easting);
        maxN = std::max(maxN, seg.start.northing);

        // Blocker 14: for circular arcs, compute the exact axis extrema.
        // The arc may reach its max/min easting/northing at interior points
        // where the tangent is axis-aligned (heading = 0, 90, 180, 270
        // degrees). Check if those heading angles fall within the arc's
        // heading sweep and evaluate the position at those exact points.
        if (seg.kind == AlignmentSegmentKind::CircularArc) {
            const double startHeading = seg.startHeading;
            const double curvature = seg.curvature;
            const double length = seg.length;
            const double radius = 1.0 / std::abs(curvature);
            const double sweep = length * curvature;  // signed total angle
            const double endHeading = startHeading + sweep;

            // The arc center is perpendicular to the start heading.
            // For positive curvature (left turn), center is to the left.
            const double perpSign = curvature > 0 ? 1.0 : -1.0;
            const double centerE = seg.start.easting +
                perpSign * radius * (-std::sin(startHeading));
            const double centerN = seg.start.northing +
                perpSign * radius * std::cos(startHeading);

            // Check each axis-aligned heading (0, 90, 180, 270 degrees)
            // to see if it falls within [startHeading, endHeading] (or
            // [endHeading, startHeading] if sweep is negative).
            const double lo = std::min(startHeading, endHeading);
            const double hi = std::max(startHeading, endHeading);
            for (int k = -4; k <= 4; ++k) {
                const double target = static_cast<double>(k) *
                    (3.14159265358979323846 / 2.0);
                if (target >= lo && target <= hi) {
                    // At this heading, the arc point is at center + radius
                    // in the direction perpendicular to the heading.
                    const double ptE = centerE + radius * (-std::sin(target));
                    const double ptN = centerN + radius * std::cos(target);
                    minE = std::min(minE, ptE);
                    minN = std::min(minN, ptN);
                    maxE = std::max(maxE, ptE);
                    maxN = std::max(maxN, ptN);
                }
            }
        }

        // Sample the segment to find curve extrema for clothoids and as
        // a conservative fallback for arcs. Use deterministic adaptive
        // sampling: at minimum 16 samples per segment, scaled by length.
        const std::size_t samples = std::max<std::size_t>(
            16, static_cast<std::size_t>(seg.length * 2.0));
        for (std::size_t j = 1; j <= samples; ++j) {
            const double sLocal = seg.length * static_cast<double>(j) /
                static_cast<double>(samples);
            const auto sample = evaluateSegment(segment, sLocal);
            minE = std::min(minE, sample.position.easting);
            minN = std::min(minN, sample.position.northing);
            maxE = std::max(maxE, sample.position.easting);
            maxN = std::max(maxN, sample.position.northing);
        }
    }
    double maximumSideWidth = 5.0;
    for (const auto& breakpoint : road.widthBreakpoints) {
        maximumSideWidth = std::max({maximumSideWidth,
            breakpoint.leftWidth, breakpoint.rightWidth});
    }
    const double margin = maximumSideWidth + 1.0;
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
    s.constructionKind = roadConstructionKindName(road.constructionKind);

    // Compute total length.
    double totalLength = 0.0;
    for (const auto& seg : road.segments) {
        totalLength += seg.length;
    }
    s.length = totalLength;

    if (!road.segments.empty()) {
        s.startEasting = road.segments.front().start.easting;
        s.startNorthing = road.segments.front().start.northing;
        try {
            auto roadObj = rebuildRoad(road);
            const auto endPos = roadObj.alignment().evaluate(roadObj.alignment().totalLength()).position;
            s.endEasting = endPos.easting;
            s.endNorthing = endPos.northing;
        } catch (...) {
            if (!road.controlVertices.empty()) {
                s.endEasting = road.controlVertices.back().x;
                s.endNorthing = road.controlVertices.back().y;
            }
        }
    } else if (!road.controlVertices.empty()) {
        s.startEasting = road.controlVertices.front().x;
        s.startNorthing = road.controlVertices.front().y;
        s.endEasting = road.controlVertices.back().x;
        s.endNorthing = road.controlVertices.back().y;
    }

    return s;
}

RoadDetails RoadService::toDetails(const RoadRecord& road) const {
    RoadDetails d;
    d.roadId = uuidTextFromRoadId(road.id);
    d.name = road.displayName;
    d.constructionKind = roadConstructionKindName(road.constructionKind);
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
    d.positionTolerance = road.positionTolerance;
    d.maxCurvature = road.maxCurvature;
    d.elevationBreakpoints.reserve(road.elevationBreakpoints.size());
    for (const auto& breakpoint : road.elevationBreakpoints) {
        d.elevationBreakpoints.push_back({breakpoint.station, breakpoint.value});
    }
    d.superelevationBreakpoints.reserve(road.superelevationBreakpoints.size());
    for (const auto& breakpoint : road.superelevationBreakpoints) {
        d.superelevationBreakpoints.push_back({breakpoint.station, breakpoint.value});
    }
    d.widthBreakpoints = road.widthBreakpoints;
    d.laneSections = road.laneSections;
    d.lanes = road.lanes;
    d.controlPoints.reserve(road.controlVertices.size());
    for (std::size_t i = 0; i < road.controlVertices.size(); ++i) {
        const auto& vertex = road.controlVertices[i];
        const bool protectedAnchor = std::any_of(road.protectedAnchors.begin(),
            road.protectedAnchors.end(), [&vertex](const auto& anchor) {
                return anchor.position.easting == vertex.x &&
                    anchor.position.northing == vertex.y;
            });
        d.controlPoints.push_back({vertex.x, vertex.y, vertex.z, protectedAnchor});
    }

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

std::optional<RoadRecord> RoadService::findRoad(const std::string& roadId) const {
    // Blocker 17: single lookup path. Still uses store_.roads() since the
    // ProjectStore port does not yet expose a direct id-based lookup, but
    // this centralizes the search so all mutating commands share one path.
    // A future store enhancement can add road(id) and all callers benefit.
    auto roads = store_.roads();
    auto it = std::find_if(roads.begin(), roads.end(),
        [&](const RoadRecord& r) { return uuidTextFromRoadId(r.id) == roadId; });
    if (it == roads.end()) return std::nullopt;
    return *it;
}

AlignmentFitResult RoadService::fitAlignmentOnly(
    const std::vector<ConditionedVertex>& polyline,
    const std::vector<ProtectedAnchor>& anchors,
    double positionTolerance,
    std::optional<double> maxCurvature) const {

    AlignmentFitInput fitInput;
    fitInput.polyline = polyline;
    fitInput.protectedAnchors = anchors;
    fitInput.positionTolerance = positionTolerance;
    fitInput.maxCurvature = maxCurvature;

    return fitAlignment(fitInput);
}

RoadRecord RoadService::buildNewRoadRecord(
    const std::string& name,
    const ReferenceAlignment& alignment,
    const std::vector<ConditionedVertex>& polyline,
    const std::vector<ProtectedAnchor>& anchors,
    const std::vector<std::optional<double>>& sourceElevations,
    SourceProvider provider,
    double positionTolerance,
    std::optional<double> maxCurvature,
    const std::set<std::size_t>& anchorBoundarySegments,
    const std::vector<ProfileBreakpoint>& initialElevationProfile,
    RoadConstructionKind constructionKind) const {

    // Blocker 1: this is the ONLY place that mints a new RoadId.
    Road::BuildInput roadInput;
    roadInput.id = generateRoadId();
    roadInput.displayName = name;
    roadInput.alignment = alignment;

    // Store source vertices with optional elevations (Blocker 16: preserve
    // and use source elevations instead of silently discarding them).
    for (std::size_t i = 0; i < polyline.size(); ++i) {
        SourceVertex sv;
        sv.x = polyline[i].position.easting;
        sv.y = polyline[i].position.northing;
        if (i < sourceElevations.size() && sourceElevations[i].has_value()) {
            sv.z = sourceElevations[i];
        } else if (!initialElevationProfile.empty()) {
            // Assign conformed height if available at matching station
            const double s = polyline[i].sourceStation;
            for (const auto& bp : initialElevationProfile) {
                if (std::abs(bp.station - s) < 1e-4) {
                    sv.z = bp.value;
                    break;
                }
            }
        }
        roadInput.source.geometry.vertices.push_back(sv);
    }
    roadInput.source.provenance.provider = provider;
    roadInput.source.protectedAnchors = anchors;

    // Conformed terrain profile takes precedence if supplied.
    if (!initialElevationProfile.empty()) {
        auto elevProfile = buildElevationProfile(initialElevationProfile);
        if (elevProfile.has_value()) {
            roadInput.elevation = std::move(*elevProfile);
        }
    } else if (!sourceElevations.empty()) {
        std::vector<ProfileBreakpoint> elevBreakpoints;
        double sourceTotalLength = 0.0;
        for (std::size_t i = 1; i < polyline.size(); ++i) {
            const double dx = polyline[i].position.easting -
                polyline[i - 1].position.easting;
            const double dy = polyline[i].position.northing -
                polyline[i - 1].position.northing;
            sourceTotalLength += std::sqrt(dx * dx + dy * dy);
        }
        const double alignmentLength = alignment.totalLength();
        const double stationScale = (sourceTotalLength > 1e-9)
            ? alignmentLength / sourceTotalLength
            : 1.0;

        double cumulativeStation = 0.0;
        for (std::size_t i = 0; i < polyline.size(); ++i) {
            if (i > 0) {
                const double dx = polyline[i].position.easting -
                    polyline[i - 1].position.easting;
                const double dy = polyline[i].position.northing -
                    polyline[i - 1].position.northing;
                cumulativeStation += std::sqrt(dx * dx + dy * dy);
            }
            if (i < sourceElevations.size() && sourceElevations[i].has_value()) {
                // Map source station to alignment station.
                const double alignmentStation = cumulativeStation * stationScale;
                elevBreakpoints.push_back(
                    {alignmentStation, *sourceElevations[i]});
            }
        }
        if (elevBreakpoints.size() >= 2) {
            auto elevProfile = buildElevationProfile(elevBreakpoints);
            if (elevProfile.has_value()) {
                roadInput.elevation = std::move(*elevProfile);
            }
        }
    }

    auto road = Road::build(std::move(roadInput));
    if (!road.has_value()) {
        std::string msg = "road build failed: ";
        for (const auto& d : road.error()) {
            msg += std::string(roadErrorCodeName(d.code)) + ": " + d.message + "; ";
        }
        throw CommandFailure{CommandFailureCode::InvalidArgument, msg};
    }

    auto record = toRecord(*road);
    // Blocker 6: control vertices are the editable geometry the fitter
    // uses. They start as a copy of the source evidence; user edits may
    // diverge them while sourceVertices remains immutable evidence.
    record.controlVertices = record.sourceVertices;
    // Blocker 7: persist the fitting contract so future control edits
    // reuse the same tolerance/maxCurvature instead of a magic constant.
    record.positionTolerance = positionTolerance;
    record.maxCurvature = maxCurvature;
    record.anchorBoundarySegments = anchorBoundarySegments;
    record.constructionKind = constructionKind;
    return record;
}

namespace {

template <typename BreakpointType, typename ProfileBuilder, typename EndpointMaker>
std::vector<BreakpointType> adjustProfileLength(
    const std::vector<BreakpointType>& breakpoints,
    double newLength,
    ProfileBuilder&& builder,
    EndpointMaker&& makeEndpoint) {
    if (breakpoints.empty() || newLength <= 0.0 || !std::isfinite(newLength)) return {};
    auto profile = builder(breakpoints);
    if (!profile.has_value()) return {};

    constexpr double kStationTol = 1e-4;
    std::vector<BreakpointType> result;
    bool truncated = false;

    for (const auto& bp : breakpoints) {
        if (!std::isfinite(bp.station)) return {};
        if (bp.station < newLength - kStationTol) {
            result.push_back(bp);
        } else if (std::abs(bp.station - newLength) <= kStationTol) {
            auto snapped = bp;
            snapped.station = newLength;
            result.push_back(snapped);
            truncated = true;
            break;
        } else {
            truncated = true;
            break;
        }
    }

    if (truncated) {
        // Shortening: if the last kept breakpoint does not reach newLength within tolerance,
        // interpolate/evaluate the previous profile at newLength.
        if (result.empty() || result.back().station < newLength - kStationTol) {
            result.push_back(makeEndpoint(*profile, newLength));
        } else {
            result.back().station = newLength;
        }
    } else {
        // Lengthening: extend canonical profile to newLength using existing endpoint evaluation.
        if (!result.empty() && result.back().station < newLength - kStationTol) {
            result.push_back(makeEndpoint(*profile, newLength));
        } else if (!result.empty() && std::abs(result.back().station - newLength) <= kStationTol) {
            result.back().station = newLength;
        }
    }

    // Defensive pass: ensure strictly increasing stations by station tolerance.
    std::vector<BreakpointType> sanitized;
    for (const auto& bp : result) {
        if (!sanitized.empty() && bp.station <= sanitized.back().station + 1e-9) {
            sanitized.back() = bp;
        } else {
            sanitized.push_back(bp);
        }
    }
    return sanitized;
}

} // namespace

std::vector<ProfileBreakpoint> RoadService::clampProfileBreakpoints(
    const std::vector<ProfileBreakpoint>& breakpoints, double newLength) {
    return adjustProfileLength(
        breakpoints,
        newLength,
        [](const std::vector<ProfileBreakpoint>& bps) { return buildElevationProfile(bps); },
        [](const auto& prof, double len) {
            return ProfileBreakpoint{.station = len, .value = prof.evaluate(len)};
        });
}

std::vector<RoadWidthBreakpoint> RoadService::clampWidthBreakpoints(
    const std::vector<RoadWidthBreakpoint>& breakpoints, double newLength) {
    return adjustProfileLength(
        breakpoints,
        newLength,
        [](const std::vector<RoadWidthBreakpoint>& bps) { return buildRoadWidthProfile(bps); },
        [](const auto& prof, double len) {
            auto evaluated = prof.evaluate(len);
            return RoadWidthBreakpoint{.station = len, .leftWidth = evaluated.left, .rightWidth = evaluated.right};
        });
}

RoadRecord RoadService::refitRoad(
    const RoadRecord& existing,
    double positionTolerance,
    std::optional<double> maxCurvature) const {

    // Blocker 1 + Blocker 2: refit preserves the existing RoadId, display
    // name, elevation/superelevation profiles, provenance (provider, source
    // ID, source CRS, source tags, import timestamp), and protected
    // anchors.

    // Rebuild conditioned polyline from CONTROL vertices (the editable
    // geometry the fitter/primitive constructor uses). Source vertices remain
    // immutable evidence and are preserved unchanged (Blocker 6).
    const auto& fitVertices = existing.controlVertices.empty()
        ? existing.sourceVertices : existing.controlVertices;

    std::optional<ReferenceAlignment> primitiveAlignment;
    std::set<std::size_t> anchorBoundaries;
    std::vector<ProtectedAnchor> anchors;

    // Preserve canonical primitive type during editing for directly authored roads.
    // If the road has an explicit construction kind (Straight, ArcThreePoint, Clothoid),
    // it MUST reconstruct strictly as that primitive or fail with InvalidArgument.
    // It must NEVER fall back to polyline fitting (Atomic Non-Fallback Guarantee).
    if (existing.constructionKind == RoadConstructionKind::Straight) {
        if (fitVertices.size() != 2) {
            throw CommandFailure{CommandFailureCode::InvalidArgument,
                "straight road requires exactly 2 control vertices"};
        }
        const AlignmentPoint p0{fitVertices[0].x, fitVertices[0].y};
        const AlignmentPoint p1{fitVertices[1].x, fitVertices[1].y};
        auto lineRes = constructStraightSegment(p0, p1);
        if (!lineRes.has_value()) {
            throw CommandFailure{CommandFailureCode::InvalidArgument,
                "failed to reconstruct straight segment: " + lineRes.error().message};
        }
        std::vector<AlignmentSegment> segments;
        segments.emplace_back(std::move(*lineRes));
        auto alignRes = ReferenceAlignment::build(std::move(segments));
        if (!alignRes.has_value()) {
            std::string msg = "failed to build straight alignment: ";
            for (const auto& d : alignRes.error()) {
                msg += d.message + "; ";
            }
            throw CommandFailure{CommandFailureCode::InvalidArgument, msg};
        }
        primitiveAlignment = std::move(*alignRes);
    } else if (existing.constructionKind == RoadConstructionKind::ArcThreePoint) {
        if (fitVertices.size() != 3) {
            throw CommandFailure{CommandFailureCode::InvalidArgument,
                "arc road requires exactly 3 control vertices"};
        }
        const AlignmentPoint p0{fitVertices[0].x, fitVertices[0].y};
        const AlignmentPoint p1{fitVertices[1].x, fitVertices[1].y};
        const AlignmentPoint p2{fitVertices[2].x, fitVertices[2].y};
        auto arcRes = constructCircularArcThroughPoints(p0, p1, p2);
        if (!arcRes.has_value()) {
            throw CommandFailure{CommandFailureCode::InvalidArgument,
                "failed to reconstruct 3-point circular arc: " + arcRes.error().message};
        }
        std::vector<AlignmentSegment> segments;
        segments.emplace_back(std::move(*arcRes));
        auto alignRes = ReferenceAlignment::build(std::move(segments));
        if (!alignRes.has_value()) {
            std::string msg = "failed to build arc alignment: ";
            for (const auto& d : alignRes.error()) {
                msg += d.message + "; ";
            }
            throw CommandFailure{CommandFailureCode::InvalidArgument, msg};
        }
        primitiveAlignment = std::move(*alignRes);
    } else if (existing.constructionKind == RoadConstructionKind::Clothoid) {
        if (fitVertices.size() != 2) {
            throw CommandFailure{CommandFailureCode::InvalidArgument,
                "clothoid road requires exactly 2 control vertices"};
        }
        if (existing.segments.empty()) {
            throw CommandFailure{CommandFailureCode::InvalidArgument,
                "clothoid road has no canonical segment definition"};
        }
        const auto& seg = existing.segments[0];
        const AlignmentPoint p0{fitVertices[0].x, fitVertices[0].y};
        const AlignmentPoint p1{fitVertices[1].x, fitVertices[1].y};
        auto clothoidRes = constructClothoidReachingEndpoint(
            p0, p1, seg.startCurvature, seg.endCurvature);
        if (!clothoidRes.has_value()) {
            throw CommandFailure{CommandFailureCode::InvalidArgument,
                "failed to reconstruct clothoid reaching endpoint: " + clothoidRes.error().message};
        }
        std::vector<AlignmentSegment> segments;
        segments.emplace_back(std::move(*clothoidRes));
        auto alignRes = ReferenceAlignment::build(std::move(segments));
        if (!alignRes.has_value()) {
            std::string msg = "failed to build clothoid alignment: ";
            for (const auto& d : alignRes.error()) {
                msg += d.message + "; ";
            }
            throw CommandFailure{CommandFailureCode::InvalidArgument, msg};
        }
        primitiveAlignment = std::move(*alignRes);
    }

    for (const auto& a : existing.protectedAnchors) {
        ProtectedAnchor pa;
        pa.station = a.station;
        pa.position = a.position;
        pa.kind = a.kind;
        anchors.push_back(pa);
    }

    ReferenceAlignment alignmentToUse;
    if (primitiveAlignment.has_value()) {
        alignmentToUse = std::move(*primitiveAlignment);
    } else {
        std::vector<ConditionedVertex> polyline;
        for (const auto& v : fitVertices) {
            ConditionedVertex cv;
            cv.position = AlignmentPoint{v.x, v.y};
            polyline.push_back(cv);
        }

        auto fitResult = fitAlignmentOnly(polyline, anchors,
            positionTolerance, maxCurvature);
        if (!fitResult.alignment.has_value()) {
            std::string msg = "road refit failed: ";
            for (const auto& d : fitResult.diagnostics) {
                msg += std::string(roadErrorCodeName(d.code)) + ": " + d.message + "; ";
            }
            throw CommandFailure{CommandFailureCode::InvalidArgument, msg};
        }
        alignmentToUse = std::move(*fitResult.alignment);
        anchorBoundaries = std::move(fitResult.anchorBoundarySegments);
    }

    // Rebuild the road preserving ALL existing state except the alignment.
    Road::BuildInput roadInput;
    roadInput.id = existing.id;  // Blocker 1: preserve existing RoadId
    roadInput.displayName = existing.displayName;  // Blocker 2: preserve name
    roadInput.alignment = std::move(alignmentToUse);

    // Blocker 2: preserve source geometry and provenance.
    for (const auto& v : existing.sourceVertices) {
        roadInput.source.geometry.vertices.push_back({v.x, v.y, v.z});
    }
    roadInput.source.geometry.sourceCrs = existing.sourceCrs;
    roadInput.source.provenance.provider = existing.provider;
    roadInput.source.provenance.sourceId = existing.sourceId;
    roadInput.source.provenance.importedAt = existing.importedAt;
    for (const auto& tag : existing.sourceTags) {
        roadInput.source.provenance.tags.push_back({tag.key, tag.value});
    }
    roadInput.source.protectedAnchors = anchors;

    // Blocker 2 + Correctness: preserve elevation, superelevation, and width
    // profiles, clamping stations to the new alignment length so control edits
    // that shorten or lengthen the road never leave impossible stations beyond
    // the road endpoint.
    const double newLength = roadInput.alignment.totalLength();
    auto clampedElev = clampProfileBreakpoints(existing.elevationBreakpoints, newLength);
    auto elevProfile = buildElevationProfile(clampedElev);
    if (elevProfile.has_value()) {
        roadInput.elevation = std::move(*elevProfile);
    }
    auto clampedSuperelev = clampProfileBreakpoints(
        existing.superelevationBreakpoints, newLength);
    auto superelevProfile = buildSuperelevationProfile(clampedSuperelev);
    if (superelevProfile.has_value()) {
        roadInput.superelevation = std::move(*superelevProfile);
    }
    auto clampedWidth = clampWidthBreakpoints(existing.widthBreakpoints, newLength);
    auto widthProfile = buildRoadWidthProfile(clampedWidth);
    if (widthProfile.has_value()) {
        roadInput.width = std::move(*widthProfile);
    }

    auto road = Road::build(std::move(roadInput));
    if (!road.has_value()) {
        std::string msg = "road refit build failed: ";
        for (const auto& d : road.error()) {
            msg += std::string(roadErrorCodeName(d.code)) + ": " + d.message + "; ";
        }
        throw CommandFailure{CommandFailureCode::InvalidArgument, msg};
    }

    auto record = toRecord(*road);
    // Blocker 6: preserve the editable control vertices (which the caller
    // just modified). sourceVertices is repopulated from the Road's
    // immutable source evidence by toRecord.
    record.controlVertices = existing.controlVertices;
    // Blocker 7: preserve the fitting contract in the record so control
    // edits reuse the same tolerance/maxCurvature instead of a magic
    // constant. Update with the parameters used for this refit.
    record.positionTolerance = positionTolerance;
    record.maxCurvature = maxCurvature;
    record.anchorBoundarySegments = std::move(anchorBoundaries);
    record.constructionKind = existing.constructionKind;
    return record;
}

} // namespace infraforge::application
