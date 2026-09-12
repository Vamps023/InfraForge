#include "infraforge/viewport/renderer/SelectionId.hpp"

#include <cstdint>

#include <algorithm>

namespace infraforge::viewport {

std::uint32_t SelectionIdRegistry::allocate(const DomainId& domainId) {
    if (!freeList_.empty()) {
        const std::uint32_t index = freeList_.back();
        freeList_.pop_back();
        Slot& slot = slots_[index];
        slot.generation = static_cast<std::uint32_t>((slot.generation + 1) & 0xFFU);
        if (slot.generation == 0) {
            slot.generation = 1;
        }
        slot.domainId = domainId;
        slot.live = true;
        ++liveCount_;
        return (index << 8) | slot.generation;
    }

    constexpr std::size_t kMaxSlots = 0x00FFFFFF; // index fits the high 24 bits
    if (slots_.size() >= kMaxSlots) {
        return kInvalidSelectionId;
    }
    Slot slot;
    slot.domainId = domainId;
    slot.generation = 1;
    slot.live = true;
    slots_.push_back(slot);
    ++liveCount_;
    const auto index = static_cast<std::uint32_t>(slots_.size() - 1);
    return (index << 8) | slot.generation;
}

std::optional<DomainId> SelectionIdRegistry::resolve(const std::uint32_t selectionId) const {
    if (selectionId == kInvalidSelectionId) {
        return std::nullopt;
    }
    const auto index = selectionId >> 8;
    const auto generation = selectionId & 0xFFU;
    if (index >= slots_.size()) {
        return std::nullopt;
    }
    const Slot& slot = slots_[index];
    if (!slot.live || slot.generation != generation) {
        return std::nullopt;
    }
    return slot.domainId;
}

void SelectionIdRegistry::release(const std::uint32_t selectionId) {
    if (selectionId == kInvalidSelectionId) {
        return;
    }
    const auto index = selectionId >> 8;
    const auto generation = selectionId & 0xFFU;
    if (index >= slots_.size()) {
        return;
    }
    Slot& slot = slots_[index];
    if (!slot.live || slot.generation != generation) {
        return;
    }
    slot.live = false;
    slot.domainId = DomainId{};
    --liveCount_;
    // Generations make stale pick results detectable; the 255 cap simply
    // retires a slot whose generation space is exhausted.
    if (slot.generation < 0xFFU) {
        freeList_.push_back(index);
    }
}

std::size_t SelectionIdRegistry::liveCount() const noexcept {
    return liveCount_;
}

} // namespace infraforge::viewport
