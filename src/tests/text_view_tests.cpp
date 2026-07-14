#include "../text_view.h"

#include <gtest/gtest.h>

#include <array>
#include <string_view>

TEST(TextView, PreservesOrdinaryBoundedText)
{
    const std::array<char, 5> storage { 'h', 'e', 'l', 'l', 'o' };
    EXPECT_EQ(
        rots::text::truncate_at_null(std::string_view(storage.data(), storage.size())), "hello");
}

TEST(TextView, StopsAtFirstEmbeddedNull)
{
    constexpr std::string_view text("alpha\0omega", 11);
    EXPECT_EQ(rots::text::truncate_at_null(text), "alpha");
}

TEST(TextView, PreservesEmptyView)
{
    EXPECT_TRUE(rots::text::truncate_at_null({}).empty());
}
