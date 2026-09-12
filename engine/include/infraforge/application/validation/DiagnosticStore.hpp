#pragma once

#include "infraforge/domain/validation/Diagnostic.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace infraforge::application::validation {

// Engine-owned store of the currently published diagnostic set. The
// CommandProcessor owns one instance and uses it to:
// - diff a new validation result against the previous published set;
// - compute deterministic added/removed events;
// - invalidate (clear) diagnostics when the project closes or the
//   canonical revision changes.
//
// The frontend never owns this state; it receives added/removed/cleared
// events and projects them. This is the canonical diagnostic lifecycle owner.
//
// Identity: a diagnostic is identified by (code, entities), not by code
// alone. The store keys diagnostics by `diagnosticIdentity()` so multiple
// entities producing the same code are distinct entries.
class DiagnosticStore {
public:
    // Represents a single change between two diagnostic sets.
    enum class ChangeKind { Added, Removed };
    struct Change {
        ChangeKind kind;
        domain::validation::Diagnostic diagnostic; // for Added
        // For Removed, only code and entities are meaningful; we still carry
        // the full diagnostic for simplicity since the store had it.
    };

    // Replaces the published set with `next` (computed against `revision`)
    // and returns the deterministic diff (added first, then removed, each
    // in stable order). Returns an empty vector if `next` is identical to
    // the current set.
    //
    // If the store was stale or empty and `next` is non-empty, all of
    // `next` are "added". If `next` is empty and the store had diagnostics,
    // all are "removed".
    std::vector<Change> publish(const std::vector<domain::validation::Diagnostic>& next, std::uint64_t revision);

    // Clears the published set. Returns the removed diagnostics so the
    // caller can broadcast removals. `reason` is stored for diagnostics.
    std::vector<domain::validation::Diagnostic> clear();

    // Returns true if the published diagnostics were computed against a
    // revision different from `currentRevision`, or if no diagnostics have
    // been published yet.
    [[nodiscard]] bool isStale(std::uint64_t currentRevision) const noexcept;

    // Returns the revision the current diagnostics were computed against,
    // or 0 if none have been published.
    [[nodiscard]] std::uint64_t publishedRevision() const noexcept { return publishedRevision_; }

    // Returns true if diagnostics have been published (non-empty set or
    // an explicit empty-set publication).
    [[nodiscard]] bool hasPublished() const noexcept { return hasPublished_; }

    // Returns the current published diagnostics in deterministic order.
    [[nodiscard]] std::vector<domain::validation::Diagnostic> current() const;

private:
    // Keyed by diagnosticIdentity() for O(1) lookup.
    std::unordered_map<std::string, domain::validation::Diagnostic> published_;
    // Preserves insertion order for deterministic output.
    std::vector<std::string> order_;
    std::uint64_t publishedRevision_{0};
    bool hasPublished_{false};
};

} // namespace infraforge::application::validation
