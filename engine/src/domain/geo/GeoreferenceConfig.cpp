#include "infraforge/domain/geo/GeoreferenceConfig.hpp"

#include <cmath>

namespace infraforge::domain::geo {

std::string_view axisConventionName(const AxisConvention convention) noexcept {
    switch (convention) {
    case AxisConvention::EastingNorthingUp:
        return "easting_northing_up";
    }
    return "";
}

std::optional<AxisConvention> axisConventionFromName(const std::string_view name) noexcept {
    if (name == "easting_northing_up") {
        return AxisConvention::EastingNorthingUp;
    }
    return std::nullopt;
}

std::optional<ValidationError> validateGeoreference(const GeoreferenceConfig& georeference) {
    if (georeference.horizontalCrs.empty()) {
        return ValidationError{.field = "georeference.horizontal_crs", .message = "horizontal CRS must not be empty"};
    }
    if (georeference.horizontalCrs.size() > 8192) {
        return ValidationError{.field = "georeference.horizontal_crs", .message = "horizontal CRS definition is too long"};
    }
    if (georeference.linearUnit.empty()) {
        return ValidationError{.field = "georeference.linear_unit", .message = "linear unit must not be empty"};
    }
    if (georeference.linearUnit.size() > 64) {
        return ValidationError{.field = "georeference.linear_unit", .message = "linear unit identifier is too long"};
    }
    if (georeference.axisConvention != AxisConvention::EastingNorthingUp) {
        return ValidationError{.field = "georeference.axis_convention", .message = "axis convention is unspecified"};
    }
    if (!std::isfinite(georeference.originEasting) || !std::isfinite(georeference.originNorthing)
        || !std::isfinite(georeference.originHeight)) {
        return ValidationError{.field = "georeference.origin", .message = "project origin must be finite"};
    }
    if (georeference.verticalCrs.size() > 8192) {
        return ValidationError{.field = "georeference.vertical_crs", .message = "vertical CRS definition is too long"};
    }
    return std::nullopt;
}

} // namespace infraforge::domain::geo
