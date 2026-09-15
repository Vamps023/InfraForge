#include "infraforge/domain/world/Invalidation.hpp"

#include <stdexcept>

namespace infraforge::domain::world {

std::string_view invalidationClassName(const InvalidationClass invalidationClass) noexcept {
    switch (invalidationClass) {
    case InvalidationClass::Geometry:
        return "geometry";
    case InvalidationClass::Material:
        return "material";
    case InvalidationClass::Topology:
        return "topology";
    case InvalidationClass::Terrain:
        return "terrain";
    case InvalidationClass::Simulation:
        return "simulation";
    case InvalidationClass::Road:
        return "road";
    }
    return "";
}

std::optional<InvalidationClass> invalidationClassFromName(const std::string_view name) noexcept {
    for (auto index = std::size_t{0}; index < invalidationClassCount(); ++index) {
        const auto invalidationClass = static_cast<InvalidationClass>(index);
        if (invalidationClassName(invalidationClass) == name) {
            return invalidationClass;
        }
    }
    return std::nullopt;
}

} // namespace infraforge::domain::world
