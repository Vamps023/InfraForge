#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace infraforge::domain::validation {

// Diagnostic severity ordered from least to most severe. The ordering is
// stable and part of the public contract; consumers may compare values.
enum class Severity : std::uint8_t {
    Info,
    Warning,
    Error,
};

[[nodiscard]] std::string_view severityName(Severity severity) noexcept;
[[nodiscard]] std::optional<Severity> severityFromName(std::string_view name) noexcept;

// A stable reference to a canonical entity that a diagnostic concerns. The
// kind identifies the entity domain (e.g. "project", "road", "terrain") and
// the id is the canonical 128-bit UUID-compatible identifier. Both are stable
// across validation runs against the same project revision.
struct EntityRef {
    std::string kind;
    std::string id;

    friend bool operator==(const EntityRef&, const EntityRef&) = default;
};

// Optional metadata describing a remediation action the user may take. The
// action label is human-readable; the kind is a stable machine-readable tag
// (e.g. "open_settings", "fix_geometry") that future tooling can dispatch on.
struct SuggestedAction {
    std::string kind;
    std::string label;

    friend bool operator==(const SuggestedAction&, const SuggestedAction&) = default;
};

// The canonical diagnostic produced by validators. Every field is stable and
// deterministic for a given project revision: the same canonical state always
// yields the same diagnostics in the same order.
//
// Invariants enforced by construction:
// - `code` is non-empty and stable across runs (e.g. "project.georeference.missing").
// - `source` identifies the validator that produced this diagnostic.
// - `message` is human-readable and may change between engine versions; it is
//   never parsed by machines.
// - `revision` is the project revision the diagnostic was computed against.
struct Diagnostic {
    std::string code;
    Severity severity{Severity::Warning};
    // Domain/validator that produced this diagnostic (e.g. "project.foundation").
    std::string source;
    std::string message;
    std::vector<EntityRef> entities;
    // Project revision the diagnostic was computed against.
    std::uint64_t revision{0};
    std::optional<SuggestedAction> suggestedAction;

    friend bool operator==(const Diagnostic&, const Diagnostic&) = default;
};

} // namespace infraforge::domain::validation
