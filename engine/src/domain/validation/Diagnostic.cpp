#include "infraforge/domain/validation/Diagnostic.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace infraforge::domain::validation {

std::string_view severityName(Severity severity) noexcept {
    switch (severity) {
    case Severity::Info:
        return "info";
    case Severity::Warning:
        return "warning";
    case Severity::Error:
        return "error";
    }
    return "";
}

std::optional<Severity> severityFromName(std::string_view name) noexcept {
    if (name == "info") {
        return Severity::Info;
    }
    if (name == "warning") {
        return Severity::Warning;
    }
    if (name == "error") {
        return Severity::Error;
    }
    return std::nullopt;
}

std::string Diagnostic::diagnosticIdentity() const {
    // Deterministic identity: code + sorted entity refs. The message is
    // never part of identity. Entities are sorted by (kind, id) so the
    // identity is stable regardless of the order a validator emits them.
    std::string identity = code;

    if (!entities.empty()) {
        std::vector<EntityRef> sorted = entities;
        std::sort(sorted.begin(), sorted.end(), [](const EntityRef& a, const EntityRef& b) {
            if (a.kind != b.kind) {
                return a.kind < b.kind;
            }
            return a.id < b.id;
        });

        identity += "|";
        for (std::size_t i = 0; i < sorted.size(); ++i) {
            if (i > 0) {
                identity += ",";
            }
            identity += sorted[i].kind;
            identity += ":";
            identity += sorted[i].id;
        }
    }

    return identity;
}

} // namespace infraforge::domain::validation
