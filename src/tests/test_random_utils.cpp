#include "test_random_utils.h"
#include <deque>

namespace {

std::deque<double> test_random_values;

double clamp_test_random_value(double value)
{
    if (value < 0.0) {
        return 0.0;
    }

    if (value >= 1.0) {
        return 0.999999;
    }

    return value;
}

// Consumer side of the seam declared in utility.cpp: pops one queued value
// (clamped to [0.0, 1.0)) per call, or returns -1.0 when the queue is empty
// to tell number()/number(int,int) to fall through to the real PRNG. This
// reproduces the exact fallthrough the old __wrap__Z6numberv/
// __wrap__Z6numberii linker-wrap functions provided.
double test_number_hook()
{
    if (test_random_values.empty()) {
        return -1.0;
    }

    double value = test_random_values.front();
    test_random_values.pop_front();
    return clamp_test_random_value(value);
}

} // namespace

// Defined in utility.cpp (TESTING builds only). number()/number(int,int)
// check this pointer before drawing from the PRNG; installing it below is
// what makes the queue above take effect.
extern double (*rots_test_random_hook)();

namespace {

struct TestRandomHookInstaller
{
    TestRandomHookInstaller()
    {
        rots_test_random_hook = test_number_hook;
    }
};

// Dynamic-initialization order across translation units is otherwise
// unspecified, but rots_test_random_hook's own initializer (= nullptr) is a
// constant expression evaluated during static (not dynamic) initialization,
// so it is guaranteed to happen before this runs regardless of TU order.
TestRandomHookInstaller test_random_hook_installer;

} // namespace

void clear_test_random_values()
{
    test_random_values.clear();
}

void push_test_random_value(double value)
{
    test_random_values.push_back(value);
}
