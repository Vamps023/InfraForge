#pragma once

#include "infraforge/domain/project/ProjectModel.hpp"
#include "infraforge/ports/ProjectStore.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

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
};

struct ProjectEvent {
    ProjectEventKind kind;
    // State snapshot after the change. For Closed only `uuid` is meaningful.
    domain::project::ProjectRecord record;
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
    explicit ProjectService(ports::ProjectStore& store);

    [[nodiscard]] ProjectCommandResult create(const domain::project::CreateProjectSpec& spec);
    [[nodiscard]] ProjectCommandResult open(const std::filesystem::path& projectDirectory);
    [[nodiscard]] ProjectCommandResult save();
    [[nodiscard]] ProjectCommandResult saveAs(const domain::project::SaveAsSpec& spec);
    [[nodiscard]] ProjectCommandResult close();
    [[nodiscard]] ProjectCommandResult getSummary() const;

private:
    ports::ProjectStore& store_;
};

} // namespace infraforge::application
