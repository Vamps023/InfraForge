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
    struct ControlPoint {
        double easting{0.0};
        double northing{0.0};
        std::optional<double> elevation;
        bool protectedAnchor{false};
    };
    std::vector<ControlPoint> controlPoints;
    double positionTolerance{1.0};
    std::optional<double> maxCurvature;
    struct ProfileBreakpoint {
        double station{0.0};
        double value{0.0};
    };
    std::vector<ProfileBreakpoint> elevationBreakpoints;
    std::vector<ProfileBreakpoint> superelevationBreakpoints;
    std::vector<domain::road::RoadWidthBreakpoint> widthBreakpoints;
};

// One vertex of a road scene mesh, in render-local float coordinates.
struct RoadSceneVertex {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
    float nx{0.0f};
    float ny{0.0f};
    float nz{1.0f};
};

// One road mesh for the viewport: a triangle list with vertices and indices.
struct RoadSceneMesh {
    std::string roadId;
    std::int64_t chunkX{0};
    std::int64_t chunkY{0};
    std::vector<RoadSceneVertex> vertices;
    std::vector<std::uint32_t> indices;
};

// Road scene projection: all road meshes in render-local coordinates.
struct RoadSceneProjection {
    double originEasting{0.0};
    double originNorthing{0.0};
    double originHeight{0.0};
    std::vector<RoadSceneMesh> meshes;
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
    std::optional<double> positionTolerance;
    std::optional<double> maxCurvature;
    bool replaceMaxCurvature{false};
};

struct RoadHistoryResult {
    std::string roadId;
    std::optional<RoadSummary> road;
    bool existsAfterOperation{false};

    explicit operator bool() const { return !roadId.empty(); }
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

struct UpdateWidthInput {
    std::string roadId;
    std::vector<double> stations;
    std::vector<double> leftWidths;
    std::vector<double> rightWidths;
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
    [[nodiscard]] RoadSummary updateWidth(const UpdateWidthInput& input);

    // ---- Undo/Redo ----

    [[nodiscard]] RoadHistoryResult undo(const std::string& roadId);
    [[nodiscard]] RoadHistoryResult redo(const std::string& roadId);
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

    // Produces the road scene projection for the viewport: all road meshes
    // in render-local float coordinates relative to the scene origin.
    // The origin is the project georeference origin.
    [[nodiscard]] RoadSceneProjection roadSceneProjection() const;

private:
    // Undo/redo history entry: stores the full road record before/after.
    // Blocker 10: each entry carries a monotonically increasing sequence
    // number so global undo/redo is chronological, not based on per-road
    // stack depth or unordered_map iteration order.
    struct HistoryEntry {
        std::uint64_t sequence{0};
        std::string roadId;
        // The road record before the command (for undo).
        std::optional<domain::road::RoadRecord> before;
        // The road record after the command (for redo).
        std::optional<domain::road::RoadRecord> after;
        // Whether the road existed before the command.
        bool existedBefore{false};
    };

    // Records a history entry for undo/redo. Appends to the single global
    // chronological undo history and clears the redo history.
    void recordHistory(HistoryEntry entry);

    // Finds the most recent undo entry for a specific road (or the global
    // most recent if roadId is empty). Returns the index into undoHistory_
    // or nullopt if none exists.
    [[nodiscard]] std::optional<std::size_t> findLastUndo(
        const std::string& roadId) const;

    // Finds the most recent redo entry for a specific road (or the global
    // most recent if roadId is empty). Returns the index into redoHistory_
    // or nullopt if none exists.
    [[nodiscard]] std::optional<std::size_t> findLastRedo(
        const std::string& roadId) const;

    // Computes the spatial bounds of a road from its alignment.
    [[nodiscard]] domain::world::SpatialBounds computeRoadBounds(
        const domain::road::RoadRecord& road) const;

    // Converts a RoadRecord to a RoadSummary.
    [[nodiscard]] RoadSummary toSummary(const domain::road::RoadRecord& road) const;

    // Converts a RoadRecord to RoadDetails.
    [[nodiscard]] RoadDetails toDetails(const domain::road::RoadRecord& road) const;

    // Rebuilds a Road from a record and returns it (or throws on failure).
    [[nodiscard]] domain::road::Road rebuildRoad(const domain::road::RoadRecord& record) const;

    // Finds a road record by its uuid-text id. Returns nullopt if not found.
    // This is the single lookup path used by all mutating commands so the
    // service does not reload every road for every command (Blocker 17).
    [[nodiscard]] std::optional<domain::road::RoadRecord> findRoad(
        const std::string& roadId) const;

    // Fits a canonical ReferenceAlignment from conditioned source vertices.
    // This is the pure geometry step, separate from entity creation: it
    // returns the fitted alignment without minting a RoadId or building a
    // Road record (Blocker 1: only createRoad mints a new RoadId).
    [[nodiscard]] domain::road::AlignmentFitResult fitAlignmentOnly(
        const std::vector<domain::road::ConditionedVertex>& polyline,
        const std::vector<domain::road::ProtectedAnchor>& anchors,
        double positionTolerance,
        std::optional<double> maxCurvature) const;

    // Builds a new road record from a fitted alignment, minting a new RoadId.
    // Used only by createRoad. The source vertices and provenance are set
    // for the new entity.
    [[nodiscard]] domain::road::RoadRecord buildNewRoadRecord(
        const std::string& name,
        const domain::road::ReferenceAlignment& alignment,
        const std::vector<domain::road::ConditionedVertex>& polyline,
        const std::vector<domain::road::ProtectedAnchor>& anchors,
        const std::vector<std::optional<double>>& sourceElevations,
        domain::road::SourceProvider provider,
        double positionTolerance,
        std::optional<double> maxCurvature,
        const std::set<std::size_t>& anchorBoundarySegments) const;

    // Refits an existing road from its source vertices with new parameters.
    // Preserves the existing RoadId, display name, elevation/superelevation
    // profiles, provenance, source tags, and protected anchors. Only the
    // canonical alignment is re-derived (Blocker 1 + Blocker 2).
    [[nodiscard]] domain::road::RoadRecord refitRoad(
        const domain::road::RoadRecord& existing,
        double positionTolerance,
        std::optional<double> maxCurvature) const;

    ports::ProjectStore& store_;
    WorldState& world_;
    EventSink eventSink_;

    // Blocker 10: single global chronological undo/redo history. Each entry
    // carries a monotonically increasing sequence number so global undo
    // pops the most recent command across ALL roads, not the road with the
    // deepest per-road stack. Per-road undo/redo filters by roadId.
    std::uint64_t nextSequence_{1};
    std::vector<HistoryEntry> undoHistory_;
    std::vector<HistoryEntry> redoHistory_;

    struct CachedRoadMeshes {
        domain::road::RoadRecord record;
        std::vector<RoadSceneMesh> meshes;
    };
    // Derived-only cache. Canonical road state remains in SQLite; unchanged
    // roads retain their tessellation across scene publications.
    mutable std::unordered_map<std::string, CachedRoadMeshes> sceneMeshCache_;
};

} // namespace infraforge::application
