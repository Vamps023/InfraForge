#pragma once

#include <infraforge/viewport/renderer/Vulkan.hpp>

#include <cstdint>
#include <limits>

namespace infraforge::viewport {

// Computes a spec-valid VkMappedMemoryRange flush range for non-coherent
// memory. Vulkan requires that for non-coherent memory, flush ranges have:
//   - offset aligned to nonCoherentAtomSize
//   - size aligned to nonCoherentAtomSize, OR offset+size == allocationSize
//
// This helper aligns offset down and size up to the atom boundary, clamping
// to allocationSize to avoid overflow. When the payload covers the entire
// allocation, it returns VK_WHOLE_SIZE (always spec-valid).
//
// Deterministic and GPU-free so it can be unit-tested.
struct AlignedFlushRange {
    VkDeviceSize offset;
    VkDeviceSize size; // may be VK_WHOLE_SIZE
};

[[nodiscard]] inline AlignedFlushRange computeFlushRange(
    VkDeviceSize payloadOffset, VkDeviceSize payloadSize,
    VkDeviceSize allocationSize, VkDeviceSize atomSize) noexcept {
    // VK_WHOLE_SIZE is always valid when the mapping covers the full
    // allocation. Use it when the payload starts at 0.
    if (payloadOffset == 0) {
        return {0, VK_WHOLE_SIZE};
    }
    // Align offset down to atom boundary.
    const VkDeviceSize alignedOffset = (payloadOffset / atomSize) * atomSize;
    // Align end up to atom boundary, clamped to allocationSize.
    const VkDeviceSize payloadEnd = payloadOffset + payloadSize;
    VkDeviceSize alignedEnd = ((payloadEnd + atomSize - 1) / atomSize) * atomSize;
    if (alignedEnd > allocationSize || alignedEnd < payloadEnd) {
        // Overflow or past allocation: clamp to allocation end.
        alignedEnd = allocationSize;
    }
    return {alignedOffset, alignedEnd - alignedOffset};
}

} // namespace infraforge::viewport
