#include "infraforge/application/validation/ValidationService.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace infraforge::application::validation {

namespace {

[[noreturn]] void failNotOpen() {
    throw application::CommandFailure(application::CommandFailureCode::ProjectNotOpen, "no project is open");
}

} // namespace

ValidationService::ValidationService(ports::ProjectStore& store, const ValidatorRegistry& registry)
    : store_(store),
      registry_(registry) {}

ValidationResult ValidationService::check(CancellationToken& cancellation) const {
    if (!store_.isOpen()) {
        failNotOpen();
    }

    // Capture the canonical state snapshot before any validator runs. This
    // pins the revision so diagnostics are deterministic even if a mutation
    // arrives on the executor between validators (the executor is single-
    // threaded, so this is belt-and-suspenders).
    ValidationContext context;
    context.project = store_.current();
    context.revision = context.project.revision;

    ValidationResult result;
    result.revision = context.revision;

    for (const auto& validator : registry_.validators()) {
        if (cancellation.isCancelled()) {
            result.cancelled = true;
            return result;
        }
        try {
            validator->validate(context, cancellation, result.diagnostics);
        } catch (const std::exception& error) {
            // A validator throwing is an engine bug; report it as a diagnostic
            // with a stable code rather than killing the run.
            domain::validation::Diagnostic diagnostic;
            diagnostic.code = std::string{validator->name()} + ".internal_error";
            diagnostic.severity = domain::validation::Severity::Error;
            diagnostic.source = std::string{validator->name()};
            diagnostic.message = std::string{error.what()};
            diagnostic.revision = context.revision;
            result.diagnostics.push_back(std::move(diagnostic));
        } catch (...) {
            domain::validation::Diagnostic diagnostic;
            diagnostic.code = std::string{validator->name()} + ".internal_error";
            diagnostic.severity = domain::validation::Severity::Error;
            diagnostic.source = std::string{validator->name()};
            diagnostic.message = "validator failed with an unknown exception";
            diagnostic.revision = context.revision;
            result.diagnostics.push_back(std::move(diagnostic));
        }
    }

    return result;
}

} // namespace infraforge::application::validation
