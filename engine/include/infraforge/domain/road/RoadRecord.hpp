#pragma once

#include "infraforge/domain/road/LaneTypes.hpp"
#include "infraforge/domain/road/Road.hpp"

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace infraforge::domain::road {

// Serializable canonical road record for persistence. This is the exact
// data that survives save/reopen: stable identity, display name, ordered
// alignment segments, elevation/superelevation breakpoints, lane sections,
// and optional source geometry/provenance with protected anchors.
//
// The record is a flat projection of the Road domain model: segments are
// stored as ordered rows with a kind discriminator and all possible
// parameters (type-specific columns are zero for unused types). This
// follows the explicit-schema preference over opaque blobs
// (docs/02_DATA/DATABASE_SCHEMA.md).
struct RoadSegmentRecord {
    std::uint64_t segmentIndex{0};
    AlignmentSegmentKind kind{AlignmentSegmentKind::Line};
    // Common to all segment types.
    AlignmentPoint start{};
    Heading startHeading{0.0};
    double length{0.0};
    // Circular arc: constant curvature.
    Curvature curvature{0.0};
    // Clothoid: linear curvature transition.
    Curvature startCurvature{0.0};
    Curvature endCurvature{0.0};

    friend bool operator==(const RoadSegmentRecord&, const RoadSegmentRecord&) = default;
};

struct RoadSourceVertexRecord {
    std::uint64_t index{0};
    double x{0.0};
    double y{0.0};
    std::optional<double> z;

    friend bool operator==(const RoadSourceVertexRecord&, const RoadSourceVertexRecord&) = default;
};

struct RoadSourceTagRecord {
    std::string key;
    std::string value;

    friend bool operator==(const RoadSourceTagRecord&, const RoadSourceTagRecord&) = default;
};

struct RoadProtectedAnchorRecord {
    std::uint64_t index{0};
    Station station{0.0};
    AlignmentPoint position{};
    AnchorKind kind{AnchorKind::Semantic};

    friend bool operator==(const RoadProtectedAnchorRecord&, const RoadProtectedAnchorRecord&) = default;
};

struct RoadLaneRecord {
    std::string laneId;
    std::uint32_t sectionIndex{0};
    std::string side{"right"};
    std::uint32_t laneIndex{1};
    std::string type{"driving"};
    std::string direction{"forward"};
    double width{3.5};

    friend bool operator==(const RoadLaneRecord&, const RoadLaneRecord&) = default;
};

struct RoadLaneSectionRecord {
    std::uint32_t sectionIndex{0};
    Station startStation{0.0};
    Station endStation{0.0};

    friend bool operator==(const RoadLaneSectionRecord&, const RoadLaneSectionRecord&) = default;
};

struct RoadRecord {
    RoadId id{};
    std::string displayName;
    std::vector<RoadSegmentRecord> segments;
    std::vector<ProfileBreakpoint> elevationBreakpoints;
    std::vector<ProfileBreakpoint> superelevationBreakpoints;
    std::vector<RoadWidthBreakpoint> widthBreakpoints;
    std::vector<RoadLaneSectionRecord> laneSections;
    std::vector<RoadLaneRecord> lanes;
    // Source geometry/provenance (absent for authored roads).
    bool hasSource{false};
    SourceProvider provider{SourceProvider::Authored};
    std::string sourceId;
    std::string sourceCrs;
    std::string importedAt;
    std::vector<RoadSourceTagRecord> sourceTags;
    // Immutable source evidence: the original polyline as imported or
    // authored. Never modified by control edits; preserved for recovery
    // and inspection (Blocker 6).
    std::vector<RoadSourceVertexRecord> sourceVertices;
    // Editable control geometry: the polyline the fitter uses and the
    // user manipulates via move/insert/delete. Starts as a copy of
    // sourceVertices and may diverge through edits.
    std::vector<RoadSourceVertexRecord> controlVertices;
    std::vector<RoadProtectedAnchorRecord> protectedAnchors;
    std::set<std::size_t> anchorBoundarySegments;
    // Blocker 7: persisted fitting contract so control edits reuse the
    // same source-deviation tolerance the road was created/refit with,
    // instead of a magic constant. maxCurvature is nullopt when unset.
    double positionTolerance{1.0};
    std::optional<double> maxCurvature;
    std::string createdAt;
    std::string modifiedAt;

    friend bool operator==(const RoadRecord&, const RoadRecord&) = default;
};

// Converts a canonical Road to a serializable RoadRecord.
[[nodiscard]] RoadRecord toRecord(const Road& road);

// Reconstructs a canonical Road from a RoadRecord. Validates and builds
// the alignment/profiles; returns typed diagnostics on failure.
[[nodiscard]] std::expected<Road, std::vector<RoadDiagnostic>> fromRecord(const RoadRecord& record);

} // namespace infraforge::domain::road
