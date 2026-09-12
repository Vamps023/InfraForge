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
// Invariants enforced by the service:
// - `code` is non-empty and stable across runs (e.g. "project.georeference.missing").
// - `source` is non-empty and identifies the validator that produced this diagnostic.
// - `message` is human-readable and may change between engine versions; it is
//   never parsed by machines and never used as identity.
// - `revision` is the project revision the diagnostic was computed against.
//
// Diagnostic identity:
// A diagnostic `code` alone is not a unique instance — multiple entities may
// produce the same code (e.g. `road.geometry.self_intersection` on two roads).
// Identity is the stable tuple (code, entities). The `message` is never part
// of identity. Use `diagnosticIdentity()` for a deterministic string key.
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

    // Returns a deterministic identity string derived from (code, entities).
    // Two diagnostics with the same code but different entity refs produce
    // different identity strings. The message is never included.
    // Format: "<code>|<kind:id>,<kind:id>,..." with entities sorted for stability.
    [[nodiscard]] std::string diagnosticIdentity() const;
};

} // namespace infraforge::domain::validation
