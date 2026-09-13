#include <doctest/doctest.h>

#include "infraforge/viewport/renderer/VulkanFlushRange.hpp"

#include <cstdint>

TEST_SUITE("vulkan flush range alignment") {

using infraforge::viewport::computeFlushRange;
using infraforge::viewport::AlignedFlushRange;

TEST_CASE("offset zero returns VK_WHOLE_SIZE") {
    const auto result = computeFlushRange(0, 1024, 4096, 128);
    CHECK(result.offset == 0);
    CHECK(result.size == VK_WHOLE_SIZE);
}

TEST_CASE("nonzero offset aligns down and size aligns up") {
    // offset=130, atom=128 -> alignedOffset=128
    // payloadEnd=130+512=642, alignedEnd=ceil(642/128)*128=6*128=768
    const auto result = computeFlushRange(130, 512, 4096, 128);
    CHECK(result.offset == 128);
    CHECK(result.size == 768 - 128);
}

TEST_CASE("already aligned offset and size") {
    const auto result = computeFlushRange(128, 256, 4096, 128);
    CHECK(result.offset == 128);
    CHECK(result.size == 256);
}

TEST_CASE("unaligned end clamps to allocation size") {
    // offset=128, payloadSize=4000, allocationSize=4096, atom=128
    // payloadEnd=128+4000=4128, alignedEnd=ceil(4128/128)*128=32*128=4096
    // 4096 <= 4096, so alignedEnd=4096
    const auto result = computeFlushRange(128, 4000, 4096, 128);
    CHECK(result.offset == 128);
    CHECK(result.size == 4096 - 128);
}

TEST_CASE("atom size greater than payload size") {
    // atom=256, offset=0 -> VK_WHOLE_SIZE
    const auto result = computeFlushRange(0, 10, 4096, 256);
    CHECK(result.offset == 0);
    CHECK(result.size == VK_WHOLE_SIZE);
}

TEST_CASE("overflow in alignment math does not crash") {
    // Very large values that could overflow if not handled carefully.
    const VkDeviceSize maxSize = UINT64_MAX;
    const auto result = computeFlushRange(256, 256, maxSize, 128);
    CHECK(result.offset == 256);
    CHECK(result.size == 256);
}

TEST_CASE("payload end exactly at allocation end") {
    // offset=0 -> VK_WHOLE_SIZE
    const auto result = computeFlushRange(0, 4096, 4096, 128);
    CHECK(result.offset == 0);
    CHECK(result.size == VK_WHOLE_SIZE);
}

} // TEST_SUITE
