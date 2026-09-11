#include "TestHelpers.hpp"

#include "infraforge/runtime/Uuid.hpp"

#include <stdexcept>

namespace infraforge::testhelpers {

ScratchDirectory::ScratchDirectory() {
    std::error_code ioError;
    const auto tempRoot = std::filesystem::temp_directory_path(ioError);
    if (ioError) {
        throw std::runtime_error("cannot resolve temporary directory for tests");
    }
    path_ = tempRoot / ("infraforge-test-" + runtime::generateUuidV4());
    std::filesystem::create_directories(path_, ioError);
    if (ioError) {
        throw std::runtime_error("cannot create scratch directory for tests");
    }
}

ScratchDirectory::~ScratchDirectory() {
    std::error_code ioError;
    (void)std::filesystem::remove_all(path_, ioError);
}

domain::project::GeoreferenceConfig sampleGeoreference() {
    domain::project::GeoreferenceConfig georeference;
    georeference.horizontalCrs = "EPSG:32633";
    georeference.linearUnit = "metre";
    georeference.axisConvention = domain::project::AxisConvention::EastingNorthingUp;
    georeference.originEasting = 500000.0;
    georeference.originNorthing = 4649776.0;
    georeference.verticalCrs = "";
    return georeference;
}

domain::project::CreateProjectSpec sampleCreateSpec(const std::filesystem::path& parent) {
    domain::project::CreateProjectSpec spec;
    spec.displayName = "Test Project";
    spec.parentDirectory = parent;
    spec.trafficSide = domain::project::TrafficSide::Right;
    spec.georeference = sampleGeoreference();
    return spec;
}

} // namespace infraforge::testhelpers
