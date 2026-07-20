#include "../rots_rng.h"
#include "test_platform_compat.h"

#include <gtest/gtest.h>

// Exercises rots_rng::seed_from_environment_or_time(), the ROTS_RNG_SEED-aware
// seeding helper used by the combat smoke harness (and any future
// deterministic replay tooling) to pin the mt19937 stream without touching
// production callers' unconditional time(0) default.
//
// rots_setenv()/rots_unsetenv() (test_platform_compat.h) are cross-platform
// wrappers over setenv(3)/_putenv_s -- ROTS_RNG_SEED handling itself has no
// POSIX-only dependency (std::getenv/std::strtoul are both portable ISO C),
// so unlike the suite's genuinely-POSIX-only fixtures (geteuid-based
// fault injection, AF_UNIX socketpair -- see account_management_tests.cpp /
// interpre_account_menu_tests.cpp), this test runs unconditionally on every
// platform rather than GTEST_SKIP()-ing on Windows.
namespace {

// RAII guard restoring ROTS_RNG_SEED's prior state (present, absent, or
// unset) so this test can't leak its override into any test that runs after
// it in the same process -- mirrors ScopedEnvironmentVariable
// (account_management_tests.cpp / interpre_account_menu_tests.cpp), kept
// local here since neither of those files is a shared header.
class ScopedRngSeedEnv {
public:
    ScopedRngSeedEnv()
    {
        const char* original_value = std::getenv("ROTS_RNG_SEED");
        if (original_value != nullptr) {
            m_had_original_value = true;
            m_original_value = original_value;
        }
    }

    ~ScopedRngSeedEnv()
    {
        if (m_had_original_value)
            rots_setenv("ROTS_RNG_SEED", m_original_value.c_str());
        else
            rots_unsetenv("ROTS_RNG_SEED");
    }

private:
    std::string m_original_value;
    bool m_had_original_value = false;
};

} // namespace

TEST(RngSeed, SameEnvironmentSeedProducesSameSequence)
{
    ScopedRngSeedEnv restore_env;

    ASSERT_EQ(0, rots_setenv("ROTS_RNG_SEED", "12345"));
    rots_rng::seed_from_environment_or_time();
    unsigned int first[8];
    for (unsigned int& value : first) {
        value = rots_rng::next();
    }

    ASSERT_EQ(0, rots_setenv("ROTS_RNG_SEED", "12345"));
    rots_rng::seed_from_environment_or_time();
    for (unsigned int expected : first) {
        EXPECT_EQ(expected, rots_rng::next());
    }
}

TEST(RngSeed, UnsetEnvironmentStillSeedsViaTimeFallback)
{
    ScopedRngSeedEnv restore_env;

    ASSERT_EQ(0, rots_unsetenv("ROTS_RNG_SEED"));
    // No crash, and the engine is left in a usable state -- next() must
    // produce something without throwing/aborting. This is the time(0)
    // fallback path; it has no fixed expected value to assert against.
    ASSERT_NO_THROW(rots_rng::seed_from_environment_or_time());
    ASSERT_NO_THROW(rots_rng::next());
}

TEST(RngSeed, MalformedEnvironmentValueFallsBackToTimeSeeding)
{
    ScopedRngSeedEnv restore_env;

    ASSERT_EQ(0, rots_setenv("ROTS_RNG_SEED", "not-a-number"));
    ASSERT_NO_THROW(rots_rng::seed_from_environment_or_time());
    ASSERT_NO_THROW(rots_rng::next());
}
