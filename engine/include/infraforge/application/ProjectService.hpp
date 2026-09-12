#pragma once

#include "infraforge/domain/geo/ProjectGeoreference.hpp"
#include "infraforge/domain/project/ProjectModel.hpp"
#include "infraforge/ports/ProjectStore.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace infraforge::domain::geo {
class GeoTransformService;
}

namespace infraforge::application {

// Application-level failure taxonomy; the transport layer maps these to
// protocol error codes.
enum class CommandFailureCode : std::uint8_t {
    InvalidArgument,
    ProjectNotOpen,
    ProjectAlreadyOpen,
    ProjectDirectoryInvalid,
    ProjectFormatUnsupported,
    SchemaVersionUnsupported,
    PersistenceFailure,
    Internal,
    // Well-formed georeference/transform request that the canonical
    // geospatial engine cannot satisfy (unsupported CRS kind, unavailable
    // vertical transform, unsupported unit).
    GeoUnsupported,
};

class CommandFailure : public std::runtime_error {
public:
    CommandFailure(CommandFailureCode code, std::string message)
        : std::runtime_error(std::move(message)),
          code_(code) {}

    [[nodiscard]] CommandFailureCode code() const noexcept { return code_; }

private:
    CommandFailureCode code_;
};

enum class ProjectEventKind : std::uint8_t {
    Opened,
    Closed,
    RevisionChanged,
    DirtyStateChanged,
    GeoreferenceChanged,
};

struct ProjectEvent {
    ProjectEventKind kind;
    // State snapshot after the change. For Closed only `uuid` is meaningful.
    domain::project::ProjectRecord record;
    // Resolved georeference snapshot carried by GeoreferenceChanged events.
    std::optional<domain::geo::ProjectGeoreference> georeference = std::nullopt;
};

struct ProjectCommandResult {
    domain::project::ProjectRecord record;
    std::vector<ProjectEvent> events;
    // True when the session ended with this command (close); the transport
    // layer then answers with the closed result shape instead of a summary.
    bool sessionClosed{false};
};

// Use-case layer for the canonical project lifecycle. Validates command
// arguments, drives the persistence port, and derives the events the
// transport layer broadcasts. Runs on the single application executor.
class ProjectService final {
public:
    ProjectService(ports::ProjectStore& store, const domain::geo::GeoTransformService& transforms);

    [[nodiscard]] ProjectCommandResult create(const domain::project::CreateProjectSpec& spec);
    [[nodiscard]] ProjectCommandResult open(const std::filesystem::path& projectDirectory);
    [[nodiscard]] ProjectCommandResult save();
    [[nodiscard]] ProjectCommandResult saveAs(const domain::project::SaveAsSpec& spec);
    [[nodiscard]] ProjectCommandResult close();
    [[nodiscard]] ProjectCommandResult getSummary() const;

private:
    ports::ProjectStore& store_;
    const domain::geo::GeoTransformService& transforms_;
};

} // namespace infraforge::application
