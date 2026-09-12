#pragma once

#include "infraforge/domain/geo/GeoTypes.hpp"
#include "infraforge/domain/geo/ProjectGeoreference.hpp"

namespace infraforge::domain::geo {

// Stable coordinate-conversion boundary between the canonical project
// georeference and renderers (the issue #3 renderer integration contract).
//
// A frame captures a double-precision render origin expressed in
// project-global coordinates and converts positions with exact double
// arithmetic. The default frame is anchored at the canonical project
// origin; a renderer may anchor at an explicit render origin for
// camera-relative precision — that choice never mutates canonical
// project-global data.
//
// This header carries no geospatial-library dependency so render processes
// can consume it without linking PROJ. Renderers must not implement their
// own CRS or origin logic; converting render-local values to GPU precision
// happens only at the render boundary.
class RenderLocalFrame {
public:
    // Frame anchored at the canonical project origin.
    [[nodiscard]] static constexpr RenderLocalFrame atProjectOrigin(
        const ProjectGeoreference& georeference) noexcept {
        return RenderLocalFrame{georeference.origin};
    }

    // Frame anchored at an explicit render origin in project-global space.
    [[nodiscard]] static constexpr RenderLocalFrame atRenderOrigin(
        ProjectGlobalPosition renderOrigin) noexcept {
        return RenderLocalFrame{renderOrigin};
    }

    [[nodiscard]] constexpr ProjectGlobalPosition renderOrigin() const noexcept {
        return renderOrigin_;
    }

    [[nodiscard]] constexpr RenderLocalPosition toRenderLocal(
        ProjectGlobalPosition position) const noexcept {
        return RenderLocalPosition{
            position.easting - renderOrigin_.easting,
            position.northing - renderOrigin_.northing,
            position.height - renderOrigin_.height};
    }

    [[nodiscard]] constexpr ProjectGlobalPosition toProjectGlobal(
        RenderLocalPosition position) const noexcept {
        return ProjectGlobalPosition{
            position.x + renderOrigin_.easting,
            position.y + renderOrigin_.northing,
            position.z + renderOrigin_.height};
    }

private:
    constexpr explicit RenderLocalFrame(ProjectGlobalPosition renderOrigin) noexcept
        : renderOrigin_(renderOrigin) {}

    ProjectGlobalPosition renderOrigin_;
};

} // namespace infraforge::domain::geo
