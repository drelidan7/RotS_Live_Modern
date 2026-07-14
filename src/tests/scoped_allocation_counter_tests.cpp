#include "scoped_allocation_counter.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

TEST(ScopedAllocationCounter, CountsHeapAllocationsSinceConstruction) {
    rots_test::ScopedAllocationCounter counter;
    EXPECT_EQ(counter.allocations(), 0u);

    // A 64-byte string defeats SSO on every supported standard library.
    const std::string forced_allocation(64, 'x');
    EXPECT_GE(counter.allocations(), 1u);
}

TEST(ScopedAllocationCounter, SeparateCountersSnapshotIndependently) {
    rots_test::ScopedAllocationCounter outer;
    const auto first_heap_object = std::make_unique<int>(7);
    rots_test::ScopedAllocationCounter inner;
    EXPECT_EQ(inner.allocations(), 0u);
    EXPECT_GE(outer.allocations(), 1u);
    EXPECT_EQ(*first_heap_object, 7);
}
