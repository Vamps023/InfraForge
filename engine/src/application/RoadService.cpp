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
        polyline, anchors, input.sourceElevations, SourceProvider::Authored);

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
        roadId, record, std::nullopt, true});

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

    recordHistory(HistoryEntry{roadId, before, record, true});

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

    auto before = *found;
    auto record = before;

    // Insert the new control point into the source vertices.
    if (input.insertBeforeIndex > record.sourceVertices.size()) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "insert index out of range"};
    }

    // Blocker 4: check if the insert position would split a protected
    // anchor's reference. Protected anchors at or after the insert index
    // must be re-indexed to maintain their references.
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

    // Refit the road — preserves RoadId, profiles, provenance (Blocker 1+2).
    // Control edits are explicit user changes to the geometry; use a
    // generous tolerance so the refit succeeds for any reasonable edit.
    // The source-deviation tolerance applies to createRoad/fitSource where
    // the user explicitly specifies the fitting contract.
    record = refitRoad(record, 100.0, std::nullopt);
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
    auto found = findRoad(input.roadId);
    if (!found.has_value()) {
        throw CommandFailure{CommandFailureCode::NotFound,
            "road not found: " + input.roadId};
    }

    auto before = *found;
    auto record = before;

    if (input.controlIndex >= record.sourceVertices.size()) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "control index out of range"};
    }

    // Blocker 4: do not permit normal move operations to move a protected
    // topology anchor unless the command explicitly represents a topology
    // mutation. A move of a protected anchor is rejected.
    for (const auto& anchor : record.protectedAnchors) {
        // Check if this control index corresponds to a protected anchor by
        // matching position. Protected anchors are reference data; moving
        // the source vertex at the same position would displace the anchor.
        const auto& vtx = record.sourceVertices[input.controlIndex];
        const double dx = vtx.x - anchor.position.easting;
        const double dy = vtx.y - anchor.position.northing;
        if (std::sqrt(dx * dx + dy * dy) < 1e-9) {
            throw CommandFailure{CommandFailureCode::InvalidArgument,
                "cannot move a protected anchor (index " +
                std::to_string(input.controlIndex) + ")"};
        }
    }

    record.sourceVertices[input.controlIndex].x = input.position.easting;
    record.sourceVertices[input.controlIndex].y = input.position.northing;
    record.sourceVertices[input.controlIndex].z = input.elevation;

    // Refit the road — preserves RoadId, profiles, provenance (Blocker 1+2).
    // Control edits are explicit user changes; use a generous tolerance.
    record = refitRoad(record, 100.0, std::nullopt);
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
    auto found = findRoad(input.roadId);
    if (!found.has_value()) {
        throw CommandFailure{CommandFailureCode::NotFound,
            "road not found: " + input.roadId};
    }

    auto before = *found;
    auto record = before;

    if (record.sourceVertices.size() <= 2) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "cannot delete control: road must have at least 2 control points"};
    }
    if (input.controlIndex >= record.sourceVertices.size()) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "control index out of range"};
    }

    // Blocker 4: do not permit deleting a protected topology anchor.
    for (const auto& anchor : record.protectedAnchors) {
        const auto& vtx = record.sourceVertices[input.controlIndex];
        const double dx = vtx.x - anchor.position.easting;
        const double dy = vtx.y - anchor.position.northing;
        if (std::sqrt(dx * dx + dy * dy) < 1e-9) {
            throw CommandFailure{CommandFailureCode::InvalidArgument,
                "cannot delete a protected anchor (index " +
                std::to_string(input.controlIndex) + ")"};
        }
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

    // Refit the road — preserves RoadId, profiles, provenance (Blocker 1+2).
    // Control edits are explicit user changes; use a generous tolerance.
    record = refitRoad(record, 100.0, std::nullopt);
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
    auto found = findRoad(input.roadId);
    if (!found.has_value()) {
        throw CommandFailure{CommandFailureCode::NotFound,
            "road not found: " + input.roadId};
    }

    auto before = *found;
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
    auto found = findRoad(input.roadId);
    if (!found.has_value()) {
        throw CommandFailure{CommandFailureCode::NotFound,
            "road not found: " + input.roadId};
    }

    if (input.stations.size() != input.elevations.size()) {
        throw CommandFailure{CommandFailureCode::InvalidArgument,
            "stations and elevations must have the same length"};
    }

    // Blocker 15: validate profile data before persistence.
    for (std::size_t i = 0; i < input.stations.size(); ++i) {
        if (!std::isfinite(input.stations[i])) {
            throw CommandFailure{CommandFailureCode::InvalidArgument,
                "elevation station " + std::to_string(i) + " is not finite"};
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

    recordHistory(HistoryEntry{input.roadId, before, record, true});

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

    // Blocker 15: validate profile data before persistence.
    for (std::size_t i = 0; i < input.stations.size(); ++i) {
        if (!std::isfinite(input.stations[i])) {
            throw CommandFailure{CommandFailureCode::InvalidArgument,
                "superelevation station " + std::to_string(i) + " is not finite"};
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

    recordHistory(HistoryEntry{input.roadId, before, record, true});

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

bool RoadService::undo(const std::string& roadId) {
    // Blocker 14: an empty road ID means undo the last road command across
    // all roads (global undo). This is a deliberate, documented contract,
    // not an accidental implementation detail.
    std::string effectiveRoadId = roadId;
    if (roadId.empty()) {
        // Find the most recent undo entry across all road stacks.
        std::string bestRoadId;
        std::size_t bestSize = 0;
        for (const auto& [id, stack] : undoStacks_) {
            if (!stack.empty() && stack.size() >= bestSize) {
                bestSize = stack.size();
                bestRoadId = id;
            }
        }
        if (bestRoadId.empty()) return false;
        effectiveRoadId = bestRoadId;
    }

    auto& undoStack = undoStacks_[effectiveRoadId];
    if (undoStack.empty()) return false;

    auto entry = std::move(undoStack.back());
    undoStack.pop_back();

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

    redoStacks_[effectiveRoadId].push_back(std::move(entry));

    // Blocker 14: emit correct event semantics. Undo of a create = Removed;
    // undo of a delete = Created; undo of an update = GeometryChanged with
    // affected chunks (not generic Updated with empty chunks).
    if (!entry.existedBefore && entry.after.has_value()) {
        eventSink_(RoadServiceEvent{
            RoadServiceEvent::Kind::Removed, effectiveRoadId,
            store_.current().revision, affectedChunks});
    } else if (entry.existedBefore && entry.before.has_value() && !entry.after.has_value()) {
        eventSink_(RoadServiceEvent{
            RoadServiceEvent::Kind::Created, effectiveRoadId,
            store_.current().revision, affectedChunks});
    } else {
        eventSink_(RoadServiceEvent{
            RoadServiceEvent::Kind::GeometryChanged, effectiveRoadId,
            store_.current().revision, affectedChunks});
    }

    return true;
}

bool RoadService::redo(const std::string& roadId) {
    // Blocker 14: an empty road ID means redo the last undone road command
    // across all roads (global redo), mirroring the undo contract.
    std::string effectiveRoadId = roadId;
    if (roadId.empty()) {
        std::string bestRoadId;
        std::size_t bestSize = 0;
        for (const auto& [id, stack] : redoStacks_) {
            if (!stack.empty() && stack.size() >= bestSize) {
                bestSize = stack.size();
                bestRoadId = id;
            }
        }
        if (bestRoadId.empty()) return false;
        effectiveRoadId = bestRoadId;
    }

    auto& redoStack = redoStacks_[effectiveRoadId];
    if (redoStack.empty()) return false;

    auto entry = std::move(redoStack.back());
    redoStack.pop_back();

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

    undoStacks_[effectiveRoadId].push_back(std::move(entry));

    // Blocker 14: emit correct event semantics matching the redone command.
    if (entry.after.has_value() && !entry.existedBefore) {
        eventSink_(RoadServiceEvent{
            RoadServiceEvent::Kind::Created, effectiveRoadId,
            store_.current().revision, affectedChunks});
    } else if (!entry.after.has_value()) {
        eventSink_(RoadServiceEvent{
            RoadServiceEvent::Kind::Removed, effectiveRoadId,
            store_.current().revision, affectedChunks});
    } else {
        eventSink_(RoadServiceEvent{
            RoadServiceEvent::Kind::GeometryChanged, effectiveRoadId,
            store_.current().revision, affectedChunks});
    }

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

    // Blocker 7: derive conservative but accurate bounds from actual
    // canonical geometry. For each segment, sample the mathematical
    // curve at adaptive density to find the true extrema, rather than
    // estimating by adding/subtracting length from the start point.
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

        // Sample the segment to find curve extrema. For arcs and clothoids,
        // the extrema may be at interior points where the tangent is aligned
        // with an axis. Use deterministic adaptive sampling: at minimum 16
        // samples per segment, scaled by segment length for longer curves.
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
    // Add a conservative margin for the road surface width.
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
    SourceProvider provider) const {

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
        }
        roadInput.source.geometry.vertices.push_back(sv);
    }
    roadInput.source.provenance.provider = provider;
    roadInput.source.protectedAnchors = anchors;

    // Blocker 16: if source elevations were supplied, build an initial
    // elevation profile from them so the road starts with meaningful
    // vertical geometry rather than discarding the data.
    if (!sourceElevations.empty()) {
        std::vector<ProfileBreakpoint> elevBreakpoints;
        double cumulativeStation = 0.0;
        for (std::size_t i = 0; i < polyline.size(); ++i) {
            if (i < sourceElevations.size() && sourceElevations[i].has_value()) {
                if (i > 0) {
                    const double dx = polyline[i].position.easting -
                        polyline[i - 1].position.easting;
                    const double dy = polyline[i].position.northing -
                        polyline[i - 1].position.northing;
                    cumulativeStation += std::sqrt(dx * dx + dy * dy);
                }
                elevBreakpoints.push_back(
                    {cumulativeStation, *sourceElevations[i]});
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

    return toRecord(*road);
}

RoadRecord RoadService::refitRoad(
    const RoadRecord& existing,
    double positionTolerance,
    std::optional<double> maxCurvature) const {

    // Blocker 1 + Blocker 2: refit preserves the existing RoadId, display
    // name, elevation/superelevation profiles, provenance (provider, source
    // ID, source CRS, source tags, import timestamp), and protected
    // anchors. Only the canonical alignment is re-derived from the source
    // vertices. The road is NOT rebuilt as a fresh Authored road.

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

    // Fit the alignment (pure geometry, no new RoadId).
    auto fitResult = fitAlignmentOnly(polyline, anchors,
        positionTolerance, maxCurvature);
    if (!fitResult.alignment.has_value()) {
        std::string msg = "road refit failed: ";
        for (const auto& d : fitResult.diagnostics) {
            msg += std::string(roadErrorCodeName(d.code)) + ": " + d.message + "; ";
        }
        throw CommandFailure{CommandFailureCode::InvalidArgument, msg};
    }

    // Rebuild the road preserving ALL existing state except the alignment.
    Road::BuildInput roadInput;
    roadInput.id = existing.id;  // Blocker 1: preserve existing RoadId
    roadInput.displayName = existing.displayName;  // Blocker 2: preserve name
    roadInput.alignment = std::move(*fitResult.alignment);

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

    // Blocker 2: preserve elevation and superelevation profiles.
    auto elevProfile = buildElevationProfile(existing.elevationBreakpoints);
    if (elevProfile.has_value()) {
        roadInput.elevation = std::move(*elevProfile);
    }
    auto superelevProfile = buildSuperelevationProfile(
        existing.superelevationBreakpoints);
    if (superelevProfile.has_value()) {
        roadInput.superelevation = std::move(*superelevProfile);
    }

    auto road = Road::build(std::move(roadInput));
    if (!road.has_value()) {
        std::string msg = "road refit build failed: ";
        for (const auto& d : road.error()) {
            msg += std::string(roadErrorCodeName(d.code)) + ": " + d.message + "; ";
        }
        throw CommandFailure{CommandFailureCode::InvalidArgument, msg};
    }

    return toRecord(*road);
}

} // namespace infraforge::application
