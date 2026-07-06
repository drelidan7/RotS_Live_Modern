#include "../rots_rng.h"
#include <gtest/gtest.h>

TEST(RotsRng, SameSeedProducesSameSequence)
{
    rots_rng::seed(12345u);
    unsigned int first[8];
    for (unsigned int& value : first) {
        value = rots_rng::next();
    }

    rots_rng::seed(12345u);
    for (unsigned int expected : first) {
        EXPECT_EQ(expected, rots_rng::next());
    }
}

TEST(RotsRng, DifferentSeedsDiverge)
{
    rots_rng::seed(1u);
    unsigned int a = rots_rng::next();
    rots_rng::seed(2u);
    unsigned int b = rots_rng::next();
    EXPECT_NE(a, b);
}

// Pins the engine choice itself: std::mt19937's output for a given seed is
// defined by the C++ standard, so this value must be identical on every
// platform/compiler. If this test ever fails, the engine changed — which
// invalidates every characterization golden.
TEST(RotsRng, EngineIsStandardMt19937)
{
    rots_rng::seed(5489u); // mt19937 default seed
    // First output of std::mt19937 with its default seed, per the standard.
    EXPECT_EQ(3499211612u, rots_rng::next());
}
