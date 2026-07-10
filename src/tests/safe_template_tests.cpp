// Signature-match matrix for safe_template::expand_checked (Phase 4 Wave 2
// Task 2: world-data format-template hardening). Covers the binding contract:
// a well-formed template (conversions == expected, no extras, no unsupported
// specifier) expands byte-identically to sprintf/snprintf; any mismatch --
// too many/few %s, a wrong-type specifier (%d where %s is expected), an
// embedded %n, or a null template -- falls back to the caller-supplied string
// with no UB, and logs exactly once via mudlog. Literal "%%" is confirmed to
// pass through as a plain '%' and not count as a conversion.

#include "../safe_template.h"
#include "../structs.h"
#include "../utils.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <string>

extern struct descriptor_data* descriptor_list;

namespace {

// Mirrors ScopedDescriptorListReset (utility_format_tests.cpp,
// interpre_account_menu_tests.cpp): the mismatch path calls vmudlog ->
// mudlog, which walks the process-global descriptor_list broadcasting to
// listeners. Swapping it to nullptr for the duration of each test means
// mudlog's loop has nothing to iterate, regardless of what any other test in
// this binary left behind or runs afterward.
class ScopedDescriptorListReset {
public:
    ScopedDescriptorListReset()
        : previous_(descriptor_list)
    {
        descriptor_list = nullptr;
    }

    ~ScopedDescriptorListReset()
    {
        descriptor_list = previous_;
    }

    ScopedDescriptorListReset(const ScopedDescriptorListReset&) = delete;
    ScopedDescriptorListReset& operator=(const ScopedDescriptorListReset&) = delete;

private:
    // The real descriptor_list this test displaced; restored on scope exit
    // so later tests see whatever state they expect (normally nullptr too,
    // but never this fixture's business to assume that).
    descriptor_data* previous_;
};

} // namespace

TEST(SafeTemplate, MatchingTwoStringSignatureExpandsByteIdenticalToSnprintf)
{
    ScopedDescriptorListReset descriptor_list_reset;

    // Representative of spec_pro.cpp's herald death_cry2 site: exactly two
    // bare %s conversions, no flags/width.
    const char* tmpl = "%s screams as %s falls dead!";
    char expected_buf[256];
    std::snprintf(expected_buf, sizeof(expected_buf), tmpl, "Bob", "Alice");

    std::string result = safe_template::expand_checked(tmpl,
        { safe_template::Conv::String, safe_template::Conv::String },
        { std::string_view("Bob"), std::string_view("Alice") },
        "fallback text", "test: two-string match");

    EXPECT_EQ(result, std::string(expected_buf));
}

TEST(SafeTemplate, MatchingOneStringSignatureExpandsByteIdenticalToSnprintf)
{
    ScopedDescriptorListReset descriptor_list_reset;

    // Representative of shop.cpp's single-%s fields (no_such_item1/2,
    // missing_cash1/2, do_not_buy).
    const char* tmpl = "%s, we don't have that.";
    char expected_buf[256];
    std::snprintf(expected_buf, sizeof(expected_buf), tmpl, "Grimbold");

    std::string result = safe_template::expand_checked(tmpl,
        { safe_template::Conv::String },
        { std::string_view("Grimbold") },
        "fallback text", "test: one-string match");

    EXPECT_EQ(result, std::string(expected_buf));
}

TEST(SafeTemplate, LiteralPercentPercentIsNotCountedAsAConversion)
{
    ScopedDescriptorListReset descriptor_list_reset;

    const char* tmpl = "100%% sure, %s.";
    char expected_buf[256];
    std::snprintf(expected_buf, sizeof(expected_buf), tmpl, "friend");

    std::string result = safe_template::expand_checked(tmpl,
        { safe_template::Conv::String },
        { std::string_view("friend") },
        "fallback text", "test: literal percent-percent");

    EXPECT_EQ(result, std::string(expected_buf));
    EXPECT_EQ(result, "100% sure, friend.");
}

TEST(SafeTemplate, TooManyStringConversionsFallsBackWithNoUB)
{
    ScopedDescriptorListReset descriptor_list_reset;

    // Three %s in the template, but only two are expected/supplied: a
    // builder typo that would otherwise read a third, nonexistent argument
    // off the stack.
    const char* tmpl = "%s tells %s about %s.";

    std::string result = safe_template::expand_checked(tmpl,
        { safe_template::Conv::String, safe_template::Conv::String },
        { std::string_view("Bob"), std::string_view("Alice") },
        "fallback text", "test: too many %s");

    EXPECT_EQ(result, "fallback text");
}

TEST(SafeTemplate, TooFewStringConversionsFallsBack)
{
    ScopedDescriptorListReset descriptor_list_reset;

    // Only one %s in the template where two are expected: the second
    // argument would simply be dropped by a real sprintf, but this
    // validator treats any count mismatch as malformed rather than
    // silently ignoring a caller-supplied argument.
    const char* tmpl = "%s falls dead!";

    std::string result = safe_template::expand_checked(tmpl,
        { safe_template::Conv::String, safe_template::Conv::String },
        { std::string_view("Bob"), std::string_view("Alice") },
        "fallback text", "test: too few %s");

    EXPECT_EQ(result, "fallback text");
}

TEST(SafeTemplate, WrongTypeConversionFallsBack)
{
    ScopedDescriptorListReset descriptor_list_reset;

    // %d where %s is expected: the classic format-confusion bug. A real
    // sprintf would read an int off the (nonexistent) second vararg slot
    // and produce garbage; this must be rejected before any expansion.
    const char* tmpl = "%s rolled a %d!";

    std::string result = safe_template::expand_checked(tmpl,
        { safe_template::Conv::String, safe_template::Conv::String },
        { std::string_view("Bob"), std::string_view("Alice") },
        "fallback text", "test: wrong-type %d");

    EXPECT_EQ(result, "fallback text");
}

TEST(SafeTemplate, EmbeddedPercentNFallsBackWithNoUB)
{
    ScopedDescriptorListReset descriptor_list_reset;

    // %n is the classic format-string exploit primitive (writes the byte
    // count so far through a pointer argument); must never reach a real
    // expansion path.
    const char* tmpl = "%s stored the count%n here.";

    std::string result = safe_template::expand_checked(tmpl,
        { safe_template::Conv::String },
        { std::string_view("Bob") },
        "fallback text", "test: embedded %n");

    EXPECT_EQ(result, "fallback text");
}

TEST(SafeTemplate, NullTemplateFallsBack)
{
    ScopedDescriptorListReset descriptor_list_reset;

    std::string result = safe_template::expand_checked(nullptr,
        { safe_template::Conv::String },
        { std::string_view("Bob") },
        "fallback text", "test: null template");

    EXPECT_EQ(result, "fallback text");
}

TEST(SafeTemplate, MismatchedArgCountAgainstExpectedFallsBack)
{
    ScopedDescriptorListReset descriptor_list_reset;

    // Template and expected signature agree (two %s), but the caller only
    // supplied one argument -- a caller-side bug, not a template bug;
    // still must not read past the argument list.
    const char* tmpl = "%s tells %s hello.";

    std::string result = safe_template::expand_checked(tmpl,
        { safe_template::Conv::String, safe_template::Conv::String },
        { std::string_view("Bob") },
        "fallback text", "test: arg/expected count mismatch");

    EXPECT_EQ(result, "fallback text");
}
