#include "../handler.h"
#include "../interpre.h"
#include "../utils.h"

#include <gtest/gtest.h>

#include <array>
#include <concepts>
#include <string_view>
#include <type_traits>

static_assert(std::same_as<std::remove_cvref_t<decltype(weekdays[0])>, std::string_view>);
static_assert(std::same_as<std::remove_cvref_t<decltype(month_name[0])>, std::string_view>);
static_assert(std::same_as<std::remove_cvref_t<decltype(moon_phase[0])>, std::string_view>);

extern const std::string_view dirs[];
extern const std::string_view refer_dirs[];

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
    EXPECT_EQ(dirs[6], "\n");
    EXPECT_EQ(refer_dirs[6], "\n");
    EXPECT_EQ(pc_races[21], "\n");
    EXPECT_EQ(pc_race_types[21], "\n");
    EXPECT_EQ(pc_race_keywords[21], "\n");
    EXPECT_EQ(pc_star_types[21], "\n");
    EXPECT_EQ(pc_named_star_types[21], "\n");
    EXPECT_EQ(color_color[15], "\n");
    EXPECT_EQ(fill[7], "\n");
}

TEST(StringViewUtility, NullableNameMatchingRejectsNullTextWithoutConstructingAView)
{
    EXPECT_EQ(isname_nullable(nullptr, "sword shield"), 0);
    EXPECT_EQ(isname_nullable("sword", nullptr), 0);
}

TEST(StringViewUtility, NullableComparisonsPreserveDeterministicNullOrdering)
{
    EXPECT_EQ(str_cmp_nullable(nullptr, nullptr), 0);
    EXPECT_LT(str_cmp_nullable(nullptr, "value"), 0);
    EXPECT_GT(str_cmp_nullable("value", nullptr), 0);
    EXPECT_EQ(strn_cmp_nullable(nullptr, nullptr, 5), 0);
}
