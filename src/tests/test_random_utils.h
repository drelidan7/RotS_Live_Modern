#pragma once

// Test-only RNG control for the ageland_tests deterministic-RNG seam.
// Proc-heavy tests can queue normalized values in [0.0, 1.0) and number()/
// number(int,int) (utility.cpp) will consume them instead of real
// randomness, via the rots_test_random_hook function-pointer hook this
// translation unit installs.
void clear_test_random_values();
void push_test_random_value(double value);
