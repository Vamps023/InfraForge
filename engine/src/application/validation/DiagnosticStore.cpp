#include "infraforge/application/validation/DiagnosticStore.hpp"

#include <algorithm>
#include <utility>

namespace infraforge::application::validation {

std::vector<DiagnosticStore::Change> DiagnosticStore::publish(
    const std::vector<domain::validation::Diagnostic>& next,
    std::uint64_t revision) {
    std::vector<Change> changes;

    // Build the next set keyed by identity.
    std::unordered_map<std::string, domain::validation::Diagnostic> nextMap;
    nextMap.reserve(next.size());
    std::vector<std::string> nextOrder;
    nextOrder.reserve(next.size());
    for (const auto& d : next) {
        const auto key = d.diagnosticIdentity();
        if (nextMap.find(key) == nextMap.end()) {
            nextOrder.push_back(key);
        }
        nextMap[key] = d;
    }

    // Added: in next but not in published. Iterate nextOrder for determinism.
    for (const auto& key : nextOrder) {
        if (published_.find(key) == published_.end()) {
            changes.push_back(Change{ChangeKind::Added, nextMap[key]});
        }
    }

    // Removed: in published but not in next. Iterate order_ for determinism.
    for (const auto& key : order_) {
        if (nextMap.find(key) == nextMap.end()) {
            changes.push_back(Change{ChangeKind::Removed, published_[key]});
        }
    }

    // Replace the published set.
    published_ = std::move(nextMap);
    order_ = std::move(nextOrder);
    publishedRevision_ = revision;
    hasPublished_ = true;

    return changes;
}

std::vector<domain::validation::Diagnostic> DiagnosticStore::clear() {
    std::vector<domain::validation::Diagnostic> removed;
    removed.reserve(order_.size());
    for (const auto& key : order_) {
        removed.push_back(std::move(published_[key]));
    }
    published_.clear();
    order_.clear();
    publishedRevision_ = 0;
    hasPublished_ = false;
    return removed;
}

bool DiagnosticStore::isStale(std::uint64_t currentRevision) const noexcept {
    return !hasPublished_ || publishedRevision_ != currentRevision;
}

std::vector<domain::validation::Diagnostic> DiagnosticStore::current() const {
    std::vector<domain::validation::Diagnostic> result;
    result.reserve(order_.size());
    for (const auto& key : order_) {
        result.push_back(published_.at(key));
    }
    return result;
}

} // namespace infraforge::application::validation
