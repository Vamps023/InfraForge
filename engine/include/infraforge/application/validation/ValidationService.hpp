#pragma once

#include "infraforge/application/ProjectService.hpp"
#include "infraforge/application/validation/CancellationToken.hpp"
#include "infraforge/application/validation/ValidationContext.hpp"
#include "infraforge/application/validation/ValidatorRegistry.hpp"
#include "infraforge/domain/project/ProjectModel.hpp"
#include "infraforge/domain/validation/Diagnostic.hpp"
#include "infraforge/ports/ProjectStore.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace infraforge::application::validation {

// Result of a validation run. Diagnostics are ordered by validator execution
// order and, within a validator, in the order the validator produced them.
// `revision` is the project revision the snapshot was captured against;
// every diagnostic also carries this revision.
struct ValidationResult {
    std::vector<domain::validation::Diagnostic> diagnostics;
    std::uint64_t revision{0};
    // True when the run was cancelled before all validators completed. When
    // cancelled, `diagnostics` is EMPTY — partial results are never published
    // as authoritative. The caller must not update the diagnostic store.
    bool cancelled{false};
};

// Application use case for `world.check`. Captures a revision snapshot of the
// canonical project state before running any validator, so diagnostics are
// deterministic against a fixed revision even if a mutation arrives mid-run.
//
// Runs on the single application executor thread. Cancellation is cooperative:
// the caller passes a shared CancellationToken that may be signalled from the
// network thread by `world.cancel_check`. Validators poll it at safe points.
//
// Failure behavior:
// - No open project → throws CommandFailure(ProjectNotOpen).
// - Validator internal error → reported as a diagnostic, not an exception.
// - Cancellation → returns a result with `cancelled=true` and empty diagnostics.
//   Partial results are discarded, never published.
class ValidationService final {
public:
    explicit ValidationService(ports::ProjectStore& store, const ValidatorRegistry& registry);

    // Runs all registered validators against a snapshot of the current
    // canonical project state. The snapshot is captured before any validator
    // runs, so the revision in every diagnostic is the revision at the start
    // of the run.
    //
    // `cancellation` is a shared token so the network thread can request
    // cancellation of an in-progress run. When cancelled, the service stops
    // invoking further validators and returns `cancelled=true` with empty
    // diagnostics.
    [[nodiscard]] ValidationResult check(const std::shared_ptr<CancellationToken>& cancellation) const;

private:
    ports::ProjectStore& store_;
    const ValidatorRegistry& registry_;
};

} // namespace infraforge::application::validation
