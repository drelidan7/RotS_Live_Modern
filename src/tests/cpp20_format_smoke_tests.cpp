#include <format>
#include <gtest/gtest.h>

// Permanent toolchain smoke test (Phase 4 Wave 1 Task 1: C++20 + trixie/g++14
// bump). <format>/std::format only exist once the whole build is compiled as
// C++20 -- under C++17 this file fails to compile (see git history / task
// brief for the RED evidence captured before the standard was bumped). Keep
// this test forever: it is the cheapest possible tripwire against any build
// path (Makefile, tests/Makefile, CMakeLists.txt targets, or a future CI
// runner image) silently regressing back to an older standard.

TEST(Cpp20FormatSmoke, FormatsRightAlignedWidth) {
    EXPECT_EQ(std::format("{:>3}", 7), "  7");
}
