#include <doctest/doctest.h>

#include "infraforge/viewport/renderer/SelectionId.hpp"

namespace {

using infraforge::viewport::DomainId;
using infraforge::viewport::SelectionIdRegistry;
using infraforge::viewport::kInvalidSelectionId;

constexpr DomainId domainId(std::uint64_t high, std::uint64_t low) {
    return {high, low};
}

} // namespace

TEST_SUITE("selection id registry") {
    TEST_CASE("allocated ids resolve back to the domain identity") {
        SelectionIdRegistry registry;
        const DomainId road{0x123456789ABCDEF0ULL, 0x0FEDCBA987654321ULL};
        const auto selectionId = registry.allocate(road);

        CHECK_NE(selectionId, kInvalidSelectionId);
        const auto resolved = registry.resolve(selectionId);
        REQUIRE(resolved.has_value());
        CHECK(*resolved == road);
        CHECK_EQ(registry.liveCount(), 1);
    }

    TEST_CASE("released ids stop resolving and slots are recycled with fresh generations") {
        SelectionIdRegistry registry;
        const auto first = registry.allocate(domainId(1, 1));
        REQUIRE(registry.resolve(first).has_value());

        registry.release(first);
        CHECK_FALSE(registry.resolve(first).has_value());
        CHECK_EQ(registry.liveCount(), 0);

        const auto second = registry.allocate(domainId(2, 2));
        CHECK_NE(second, kInvalidSelectionId);
        // Recycling may return a different id value (generation bump); the
        // old id must stay invalid either way.
        CHECK_FALSE(registry.resolve(first).has_value());
        const auto resolved = registry.resolve(second);
        REQUIRE(resolved.has_value());
        CHECK(*resolved == domainId(2, 2));
    }

    TEST_CASE("unknown, zero, and stale ids do not resolve") {
        SelectionIdRegistry registry;
        CHECK_FALSE(registry.resolve(kInvalidSelectionId).has_value());
        CHECK_FALSE(registry.resolve(0xDEADBEEFU).has_value());

        const auto id = registry.allocate(domainId(3, 3));
        registry.release(id);
        registry.release(id); // double release is a no-op
        CHECK_EQ(registry.liveCount(), 0);
        CHECK_FALSE(registry.resolve(id).has_value());
    }

    TEST_CASE("the invalid id is never allocated") {
        SelectionIdRegistry registry;
        for (int i = 0; i < 1000; ++i) {
            const auto id = registry.allocate(domainId(static_cast<std::uint64_t>(i), 0));
            CHECK_NE(id, kInvalidSelectionId);
        }
        CHECK_EQ(registry.liveCount(), 1000);
    }
}
