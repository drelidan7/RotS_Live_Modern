#include "../platform_compat.h"
#include <cstdlib>
#include <cstring>
#include <gtest/gtest.h>

// rots_asprintf is a portable drop-in for the POSIX/BSD/glibc asprintf() extension,
// which MSVC's CRT does not provide (Phase 3 Task 5: MSVC bring-up). These pin its
// asprintf(3)-compatible ownership contract (malloc'd *out on success, caller frees;
// nullptr + -1 on failure) so the mechanical asprintf-call-site replacement across the
// game sources (act_info.cpp, fight.cpp, spec_pro.cpp, pkill.cpp, utility.cpp) is
// behavior-preserving.

TEST(RotsAsprintf, FormatsSimpleString) {
    char* out = nullptr;
    int written = rots_asprintf(&out, "%s", "hello");

    ASSERT_NE(out, nullptr);
    EXPECT_EQ(written, 5);
    EXPECT_STREQ(out, "hello");
    std::free(out);
}

TEST(RotsAsprintf, FormatsMixedArguments) {
    char* out = nullptr;
    int written = rots_asprintf(&out, "%d%s", 21, "st");

    ASSERT_NE(out, nullptr);
    EXPECT_EQ(written, 4);
    EXPECT_STREQ(out, "21st");
    std::free(out);
}

TEST(RotsAsprintf, ProducesEmptyStringForEmptyFormat) {
    char* out = nullptr;
    int written = rots_asprintf(&out, "%s", "");

    ASSERT_NE(out, nullptr);
    EXPECT_EQ(written, 0);
    EXPECT_STREQ(out, "");
    std::free(out);
}

TEST(RotsAsprintf, HandlesLongOutputPastAnySmallStackBuffer) {
    // Exercises the two-pass (size-then-allocate) path with output well past any
    // plausible fixed-size scratch buffer, guarding against an off-by-one in the
    // vsnprintf-size-then-malloc sizing.
    std::string expected(2048, 'x');
    char* out = nullptr;
    int written = rots_asprintf(&out, "%s", expected.c_str());

    ASSERT_NE(out, nullptr);
    EXPECT_EQ(written, static_cast<int>(expected.size()));
    EXPECT_EQ(std::strlen(out), expected.size());
    EXPECT_EQ(expected, out);
    std::free(out);
}

TEST(RotsAsprintf, NullTerminatesExactlyAtWrittenLength) {
    char* out = nullptr;
    int written = rots_asprintf(&out, "%s-%s", "abc", "de");

    ASSERT_NE(out, nullptr);
    EXPECT_EQ(written, 6);
    EXPECT_EQ(out[written], '\0');
    std::free(out);
}
