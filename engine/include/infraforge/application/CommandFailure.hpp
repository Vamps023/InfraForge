#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

namespace infraforge::application {

// Application-level failure taxonomy; the transport layer maps these to
// protocol error codes. Shared by all application services.
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
    // Well-formed terrain request that cannot be performed as asked
    // (unsupported raster configuration, corrupt source, missing
    // project-owned storage, failed tile generation).
    TerrainUnsupported,
    // A referenced entity (job, dataset) does not exist.
    NotFound,
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

} // namespace infraforge::application
