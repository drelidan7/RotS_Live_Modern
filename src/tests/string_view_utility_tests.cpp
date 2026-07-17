#include "../handler.h"
#include "../interpre.h"
#include "../utils.h"
#include "rots/core/character.h"
#include "rots/core/tables.h"

#include "scoped_allocation_counter.h"

#include <gtest/gtest.h>

#include <array>
#include <concepts>
#include <cstring>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

static_assert(std::same_as<std::remove_cvref_t<decltype(weekdays[0])>, std::string_view>);
static_assert(std::same_as<std::remove_cvref_t<decltype(month_name[0])>, std::string_view>);
static_assert(std::same_as<std::remove_cvref_t<decltype(moon_phase[0])>, std::string_view>);

extern const std::string_view dirs[];
extern const std::string_view refer_dirs[];

// Legacy in-place "N.keyword" tokenizer being replaced by parse_numbered_name; not declared
// in any header (each .cpp that calls it forward-declares it locally the same way).
extern int get_number(char** name);

// Global linked list of all characters in the game (defined in db.cpp); declared extern
// locally here the same way every other .cpp consuming it does (it has no header declaration).
extern struct char_data* character_list;

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
    EXPECT_EQ(str_cmp({ }, { }), 0);
    EXPECT_LT(str_cmp({ }, "value"), 0);
    EXPECT_GT(str_cmp("value", { }), 0);
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
    EXPECT_EQ(isname({ }, "sword shield"), 0);
}

TEST(StringViewUtility, OldSearchBlockRejectsWordsLongerThanBoundedEntries)
{
    // The entry view is carved from a longer backing buffer whose next characters continue the
    // query word, so any read past the entry's end sees matching text instead of a null
    // terminator. A searched word longer than the entry must itself mean "no match".
    static constexpr std::string_view backing_word("northward");
    const std::array<std::string_view, 2> table { backing_word.substr(0, 5), std::string_view("\n") };

    char argument[] = "northward";
    EXPECT_EQ(old_search_block(argument, 0, 9, table.data(), 0), -1);
}

TEST(StringViewUtility, OldSearchBlockMatchesPrefixesWithinEntryBounds)
{
    static constexpr std::array<std::string_view, 3> table { "north", "nor", "\n" };

    char prefix_argument[] = "nor";
    char exact_argument[] = "north";
    char longer_argument[] = "northward";

    EXPECT_EQ(old_search_block(prefix_argument, 0, 3, table.data(), 0), 1);
    EXPECT_EQ(old_search_block(exact_argument, 0, 5, table.data(), 1), 1);
    EXPECT_EQ(old_search_block(longer_argument, 0, 9, table.data(), 1), -1);
    EXPECT_EQ(old_search_block(longer_argument, 0, 9, table.data(), 0), -1);
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

TEST(StringViewUtility, NullableNameMatchingMatchesBoundedLegacyRules)
{
    constexpr std::array matching_cases {
        std::tuple { std::string_view("  sword"), std::string_view("sword shield"), char(1) },
        std::tuple { std::string_view("sword"), std::string_view("sword shield"), char(1) },
        std::tuple { std::string_view("swor"), std::string_view("sword shield"), char(0) },
        std::tuple { std::string_view("swor"), std::string_view("sword shield"), char(1) },
        std::tuple { std::string_view("SWORD"), std::string_view("blade,sword"), char(1) },
        std::tuple { std::string_view(""), std::string_view("sword shield"), char(1) },
        std::tuple { std::string_view("mace"), std::string_view("sword shield"), char(1) },
        std::tuple { std::string_view("2handed"), std::string_view("2handed sword"), char(1) },
        std::tuple { std::string_view("2han"), std::string_view("2handed sword"), char(0) },
        std::tuple { std::string_view("42"), std::string_view("42"), char(1) },
        std::tuple { std::string_view("sword"), std::string_view("2handed sword"), char(1) },
        std::tuple { std::string_view("handed"), std::string_view("2handed sword"), char(1) },
        std::tuple { std::string_view("b"), std::string_view("a2b c"), char(1) },
        std::tuple { std::string_view("a2x"), std::string_view("a2b c"), char(1) },
        std::tuple { std::string_view("c"), std::string_view("a2b c"), char(1) },
    };

    for (const auto& [query, name_list, full] : matching_cases) {
        EXPECT_EQ(isname_nullable(query.data(), name_list.data(), full),
            isname(query, name_list, full));
    }

    constexpr char embedded_name_list[] = "blade\0sword";
    EXPECT_EQ(isname_nullable("sword", embedded_name_list), 0);
}

TEST(StringViewUtility, NameMatchingHonorsNonAlphabeticLeadingKeywords)
{
    // The legacy matcher attempts its first candidate at byte 0 verbatim, so namelists whose
    // first keyword starts with a digit or punctuation are matchable by that keyword.
    EXPECT_EQ(isname("2handed", "2handed sword"), 1);
    EXPECT_EQ(isname("42", "42"), 1);
    EXPECT_EQ(isname("sword", "2handed sword"), 1);
}

TEST(StringViewUtility, NullableComparisonsPreserveDeterministicNullOrdering)
{
    EXPECT_EQ(str_cmp_nullable(nullptr, nullptr), 0);
    EXPECT_LT(str_cmp_nullable(nullptr, "value"), 0);
    EXPECT_GT(str_cmp_nullable("value", nullptr), 0);
    EXPECT_EQ(strn_cmp_nullable(nullptr, nullptr, 5), 0);
}

TEST(StringViewUtility, NullableComparisonsMatchViewComparisons)
{
    constexpr std::array<std::pair<const char*, const char*>, 12> comparison_cases { {
        { "alpha", "alpha" },
        { "alpha", "ALPHA" },
        { "alpha", "alphabet" },
        { "alphabet", "alpha" },
        { "", "" },
        { "", "x" },
        { "x", "" },
        { "abc", "abd" },
        { "abd", "abc" },
        { "Zeta", "alpha" },
        { "alpha", "Zeta" },
        { "same", "sameness" },
    } };

    for (const auto& [first, second] : comparison_cases) {
        EXPECT_EQ(str_cmp_nullable(first, second), str_cmp(first, second))
            << first << " vs " << second;
        for (const int count : { 0, 1, 3, 100 }) {
            EXPECT_EQ(strn_cmp_nullable(first, second, count), strn_cmp(first, second, count))
                << first << " vs " << second << " n=" << count;
        }
    }
}

TEST(StringViewUtility, ParseNumberedNameMatchesLegacyGetNumber)
{
    constexpr std::array<const char*, 11> numbered_cases {
        "sword", "2.sword", ".sword", "x.sword", "2.3.sword", "0.sword", "12.long name", "7.",
        "", "-1.sword", "+2.sword"
    };

    for (const char* text : numbered_cases) {
        char legacy_buffer[MAX_INPUT_LENGTH];
        std::strcpy(legacy_buffer, text);
        char* legacy_cursor = legacy_buffer;
        const int legacy_number = get_number(&legacy_cursor);

        const auto parsed = parse_numbered_name(text);
        EXPECT_EQ(parsed.match_number, legacy_number) << text;
        if (legacy_number != 0) {
            EXPECT_EQ(parsed.name, std::string_view(legacy_cursor)) << text;
        }
    }
}

// Restores character_list on scope exit so a failing assertion cannot leak a
// dangling stack pointer into later tests.
struct ScopedCharacterListSwap {
    // The character_list value to restore at destruction.
    char_data* saved_character_list;

    explicit ScopedCharacterListSwap(char_data* replacement)
        : saved_character_list(character_list)
    {
        character_list = replacement;
    }
    ~ScopedCharacterListSwap() { character_list = saved_character_list; }
};

TEST(StringViewUtility, GetCharLookupPerformsNoHeapAllocations)
{
    char_data lookup_target { };
    lookup_target.player.name = const_cast<char*>("silverbeard-the-elder-of-erebor");
    ScopedCharacterListSwap character_list_guard(&lookup_target);

    rots_test::ScopedAllocationCounter counter;
    char_data* const found = get_char("1.silverbeard-the-elder-of-erebor");
    EXPECT_EQ(counter.allocations(), 0u);
    EXPECT_EQ(found, &lookup_target);
}
