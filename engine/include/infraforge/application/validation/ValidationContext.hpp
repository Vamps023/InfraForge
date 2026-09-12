#pragma once

#include "infraforge/domain/project/ProjectModel.hpp"
#include "infraforge/domain/validation/Diagnostic.hpp"

#include <cstdint>
#include <filesystem>

namespace infraforge::application::validation {

// Immutable snapshot of the canonical project state captured at the start of
// a validation run. Validators consume this snapshot, never the live store, so
// diagnostics are deterministic against a fixed revision even if a mutation
// arrives mid-run.
//
// `revision` is the project revision the snapshot was taken against; every
// diagnostic produced from this context carries it.
struct ValidationContext {
    domain::project::ProjectRecord project;
    std::uint64_t revision{0};
};

} // namespace infraforge::application::validation
