#include "infraforge/application/validation/ProjectFoundationValidator.hpp"

#include "infraforge/domain/validation/Diagnostic.hpp"

#include <cmath>
#include <string>
#include <utility>

namespace infraforge::application::validation {

namespace {

using domain::validation::Diagnostic;
using domain::validation::EntityRef;
using domain::validation::Severity;
using domain::validation::SuggestedAction;

constexpr std::string_view kValidatorName = "project.foundation";

Diagnostic makeDiagnostic(std::string code, Severity severity, std::string message, std::uint64_t revision) {
    Diagnostic diagnostic;
    diagnostic.code = std::move(code);
    diagnostic.severity = severity;
    diagnostic.source = std::string{kValidatorName};
    diagnostic.message = std::move(message);
    diagnostic.revision = revision;
    return diagnostic;
}

EntityRef projectEntity(const std::string& projectUuid) {
    return EntityRef{.kind = "project", .id = projectUuid};
}

} // namespace

std::string_view ProjectFoundationValidator::name() const noexcept {
    return kValidatorName;
}

void ProjectFoundationValidator::validate(
    const ValidationContext& context,
    const CancellationToken& cancellation,
    std::vector<Diagnostic>& output) const {
    const auto& project = context.project;

    // georeference.horizontal_crs non-empty
    if (project.georeference.horizontalCrs.empty()) {
        auto diagnostic = makeDiagnostic(
            "project.foundation.georeference.horizontal_crs_empty",
            Severity::Error,
            "Project georeference has no horizontal CRS; the project cannot be spatially located.",
            context.revision);
        diagnostic.entities.push_back(projectEntity(project.uuid));
        diagnostic.suggestedAction = SuggestedAction{
            .kind = "set_georeference",
            .label = "Set the project horizontal CRS"};
        output.push_back(std::move(diagnostic));
    }

    if (cancellation.isCancelled()) {
        return;
    }

    // georeference.linear_unit non-empty
    if (project.georeference.linearUnit.empty()) {
        auto diagnostic = makeDiagnostic(
            "project.foundation.georeference.linear_unit_empty",
            Severity::Error,
            "Project georeference has no linear unit; measurements are ambiguous.",
            context.revision);
        diagnostic.entities.push_back(projectEntity(project.uuid));
        diagnostic.suggestedAction = SuggestedAction{
            .kind = "set_linear_unit",
            .label = "Set the project linear unit"};
        output.push_back(std::move(diagnostic));
    }

    if (cancellation.isCancelled()) {
        return;
    }

    // georeference.origin finite
    if (!std::isfinite(project.georeference.originEasting) || !std::isfinite(project.georeference.originNorthing)) {
        auto diagnostic = makeDiagnostic(
            "project.foundation.georeference.origin_not_finite",
            Severity::Error,
            "Project origin coordinates are not finite; spatial transforms are undefined.",
            context.revision);
        diagnostic.entities.push_back(projectEntity(project.uuid));
        diagnostic.suggestedAction = SuggestedAction{
            .kind = "set_origin",
            .label = "Set a finite project origin"};
        output.push_back(std::move(diagnostic));
    }

    if (cancellation.isCancelled()) {
        return;
    }

    // revision >= 1
    if (project.revision < 1) {
        auto diagnostic = makeDiagnostic(
            "project.foundation.revision_below_one",
            Severity::Error,
            "Project revision is below 1; the revision counter is corrupt.",
            context.revision);
        diagnostic.entities.push_back(projectEntity(project.uuid));
        output.push_back(std::move(diagnostic));
    }

    if (cancellation.isCancelled()) {
        return;
    }

    // saved_revision <= revision
    if (project.savedRevision > project.revision) {
        auto diagnostic = makeDiagnostic(
            "project.foundation.saved_revision_exceeds_revision",
            Severity::Error,
            "Saved revision exceeds the current revision; the project state is inconsistent.",
            context.revision);
        diagnostic.entities.push_back(projectEntity(project.uuid));
        output.push_back(std::move(diagnostic));
    }
}

} // namespace infraforge::application::validation
