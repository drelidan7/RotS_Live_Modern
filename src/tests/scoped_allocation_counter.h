#pragma once

#include <cstddef>

namespace rots_test {

// Reads the thread-local tally maintained by the replaced global allocation
// functions in scoped_allocation_counter.cpp (test binary only).
std::size_t current_allocation_count();

// Snapshots the allocation tally at construction so allocations() reports how
// many operator-new calls this thread has made since the counter was created.
class ScopedAllocationCounter {
  public:
    ScopedAllocationCounter() : m_start(current_allocation_count()) {}

    std::size_t allocations() const { return current_allocation_count() - m_start; }

  private:
    // Tally captured at construction; baseline subtracted by allocations().
    std::size_t m_start;
};

} // namespace rots_test
