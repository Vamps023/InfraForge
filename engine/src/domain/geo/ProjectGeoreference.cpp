#include "infraforge/domain/geo/ProjectGeoreference.hpp"

namespace infraforge::domain::geo {

std::string_view crsKindName(const CrsKind kind) noexcept {
    switch (kind) {
    case CrsKind::Projected:
        return "PROJECTED_CRS";
    case CrsKind::Engineering:
        return "ENGINEERING_CRS";
    case CrsKind::Geographic:
        return "GEOGRAPHIC_CRS";
    case CrsKind::Geocentric:
        return "GEOCENTRIC_CRS";
    case CrsKind::Vertical:
        return "VERTICAL_CRS";
    case CrsKind::Compound:
        return "COMPOUND_CRS";
    case CrsKind::Other:
        break;
    }
    return "OTHER";
}

} // namespace infraforge::domain::geo
