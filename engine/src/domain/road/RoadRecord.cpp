#include "infraforge/domain/road/RoadRecord.hpp"

#include <utility>

namespace infraforge::domain::road {

RoadRecord toRecord(const Road& road) {
    RoadRecord record;
    record.id = road.id();
    record.displayName = road.displayName();

    // Segments.
    const auto& segs = road.alignment().segments();
    record.segments.reserve(segs.size());
    for (std::size_t i = 0; i < segs.size(); ++i) {
        const auto& stood = segs[i];
        RoadSegmentRecord sr;
        sr.segmentIndex = static_cast<std::uint64_t>(i);
        sr.kind = segmentKind(stood.segment);
        sr.start = segmentStartPoint(stood.segment);
        sr.startHeading = segmentStartHeading(stood.segment);
        sr.length = segmentLength(stood.segment);
        sr.curvature = segmentStartCurvature(stood.segment);
        sr.startCurvature = segmentStartCurvature(stood.segment);
        sr.endCurvature = segmentEndCurvature(stood.segment);
        record.segments.push_back(sr);
    }

    // Profiles.
    record.elevationBreakpoints = road.elevation().breakpoints();
    record.superelevationBreakpoints = road.superelevation().breakpoints();

    // Source.
    const auto& source = road.source();
    record.hasSource = source.provenance.provider != SourceProvider::Authored
        || !source.geometry.vertices.empty()
        || !source.protectedAnchors.empty();
    record.provider = source.provenance.provider;
    record.sourceId = source.provenance.sourceId;
    record.sourceCrs = source.geometry.sourceCrs;
    record.importedAt = source.provenance.importedAt;
    for (const auto& tag : source.provenance.tags) {
        record.sourceTags.push_back({tag.key, tag.value});
    }
    for (std::size_t i = 0; i < source.geometry.vertices.size(); ++i) {
        record.sourceVertices.push_back({static_cast<std::uint64_t>(i),
            source.geometry.vertices[i].x, source.geometry.vertices[i].y,
            source.geometry.vertices[i].z});
    }
    // controlVertices is managed by RoadService (not part of the Road
    // domain model). toRecord leaves it empty; the caller populates it.
    for (std::size_t i = 0; i < source.protectedAnchors.size(); ++i) {
        const auto& anchor = source.protectedAnchors[i];
        record.protectedAnchors.push_back({static_cast<std::uint64_t>(i),
            anchor.station, anchor.position, anchor.kind});
    }
    return record;
}

std::expected<Road, std::vector<RoadDiagnostic>> fromRecord(const RoadRecord& record) {
    // Reconstruct segments.
    std::vector<AlignmentSegment> segments;
    segments.reserve(record.segments.size());
    for (const auto& sr : record.segments) {
        switch (sr.kind) {
        case AlignmentSegmentKind::Line:
            segments.emplace_back(LineSegment{
                .start = sr.start, .heading = sr.startHeading, .length = sr.length});
            break;
        case AlignmentSegmentKind::CircularArc:
            segments.emplace_back(CircularArcSegment{
                .start = sr.start, .startHeading = sr.startHeading,
                .curvature = sr.curvature, .length = sr.length});
            break;
        case AlignmentSegmentKind::Clothoid:
            segments.emplace_back(ClothoidSegment{
                .start = sr.start, .startHeading = sr.startHeading,
                .startCurvature = sr.startCurvature, .endCurvature = sr.endCurvature,
                .length = sr.length});
            break;
        }
    }

    auto alignment = ReferenceAlignment::build(
        std::move(segments), kDefaultPositionTolerance,
        kDefaultHeadingTolerance, kDefaultCurvatureTolerance,
        record.anchorBoundarySegments);
    if (!alignment.has_value()) {
        return std::unexpected(std::move(alignment.error()));
    }

    auto elevation = buildElevationProfile(record.elevationBreakpoints);
    if (!elevation.has_value()) {
        return std::unexpected(std::vector<RoadDiagnostic>{elevation.error()});
    }
    auto superelevation = buildSuperelevationProfile(record.superelevationBreakpoints);
    if (!superelevation.has_value()) {
        return std::unexpected(std::vector<RoadDiagnostic>{superelevation.error()});
    }

    // Reconstruct source.
    RoadSource source;
    source.provenance.provider = record.provider;
    source.provenance.sourceId = record.sourceId;
    source.provenance.importedAt = record.importedAt;
    source.geometry.sourceCrs = record.sourceCrs;
    for (const auto& tag : record.sourceTags) {
        source.provenance.tags.push_back({tag.key, tag.value});
    }
    for (const auto& v : record.sourceVertices) {
        source.geometry.vertices.push_back({v.x, v.y, v.z});
    }
    for (const auto& a : record.protectedAnchors) {
        source.protectedAnchors.push_back({a.station, a.position, a.kind});
    }

    Road::BuildInput input{
        .id = record.id,
        .displayName = record.displayName,
        .alignment = std::move(*alignment),
        .elevation = std::move(*elevation),
        .superelevation = std::move(*superelevation),
        .source = std::move(source),
    };
    return Road::build(std::move(input));
}

} // namespace infraforge::domain::road
