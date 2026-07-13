#include "../handler.h"
#include "../utils.h"

#include <gtest/gtest.h>

#include <array>
#include <concepts>
#include <string_view>
#include <type_traits>

static_assert(std::same_as<std::remove_cvref_t<decltype(weekdays[0])>, std::string_view>);
static_assert(std::same_as<std::remove_cvref_t<decltype(month_name[0])>, std::string_view>);
static_assert(std::same_as<std::remove_cvref_t<decltype(moon_phase[0])>, std::string_view>);

extern const char* const dirs[];

TEST(StringViewUtility, ComparesBoundedTextWithoutNullTermination)
{
    const std::array<char, 5> lower_storage { 'a', 'l', 'p', 'h', 'a' };
    const std::array<char, 5> upper_storage { 'A', 'L', 'P', 'H', 'A' };
    const std::string_view lower_text(lower_storage.data(), lower_storage.size());
    const std::string_view upper_text(upper_storage.data(), upper_storage.size());

    EXPECT_EQ(str_cmp(lower_text, upper_text), 0);
    EXPECT_EQ(strn_cmp(lower_text, upper_text, 5), 0);
}

TEST(StringViewUtility, ComparisonStopsAtFirstEmbeddedNull)
{
    constexpr std::string_view first("name\0ignored", 12);

    EXPECT_EQ(str_cmp(first, "NAME"), 0);
    EXPECT_EQ(strn_cmp(first, "NAME", 12), 0);
}

TEST(StringViewUtility, ComparisonHandlesEmptyViewsAndOrdering)
{
    EXPECT_EQ(str_cmp({}, {}), 0);
    EXPECT_LT(str_cmp({}, "value"), 0);
    EXPECT_GT(str_cmp("value", {}), 0);
    EXPECT_EQ(strn_cmp("different", "values", 0), 0);
}

TEST(StringViewUtility, IsNameAcceptsBoundedAndEmbeddedNullViews)
{
    const std::array<char, 5> query_storage { 's', 'w', 'o', 'r', 'd' };
    const std::string_view query(query_storage.data(), query_storage.size());
    constexpr std::string_view embedded_query("sword\0ignored", 13);
    constexpr std::string_view embedded_names("sword shield\0ignored", 20);

    EXPECT_EQ(isname(query, "sword shield"), 1);
    EXPECT_EQ(isname(embedded_query, embedded_names), 1);
    EXPECT_EQ(isname({}, "sword shield"), 0);
}

TEST(StringViewUtility, RetainsLegacySentinelTables)
{
    EXPECT_STREQ(dirs[6], "\n");
    EXPECT_STREQ(pc_races[21], "\n");
}
