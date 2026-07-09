#include "../clock.h"
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

// rots_clock is now built on std::chrono::steady_clock instead of gettimeofday/timeval.
// These characterize its observable behavior (get_elapsed_seconds() returns whole
// seconds elapsed, as a float, since the last call) both before and after that
// conversion -- the public contract does not change, only the timing primitive
// underneath it.

TEST(RotsClock, ElapsedSecondsIsNearZeroImmediatelyAfterConstruction) {
    rots_clock clock;
    float elapsed = clock.get_elapsed_seconds();

    EXPECT_GE(elapsed, 0.0f);
    EXPECT_LT(elapsed, 0.05f);
}

TEST(RotsClock, ElapsedSecondsMeasuresIntervalBetweenCalls) {
    rots_clock clock;
    clock.get_elapsed_seconds(); // establish a reference point

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    float elapsed = clock.get_elapsed_seconds();

    // Generous slack for scheduler jitter on a shared/CI box: must be comfortably
    // above zero and in the right ballpark for a 50ms sleep, not exact to the ms.
    EXPECT_GE(elapsed, 0.04f);
    EXPECT_LT(elapsed, 0.5f);
}

TEST(RotsClock, ConsecutiveCallsAreNeverNegative) {
    rots_clock clock;

    for (int i = 0; i < 5; ++i) {
        float elapsed = clock.get_elapsed_seconds();
        EXPECT_GE(elapsed, 0.0f) << "iteration " << i;
    }
}
