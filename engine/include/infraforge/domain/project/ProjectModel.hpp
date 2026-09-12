#pragma once

#include "infraforge/domain/ValidationError.hpp"
#include "infraforge/domain/geo/GeoreferenceConfig.hpp"

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

struct CreateProjectSpec {
    std::string displayName;
    // Existing directory in which <displayName>.iforge is created.
    std::filesystem::path parentDirectory;
    geo::GeoreferenceConfig georeference;
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
    geo::GeoreferenceConfig georeference;
    std::string createdAt;
    std::string modifiedAt;

    [[nodiscard]] bool isDirty() const noexcept { return revision != savedRevision; }

    friend bool operator==(const ProjectRecord&, const ProjectRecord&) = default;
};

[[nodiscard]] std::string_view trafficSideName(TrafficSide side) noexcept;
[[nodiscard]] std::optional<TrafficSide> trafficSideFromName(std::string_view name) noexcept;

// Project display names become directory names (<name>.iforge) and must be
// portable across supported platforms without sanitization surprises.
[[nodiscard]] std::optional<ValidationError> validateDisplayName(std::string_view name);

} // namespace infraforge::domain::project
