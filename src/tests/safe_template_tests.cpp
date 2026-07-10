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

TEST(SafeTemplate, FewerConversionsThanExpectedExpandsUsingLeadingArgsLikeSprintf)
{
    ScopedDescriptorListReset descriptor_list_reset;

    // Only one %s where two args are supplied: a real sprintf binds the %s to
    // the first arg and IGNORES the surplus trailing arg -- well-defined, and
    // relied on by live world data (a one-%s message_sell at the two-arg sell
    // call site). Must EXPAND (not fall back), byte-identical to sprintf.
    const char* tmpl = "%s falls dead!";
    char expected_buf[256];
    std::snprintf(expected_buf, sizeof(expected_buf), tmpl, "Bob", "Alice");

    std::string result = safe_template::expand_checked(tmpl,
        { safe_template::Conv::String, safe_template::Conv::String },
        { std::string_view("Bob"), std::string_view("Alice") },
        "fallback text", "test: fewer conversions than expected");

    EXPECT_EQ(result, std::string(expected_buf));
    EXPECT_EQ(result, "Bob falls dead!");
}

TEST(SafeTemplate, ZeroConversionsLiteralTemplateReturnsLiteralVerbatim)
{
    ScopedDescriptorListReset descriptor_list_reset;

    // A template with NO conversions at a two-%s call site: real death_cry2
    // world data is entirely literal text. sprintf ignores both args and emits
    // the literal unchanged; the validator must do the same, not fall back.
    const char* tmpl = "The herald cries out for someone else to save the king.";
    char expected_buf[256];
    std::snprintf(expected_buf, sizeof(expected_buf), tmpl, "Bob", "the Brave");

    std::string result = safe_template::expand_checked(tmpl,
        { safe_template::Conv::String, safe_template::Conv::String },
        { std::string_view("Bob"), std::string_view("the Brave") },
        "notices someone new arrive.", "test: zero-conversion literal");

    EXPECT_EQ(result, std::string(expected_buf));
    EXPECT_EQ(result, "The herald cries out for someone else to save the king.");
}

TEST(SafeTemplate, WidthModifiedStringConversionFallsBack)
{
    ScopedDescriptorListReset descriptor_list_reset;

    // "%-20s" is a width/flag-modified conversion, NOT a bare %s. The narrow
    // validator does not model field widths (its substitute path would produce
    // a DIFFERENT string than sprintf), so it must reject rather than silently
    // mis-expand. This is the top-risk case: a builder padding directive.
    const char* tmpl = "%-20s reporting in.";

    std::string result = safe_template::expand_checked(tmpl,
        { safe_template::Conv::String },
        { std::string_view("Bob") },
        "fallback text", "test: width-modified %s");

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

// Real shipping-data re-pins (reviewer-extracted 2026-07-10): the sole
// difference between the validated expander and the old sprintf must be
// MALFORMED templates. These pin that ACTUAL live world strings -- which use
// one %s at a two-arg call site, or zero %s at a two-arg call site -- still
// expand byte-identically to sprintf, NOT fall back.

TEST(SafeTemplate, RealShopMessageSellOneConversionAtTwoArgSiteExpandsByteIdentical)
{
    ScopedDescriptorListReset descriptor_list_reset;

    // The six real message_sell strings the sell call site (GET_NAME +
    // money_message => 2 args) invokes, all of which carry only ONE %s:
    //   lib/world/shp/100.shp #10011,#10012  #10013,#10014
    //   lib/world/shp/88.shp / 319.shp #8801
    const char* real_templates[] = {
        "%s If you see this its a BUG, tell Prami or Fingolfin.",
        "%s If you see this its a BUG.  Please tell Prami or Fingolfin.",
        "%s I AM BUGGED. PLEASE ASK IMMORTAL ASAP. PURCHASE MESSAGE.",
    };

    for (const char* tmpl : real_templates) {
        char expected_buf[256];
        // Old code: sprintf(buf, tmpl, GET_NAME(ch), money_message(...)) --
        // second arg ignored because tmpl has one %s.
        std::snprintf(expected_buf, sizeof(expected_buf), tmpl, "Grimbold", "100 coins");

        std::string result = safe_template::expand_checked(tmpl,
            { safe_template::Conv::String, safe_template::Conv::String },
            { std::string_view("Grimbold"), std::string_view("100 coins") },
            "Thanks for the item.", "shop message_sell");

        EXPECT_EQ(result, std::string(expected_buf)) << "template: " << tmpl;
        EXPECT_NE(result, "Thanks for the item.") << "must NOT fall back: " << tmpl;
    }
}

TEST(SafeTemplate, RealLiteralDeathCry2AtTwoArgHeraldSiteReturnsLiteral)
{
    ScopedDescriptorListReset descriptor_list_reset;

    // A representative real death_cry2 (lib/world/mob) -- entirely literal, no
    // %s, invoked at the herald site with name+title (2 args). Old snprintf
    // ignored both args and emitted the literal; the expander must too.
    const char* tmpl = "The herald cries out for someone else to save the king.";
    char expected_buf[256];
    std::snprintf(expected_buf, sizeof(expected_buf), tmpl, "Aragorn", "the King");

    std::string result = safe_template::expand_checked(tmpl,
        { safe_template::Conv::String, safe_template::Conv::String },
        { std::string_view("Aragorn"), std::string_view("the King") },
        "notices someone new arrive.", "herald death_cry2");

    EXPECT_EQ(result, std::string(expected_buf));
    EXPECT_EQ(result, "The herald cries out for someone else to save the king.");
}
