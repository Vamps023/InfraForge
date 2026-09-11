#pragma once

#include <compare>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace infraforge::domain::project {

// Project-level traffic semantics. Stored separately from coordinate
// transforms; it never influences geospatial conversion.
enum class TrafficSide : std::uint8_t {
    Left,
    Right,
};

enum class AxisConvention : std::uint8_t {
    EastingNorthingUp,
};

// The single canonical georeference configuration owned by the Geo domain.
// All spatial conversion must go through the shared geo service once it
// exists; no consumer may keep an independent CRS/origin model.
struct GeoreferenceConfig {
    std::string horizontalCrs;
    std::string linearUnit;
    AxisConvention axisConvention{AxisConvention::EastingNorthingUp};
    double originEasting{0.0};
    double originNorthing{0.0};
    std::string verticalCrs;

    friend bool operator==(const GeoreferenceConfig&, const GeoreferenceConfig&) = default;
};

struct ValidationError {
    std::string field;
    std::string message;
};

struct CreateProjectSpec {
    std::string displayName;
    // Existing directory in which <displayName>.iforge is created.
    std::filesystem::path parentDirectory;
    GeoreferenceConfig georeference;
    TrafficSide trafficSide{TrafficSide::Right};
};

struct SaveAsSpec {
    std::string displayName;
    // Existing directory in which the new <displayName>.iforge is created.
    std::filesystem::path parentDirectory;
};

// Projection of the canonical open-project session state as persisted in
// project.json and project_state. `directory` is the UTF-8 location of the
// .iforge directory for the active session; it is session context, not a
// persisted column.
struct ProjectRecord {
    std::string uuid;
    std::string displayName;
    std::string directory;
    std::uint64_t revision{0};
    std::uint64_t savedRevision{0};
    TrafficSide trafficSide{TrafficSide::Right};
    GeoreferenceConfig georeference;
    std::string createdAt;
    std::string modifiedAt;

    [[nodiscard]] bool isDirty() const noexcept { return revision != savedRevision; }

    friend bool operator==(const ProjectRecord&, const ProjectRecord&) = default;
};

[[nodiscard]] std::string_view trafficSideName(TrafficSide side) noexcept;
[[nodiscard]] std::optional<TrafficSide> trafficSideFromName(std::string_view name) noexcept;

[[nodiscard]] std::string_view axisConventionName(AxisConvention convention) noexcept;
[[nodiscard]] std::optional<AxisConvention> axisConventionFromName(std::string_view name) noexcept;

// Project display names become directory names (<name>.iforge) and must be
// portable across supported platforms without sanitization surprises.
[[nodiscard]] std::optional<ValidationError> validateDisplayName(std::string_view name);
[[nodiscard]] std::optional<ValidationError> validateGeoreference(const GeoreferenceConfig& georeference);

} // namespace infraforge::domain::project
