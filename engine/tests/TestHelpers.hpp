#pragma once

#include "infraforge/domain/geo/GeoTransformService.hpp"
#include "infraforge/domain/geo/GeoreferenceConfig.hpp"
#include "infraforge/domain/project/ProjectModel.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <utility>

namespace infraforge::testhelpers {

// Runs fn and captures the given exception type; returns nullopt when fn
// completed without throwing. Used to assert both throw-ness and error
// payload of failure paths.
template <typename ExceptionType, typename Fn>
[[nodiscard]] std::optional<ExceptionType> captureException(Fn&& fn) {
    try {
        fn();
    } catch (const ExceptionType& error) {
        return error;
    }
    return std::nullopt;
}

// Creates a unique scratch directory under the OS temp path and removes it on
// destruction. Used by persistence/application tests and self-checks.
class ScratchDirectory final {
public:
    ScratchDirectory();
    ~ScratchDirectory();

    ScratchDirectory(const ScratchDirectory&) = delete;
    ScratchDirectory& operator=(const ScratchDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

[[nodiscard]] domain::geo::GeoreferenceConfig sampleGeoreference();
[[nodiscard]] domain::project::CreateProjectSpec sampleCreateSpec(const std::filesystem::path& parent);

} // namespace infraforge::testhelpers
