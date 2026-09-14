#pragma once

#include "infraforge/application/CommandFailure.hpp"
#include "infraforge/application/WorldState.hpp"
#include "infraforge/domain/road/Road.hpp"
#include "infraforge/domain/road/RoadRecord.hpp"
#include "infraforge/domain/road/RoadSource.hpp"
#include "infraforge/domain/road/RoadTessellation.hpp"
#include "infraforge/domain/world/Invalidation.hpp"
#include "infraforge/domain/world/SpatialBounds.hpp"
#include "infraforge/ports/ProjectStore.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace infraforge::application {

// Events produced by road use cases, marshalled onto the application
// executor and translated into protocol events by the transport layer.
struct RoadServiceEvent {
    enum class Kind { Created, Updated, Removed, GeometryChanged };
    Kind kind{Kind::Created};
    std::string roadId;
    std::uint64_t revision{0};
    // Affected world chunks for geometry changes.
    std::vector<domain::world::ChunkCoord> affectedChunks;
};

// Lightweight road summary projection for the outliner and road list.
struct RoadSummary {
    std::string roadId;
    std::string name;
    double length{0.0};
    std::uint32_t alignmentSegmentCount{0};
    std::string sourceProvider;
    std::string sourceId;
    std::uint32_t protectedAnchorCount{0};
    std::uint64_t revision{0};
};

// One alignment segment in a road projection.
struct RoadAlignmentSegmentInfo {
    std::string kind;  // "line", "arc", "clothoid"
    double startStation{0.0};
    double length{0.0};
    double startCurvature{0.0};
    double endCurvature{0.0};
};

// Detailed road projection for the inspector.
struct RoadDetails {
    std::string roadId;
    std::string name;
    double length{0.0};
    std::uint32_t alignmentSegmentCount{0};
    std::vector<RoadAlignmentSegmentInfo> alignmentSegments;
    bool hasElevationProfile{false};
    std::uint32_t elevationBreakpointCount{0};
    bool hasSuperelevationProfile{false};
    std::uint32_t superelevationBreakpointCount{0};
    std::string sourceProvider;
    std::string sourceId;
    std::string sourceCrs;
    std::uint32_t protectedAnchorCount{0};
    bool isValid{false};
    std::vector<domain::road::RoadDiagnostic> diagnostics;
    std::uint64_t revision{0};
};

// Input for creating a road from a source polyline.
struct CreateRoadInput {
    std::string name;
    std::vector<domain::road::AlignmentPoint> sourcePoints;
    std::vector<std::optional<double>> sourceElevations;
    double positionTolerance{1.0};
    std::optional<double> maxCurvature;
    std::vector<std::uint32_t> protectedAnchorIndices;
};

// Input for inserting a control point.
struct InsertControlInput {
    std::string roadId;
    std::uint32_t insertBeforeIndex{0};
    domain::road::AlignmentPoint position;
    std::optional<double> elevation;
};

// Input for moving a control point.
struct MoveControlInput {
    std::string roadId;
    std::uint32_t controlIndex{0};
    domain::road::AlignmentPoint position;
    std::optional<double> elevation;
};

// Input for deleting a control point.
struct DeleteControlInput {
    std::string roadId;
    std::uint32_t controlIndex{0};
};

// Input for fitting/refitting a road.
struct FitSourceInput {
    std::string roadId;
    double positionTolerance{1.0};
    std::optional<double> maxCurvature;
};

// Input for updating the elevation profile.
struct UpdateElevationInput {
    std::string roadId;
    std::vector<double> stations;
    std::vector<double> elevations;
};

// Input for updating the superelevation profile.
struct UpdateSuperelevationInput {
    std::string roadId;
    std::vector<double> stations;
    std::vector<double> superelevations;
};

// Application boundary of the Road domain: canonical road CRUD, fitting,
// editing, undo/redo, and projection. Runs on the single application
// executor; all mutations go through the ProjectStore transactionally.
class RoadService {
public:
    using EventSink = std::function<void(const RoadServiceEvent&)>;

    RoadService(ports::ProjectStore& store, WorldState& world, EventSink eventSink);

    // Rebuilds session state after project create/open.
    void onProjectOpened();
    void onProjectClosed();

    // ---- Commands ----

    [[nodiscard]] RoadSummary createRoad(const CreateRoadInput& input);
    [[nodiscard]] RoadSummary deleteRoad(const std::string& roadId);
    [[nodiscard]] RoadSummary renameRoad(const std::string& roadId, const std::string& name);
    [[nodiscard]] RoadSummary insertControl(const InsertControlInput& input);
    [[nodiscard]] RoadSummary moveControl(const MoveControlInput& input);
    [[nodiscard]] RoadSummary deleteControl(const DeleteControlInput& input);
    [[nodiscard]] RoadSummary fitSource(const FitSourceInput& input);
    [[nodiscard]] RoadSummary updateElevation(const UpdateElevationInput& input);
    [[nodiscard]] RoadSummary updateSuperelevation(const UpdateSuperelevationInput& input);

    // ---- Undo/Redo ----

    [[nodiscard]] bool undo(const std::string& roadId);
    [[nodiscard]] bool redo(const std::string& roadId);
    [[nodiscard]] bool canUndo(const std::string& roadId) const;
    [[nodiscard]] bool canRedo(const std::string& roadId) const;

    // ---- Projections ----

    [[nodiscard]] std::vector<RoadSummary> listRoads() const;
    [[nodiscard]] std::optional<RoadDetails> getRoad(const std::string& roadId) const;
    [[nodiscard]] std::optional<RoadSummary> getRoadSummary(const std::string& roadId) const;

    // Produces the derived tessellation for a road. Returns nullopt when
    // the road does not exist. The tessellation is derived data, not
    // canonical state; it is recomputed on demand from the alignment and
    // vertical profiles.
    [[nodiscard]] std::optional<domain::road::RoadTessellation> getRoadTessellation(
        const std::string& roadId,
        const domain::road::RoadTessellationParams& params = {}) const;

private:
    // Undo/redo history entry: stores the full road record before/after.
    struct HistoryEntry {
        std::string roadId;
        // The road record before the command (for undo).
        std::optional<domain::road::RoadRecord> before;
        // The road record after the command (for redo).
        std::optional<domain::road::RoadRecord> after;
        // Whether the road existed before the command.
        bool existedBefore{false};
    };

    // Records a history entry for undo/redo.
    void recordHistory(HistoryEntry entry);

    // Computes the spatial bounds of a road from its alignment.
    [[nodiscard]] domain::world::SpatialBounds computeRoadBounds(
        const domain::road::RoadRecord& road) const;

    // Converts a RoadRecord to a RoadSummary.
    [[nodiscard]] RoadSummary toSummary(const domain::road::RoadRecord& road) const;

    // Converts a RoadRecord to RoadDetails.
    [[nodiscard]] RoadDetails toDetails(const domain::road::RoadRecord& road) const;

    // Rebuilds a Road from a record and returns it (or throws on failure).
    [[nodiscard]] domain::road::Road rebuildRoad(const domain::road::RoadRecord& record) const;

    // Fits a road from source vertices and returns the road record.
    [[nodiscard]] domain::road::RoadRecord fitRoad(
        const std::string& name,
        const std::vector<domain::road::ConditionedVertex>& polyline,
        const std::vector<domain::road::ProtectedAnchor>& anchors,
        double positionTolerance,
        std::optional<double> maxCurvature) const;

    // Refits an existing road from its source vertices with new parameters.
    [[nodiscard]] domain::road::RoadRecord refitRoad(
        const domain::road::RoadRecord& existing,
        double positionTolerance,
        std::optional<double> maxCurvature) const;

    ports::ProjectStore& store_;
    WorldState& world_;
    EventSink eventSink_;

    // Per-road undo/redo stacks.
    std::unordered_map<std::string, std::vector<HistoryEntry>> undoStacks_;
    std::unordered_map<std::string, std::vector<HistoryEntry>> redoStacks_;
};

} // namespace infraforge::application
