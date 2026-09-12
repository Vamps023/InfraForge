#include "infraforge/domain/validation/Diagnostic.hpp"

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

} // namespace infraforge::domain::validation
