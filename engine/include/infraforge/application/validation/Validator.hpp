#pragma once

#include "infraforge/application/validation/CancellationToken.hpp"
#include "infraforge/application/validation/ValidationContext.hpp"
#include "infraforge/domain/validation/Diagnostic.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace infraforge::application::validation {

// A validator inspects a snapshot of canonical project state and produces
// zero or more diagnostics. Validators are pure functions of the context:
// the same context always yields the same diagnostics in the same order.
//
// Contract:
// - `name()` returns a stable, non-empty identifier (e.g. "project.foundation").
// - `validate()` must be deterministic and must not mutate the context.
// - Validators must not throw; an internal failure is reported as a diagnostic
//   with severity Error and a stable code prefixed by the validator name.
// - Validators must not access renderer, frontend, or non-canonical state.
// - Validators must poll `cancellation` at safe points and return early when
//   cancelled; partial results are discarded by the service.
class Validator {
public:
    virtual ~Validator() = default;

    Validator(const Validator&) = delete;
    Validator& operator=(const Validator&) = delete;

    // Stable identifier for this validator. Used as the `source` field of
    // produced diagnostics and for deterministic ordering in the registry.
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    // Inspect the context and append diagnostics to `output`. Must be
    // deterministic for a given context.
    virtual void validate(const ValidationContext& context, const CancellationToken& cancellation, std::vector<domain::validation::Diagnostic>& output) const = 0;

protected:
    Validator() = default;
};

} // namespace infraforge::application::validation
