#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace infraforge::viewport {

// Renderer-side picking identity contract:
//
//   * A SelectionId is a dense 32-bit renderer slot (0 is reserved invalid).
//   * Domain identity is the canonical 128-bit stable entity id pair used
//     across persistence/protocol (TRD section 5).
//   * The GPU pick pass writes SelectionIds into a pick buffer; resolve()
//     maps them back to domain identity. Render data never becomes canonical
//     entity identity (docs/04_RENDERER/VULKAN_ARCHITECTURE.md "Picking").
//
// The registry exists from this issue onward so pass code can allocate ids
// before domain picking arrives; nothing in the grid pass registers yet.
using DomainId = std::array<std::uint64_t, 2>;

inline constexpr std::uint32_t kInvalidSelectionId = 0;

class SelectionIdRegistry {
public:
    // Allocates a fresh selection id bound to the domain identity. Returns
    // kInvalidSelectionId when the id space is exhausted (explicit, never
    // silently reuses).
    [[nodiscard]] std::uint32_t allocate(const DomainId& domainId);

    // Maps a selection id back to its domain identity.
    [[nodiscard]] std::optional<DomainId> resolve(std::uint32_t selectionId) const;

    // Releases a slot; its id is recycled only after the internal cursor
    // wraps, mirroring handle-allocator practice.
    void release(std::uint32_t selectionId);

    [[nodiscard]] std::size_t liveCount() const noexcept;

private:
    struct Slot {
        DomainId domainId{};
        std::uint32_t generation{1};
        bool live{false};
    };

    std::vector<Slot> slots_;
    std::vector<std::uint32_t> freeList_;
    std::size_t liveCount_{0};
};

} // namespace infraforge::viewport
