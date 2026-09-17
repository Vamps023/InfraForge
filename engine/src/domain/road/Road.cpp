#include "infraforge/domain/road/Road.hpp"

#include <cmath>
#include <utility>

namespace infraforge::domain::road {

std::expected<Road, std::vector<RoadDiagnostic>> Road::build(BuildInput input) {
    std::vector<RoadDiagnostic> diagnostics;

    if (auto nameError = validateRoadDisplayName(input.displayName)) {
        diagnostics.push_back({RoadErrorCode::InvalidArgument, nameError->message});
    }
    if (input.alignment.isEmpty()) {
        diagnostics.push_back({RoadErrorCode::EmptyAlignment, "road alignment must not be empty"});
    }
    if (auto d = input.elevation.validate()) {
        diagnostics.push_back(*d);
    }
    if (auto d = input.superelevation.validate()) {
        diagnostics.push_back(*d);
    }
    if (auto d = input.width.validate()) {
        diagnostics.push_back(*d);
    }
    if (input.id.isNull()) {
        diagnostics.push_back({RoadErrorCode::InvalidArgument, "road id must not be null"});
    }

    // Validate protected anchors against the alignment.
    auto anchorDiagnostics = validateProtectedAnchors(input.alignment, input.source.protectedAnchors);
    for (auto& d : anchorDiagnostics) {
        diagnostics.push_back(std::move(d));
    }

    // Validate source geometry coordinates.
    auto sourceDiagnostics = validateRoadSource(input.source);
    for (auto& d : sourceDiagnostics) {
        diagnostics.push_back(std::move(d));
    }

    if (!diagnostics.empty()) {
        return std::unexpected(std::move(diagnostics));
    }
    return Road{std::move(input)};
}

Road::RoadSample Road::evaluate(const Station s) const noexcept {
    const AlignmentSample horizontal = alignment_.evaluate(s);
    RoadSample sample;
    sample.position = horizontal.position;
    sample.heading = horizontal.heading;
    sample.curvature = horizontal.curvature;
    sample.height = elevation_.evaluate(s);
    sample.crossSlope = superelevation_.evaluate(s);
    return sample;
}

std::vector<RoadDiagnostic> validateProtectedAnchors(
    const ReferenceAlignment& alignment,
    const std::vector<ProtectedAnchor>& anchors,
    const double positionTolerance) noexcept {
    std::vector<RoadDiagnostic> diagnostics;
    if (alignment.isEmpty()) {
        return diagnostics;
    }
    if (!std::isfinite(positionTolerance) || positionTolerance < 0.0) {
        diagnostics.push_back({RoadErrorCode::InvalidArgument,
            "protected anchor position tolerance must be finite and non-negative"});
        return diagnostics;
    }
    const auto range = alignment.stationRange();
    for (const auto& anchor : anchors) {
        if (!std::isfinite(anchor.station)) {
            diagnostics.push_back({RoadErrorCode::NonFiniteParameter,
                "protected anchor station is not finite"});
            continue;
        }
        if (!std::isfinite(anchor.position.easting)
            || !std::isfinite(anchor.position.northing)) {
            diagnostics.push_back({RoadErrorCode::NonFiniteParameter,
                "protected anchor position is not finite"});
            continue;
        }
        if (anchor.station < range.start || anchor.station > range.end) {
            diagnostics.push_back({RoadErrorCode::AnchorOutOfRange,
                "protected anchor station " + std::to_string(anchor.station)
                    + " is outside alignment range [" + std::to_string(range.start)
                    + ", " + std::to_string(range.end) + "]"});
            continue;
        }
        const AlignmentSample sample = alignment.evaluate(anchor.station);
        const double dx = anchor.position.easting - sample.position.easting;
        const double dy = anchor.position.northing - sample.position.northing;
        if (std::sqrt(dx * dx + dy * dy) > positionTolerance) {
            diagnostics.push_back({RoadErrorCode::PositionDiscontinuity,
                "protected anchor at station " + std::to_string(anchor.station)
                    + " does not match alignment position"});
        }
    }
    return diagnostics;
}

std::vector<RoadDiagnostic> validateRoadSource(const RoadSource& source) noexcept {
    std::vector<RoadDiagnostic> diagnostics;
    for (std::size_t i = 0; i < source.geometry.vertices.size(); ++i) {
        const auto& v = source.geometry.vertices[i];
        if (!std::isfinite(v.x)) {
            diagnostics.push_back({RoadErrorCode::NonFiniteParameter,
                "source vertex " + std::to_string(i) + " x is not finite"});
        }
        if (!std::isfinite(v.y)) {
            diagnostics.push_back({RoadErrorCode::NonFiniteParameter,
                "source vertex " + std::to_string(i) + " y is not finite"});
        }
        if (v.z.has_value() && !std::isfinite(*v.z)) {
            diagnostics.push_back({RoadErrorCode::NonFiniteParameter,
                "source vertex " + std::to_string(i) + " z is not finite (use nullopt for missing elevation)"});
        }
    }
    return diagnostics;
}

} // namespace infraforge::domain::road
