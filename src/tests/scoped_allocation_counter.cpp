// Test-binary-global replacement of the allocation functions, feeding a
// thread-local tally that ScopedAllocationCounter snapshots. Linked into
// ageland_tests only -- never into the game binary. Aligned-allocation
// overloads are deliberately not replaced: no asserted path allocates
// over-aligned storage, and the platform defaults remain correct.

#include "scoped_allocation_counter.h"

#include <cstdlib>
#include <new>

namespace {

// Number of operator-new calls made on this thread since process start.
thread_local std::size_t allocation_count = 0;

void *counted_allocate(std::size_t size) noexcept {
    ++allocation_count;
    return std::malloc(size != 0 ? size : 1);
}

} // namespace

namespace rots_test {

std::size_t current_allocation_count() { return allocation_count; }

} // namespace rots_test

void *operator new(std::size_t size) {
    if (void *pointer = counted_allocate(size)) {
        return pointer;
    }
    throw std::bad_alloc();
}

void *operator new[](std::size_t size) {
    if (void *pointer = counted_allocate(size)) {
        return pointer;
    }
    throw std::bad_alloc();
}

void *operator new(std::size_t size, const std::nothrow_t &) noexcept {
    return counted_allocate(size);
}
void *operator new[](std::size_t size, const std::nothrow_t &) noexcept {
    return counted_allocate(size);
}

void operator delete(void *pointer) noexcept { std::free(pointer); }
void operator delete[](void *pointer) noexcept { std::free(pointer); }
void operator delete(void *pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void *pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete(void *pointer, const std::nothrow_t &) noexcept { std::free(pointer); }
void operator delete[](void *pointer, const std::nothrow_t &) noexcept { std::free(pointer); }
