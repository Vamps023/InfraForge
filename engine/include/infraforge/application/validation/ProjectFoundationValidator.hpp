#pragma once

#include "infraforge/application/validation/Validator.hpp"

namespace infraforge::application::validation {

// Validates the project foundation: canonical state that exists on current
// main (project record, georeference, revision counters). This validator
// intentionally checks only state that is already persisted and available;
// domain-specific validators (roads, terrain, etc.) are added when their
// owning domains merge.
//
// Checks performed against the canonical ProjectRecord:
// - georeference.horizontal_crs is non-empty (persisted invariant, but
//   defends against future migration gaps or direct DB edits).
// - georeference.linear_unit is non-empty.
// - georeference.origin is finite (not NaN/Inf).
// - revision >= 1 (a project must have at least one revision).
// - saved_revision <= revision (saved must not exceed current).
//
// Each check produces a diagnostic with a stable code prefixed by
// "project.foundation." so the frontend can group and react to them.
class ProjectFoundationValidator final : public Validator {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    void validate(const ValidationContext& context, const CancellationToken& cancellation, std::vector<domain::validation::Diagnostic>& output) const override;
};

} // namespace infraforge::application::validation
