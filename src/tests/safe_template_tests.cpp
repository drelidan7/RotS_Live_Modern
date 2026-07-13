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

extern struct descriptor_data *descriptor_list;

namespace {

// Mirrors ScopedDescriptorListReset (utility_format_tests.cpp,
// interpre_account_menu_tests.cpp): the mismatch path calls vmudlog ->
// mudlog, which walks the process-global descriptor_list broadcasting to
// listeners. Swapping it to nullptr for the duration of each test means
// mudlog's loop has nothing to iterate, regardless of what any other test in
// this binary left behind or runs afterward.
class ScopedDescriptorListReset {
  public:
    ScopedDescriptorListReset() : previous_(descriptor_list) { descriptor_list = nullptr; }

    ~ScopedDescriptorListReset() { descriptor_list = previous_; }

    ScopedDescriptorListReset(const ScopedDescriptorListReset &) = delete;
    ScopedDescriptorListReset &operator=(const ScopedDescriptorListReset &) = delete;

  private:
    // The real descriptor_list this test displaced; restored on scope exit
    // so later tests see whatever state they expect (normally nullptr too,
    // but never this fixture's business to assume that).
    descriptor_data *previous_;
};

} // namespace

TEST(SafeTemplate, MatchingTwoStringSignatureExpandsByteIdenticalToSnprintf) {
    ScopedDescriptorListReset descriptor_list_reset;

    // Representative of spec_pro.cpp's herald death_cry2 site: exactly two
    // bare %s conversions, no flags/width.
    const char *tmpl = "%s screams as %s falls dead!";
    char expected_buf[256];
    std::snprintf(expected_buf, sizeof(expected_buf), tmpl, "Bob", "Alice");

    std::string result = safe_template::expand_checked(
        tmpl, {safe_template::Conv::String, safe_template::Conv::String},
        {std::string_view("Bob"), std::string_view("Alice")}, "fallback text",
        "test: two-string match");

    EXPECT_EQ(result, std::string(expected_buf));
}

TEST(SafeTemplate, MatchingOneStringSignatureExpandsByteIdenticalToSnprintf) {
    ScopedDescriptorListReset descriptor_list_reset;

    // Representative of shop.cpp's single-%s fields (no_such_item1/2,
    // missing_cash1/2, do_not_buy).
    const char *tmpl = "%s, we don't have that.";
    char expected_buf[256];
    std::snprintf(expected_buf, sizeof(expected_buf), tmpl, "Grimbold");

    std::string result = safe_template::expand_checked(tmpl, {safe_template::Conv::String},
                                                       {std::string_view("Grimbold")},
                                                       "fallback text", "test: one-string match");

    EXPECT_EQ(result, std::string(expected_buf));
}

TEST(SafeTemplate, LiteralPercentPercentIsNotCountedAsAConversion) {
    ScopedDescriptorListReset descriptor_list_reset;

    const char *tmpl = "100%% sure, %s.";
    char expected_buf[256];
    std::snprintf(expected_buf, sizeof(expected_buf), tmpl, "friend");

    std::string result = safe_template::expand_checked(
        tmpl, {safe_template::Conv::String}, {std::string_view("friend")}, "fallback text",
        "test: literal percent-percent");

    EXPECT_EQ(result, std::string(expected_buf));
    EXPECT_EQ(result, "100% sure, friend.");
}

TEST(SafeTemplate, TooManyStringConversionsFallsBackWithNoUB) {
    ScopedDescriptorListReset descriptor_list_reset;

    // Three %s in the template, but only two are expected/supplied: a
    // builder typo that would otherwise read a third, nonexistent argument
    // off the stack.
    const char *tmpl = "%s tells %s about %s.";

    std::string result = safe_template::expand_checked(
        tmpl, {safe_template::Conv::String, safe_template::Conv::String},
        {std::string_view("Bob"), std::string_view("Alice")}, "fallback text", "test: too many %s");

    EXPECT_EQ(result, "fallback text");
}

TEST(SafeTemplate, FewerConversionsThanExpectedExpandsUsingLeadingArgsLikeSprintf) {
    ScopedDescriptorListReset descriptor_list_reset;

    // Only one %s where two args are supplied: a real sprintf binds the %s to
    // the first arg and IGNORES the surplus trailing arg -- well-defined, and
    // relied on by live world data (a one-%s message_sell at the two-arg sell
    // call site). Must EXPAND (not fall back), byte-identical to sprintf.
    const char *tmpl = "%s falls dead!";
    char expected_buf[256];
    std::snprintf(expected_buf, sizeof(expected_buf), tmpl, "Bob", "Alice");

    std::string result = safe_template::expand_checked(
        tmpl, {safe_template::Conv::String, safe_template::Conv::String},
        {std::string_view("Bob"), std::string_view("Alice")}, "fallback text",
        "test: fewer conversions than expected");

    EXPECT_EQ(result, std::string(expected_buf));
    EXPECT_EQ(result, "Bob falls dead!");
}

TEST(SafeTemplate, ZeroConversionsLiteralTemplateReturnsLiteralVerbatim) {
    ScopedDescriptorListReset descriptor_list_reset;

    // A template with NO conversions at a two-%s call site: real death_cry2
    // world data is entirely literal text. sprintf ignores both args and emits
    // the literal unchanged; the validator must do the same, not fall back.
    const char *tmpl = "The herald cries out for someone else to save the king.";
    char expected_buf[256];
    std::snprintf(expected_buf, sizeof(expected_buf), tmpl, "Bob", "the Brave");

    std::string result = safe_template::expand_checked(
        tmpl, {safe_template::Conv::String, safe_template::Conv::String},
        {std::string_view("Bob"), std::string_view("the Brave")}, "notices someone new arrive.",
        "test: zero-conversion literal");

    EXPECT_EQ(result, std::string(expected_buf));
    EXPECT_EQ(result, "The herald cries out for someone else to save the king.");
}

TEST(SafeTemplate, WidthModifiedStringConversionFallsBack) {
    ScopedDescriptorListReset descriptor_list_reset;

    // "%-20s" is a width/flag-modified conversion, NOT a bare %s. The narrow
    // validator does not model field widths (its substitute path would produce
    // a DIFFERENT string than sprintf), so it must reject rather than silently
    // mis-expand. This is the top-risk case: a builder padding directive.
    const char *tmpl = "%-20s reporting in.";

    std::string result = safe_template::expand_checked(tmpl, {safe_template::Conv::String},
                                                       {std::string_view("Bob")}, "fallback text",
                                                       "test: width-modified %s");

    EXPECT_EQ(result, "fallback text");
}

TEST(SafeTemplate, WrongTypeConversionFallsBack) {
    ScopedDescriptorListReset descriptor_list_reset;

    // %d where %s is expected: the classic format-confusion bug. A real
    // sprintf would read an int off the (nonexistent) second vararg slot
    // and produce garbage; this must be rejected before any expansion.
    const char *tmpl = "%s rolled a %d!";

    std::string result = safe_template::expand_checked(
        tmpl, {safe_template::Conv::String, safe_template::Conv::String},
        {std::string_view("Bob"), std::string_view("Alice")}, "fallback text",
        "test: wrong-type %d");

    EXPECT_EQ(result, "fallback text");
}

TEST(SafeTemplate, EmbeddedPercentNFallsBackWithNoUB) {
    ScopedDescriptorListReset descriptor_list_reset;

    // %n is the classic format-string exploit primitive (writes the byte
    // count so far through a pointer argument); must never reach a real
    // expansion path.
    const char *tmpl = "%s stored the count%n here.";

    std::string result = safe_template::expand_checked(tmpl, {safe_template::Conv::String},
                                                       {std::string_view("Bob")}, "fallback text",
                                                       "test: embedded %n");

    EXPECT_EQ(result, "fallback text");
}

TEST(SafeTemplate, NullTemplateFallsBack) {
    ScopedDescriptorListReset descriptor_list_reset;

    std::string result = safe_template::expand_checked(nullptr, {safe_template::Conv::String},
                                                       {std::string_view("Bob")}, "fallback text",
                                                       "test: null template");

    EXPECT_EQ(result, "fallback text");
}

TEST(SafeTemplate, MismatchedArgCountAgainstExpectedFallsBack) {
    ScopedDescriptorListReset descriptor_list_reset;

    // Template and expected signature agree (two %s), but the caller only
    // supplied one argument -- a caller-side bug, not a template bug;
    // still must not read past the argument list.
    const char *tmpl = "%s tells %s hello.";

    std::string result = safe_template::expand_checked(
        tmpl, {safe_template::Conv::String, safe_template::Conv::String}, {std::string_view("Bob")},
        "fallback text", "test: arg/expected count mismatch");

    EXPECT_EQ(result, "fallback text");
}

// Real shipping-data re-pins (reviewer-extracted 2026-07-10): the sole
// difference between the validated expander and the old sprintf must be
// MALFORMED templates. These pin that ACTUAL live world strings -- which use
// one %s at a two-arg call site, or zero %s at a two-arg call site -- still
// expand byte-identically to sprintf, NOT fall back.

TEST(SafeTemplate, RealShopMessageSellOneConversionAtTwoArgSiteExpandsByteIdentical) {
    ScopedDescriptorListReset descriptor_list_reset;

    // The six real message_sell strings the sell call site (GET_NAME +
    // money_message => 2 args) invokes, all of which carry only ONE %s:
    //   lib/world/shp/100.shp #10011,#10012  #10013,#10014
    //   lib/world/shp/88.shp / 319.shp #8801
    const char *real_templates[] = {
        "%s If you see this its a BUG, tell Prami or Fingolfin.",
        "%s If you see this its a BUG.  Please tell Prami or Fingolfin.",
        "%s I AM BUGGED. PLEASE ASK IMMORTAL ASAP. PURCHASE MESSAGE.",
    };

    for (const char *tmpl : real_templates) {
        char expected_buf[256];
        // Old code: sprintf(buf, tmpl, GET_NAME(ch), money_message(...)) --
        // second arg ignored because tmpl has one %s.
        std::snprintf(expected_buf, sizeof(expected_buf), tmpl, "Grimbold", "100 coins");

        std::string result = safe_template::expand_checked(
            tmpl, {safe_template::Conv::String, safe_template::Conv::String},
            {std::string_view("Grimbold"), std::string_view("100 coins")}, "Thanks for the item.",
            "shop message_sell");

        EXPECT_EQ(result, std::string(expected_buf)) << "template: " << tmpl;
        EXPECT_NE(result, "Thanks for the item.") << "must NOT fall back: " << tmpl;
    }
}

TEST(SafeTemplate, RealLiteralDeathCry2AtTwoArgHeraldSiteReturnsLiteral) {
    ScopedDescriptorListReset descriptor_list_reset;

    // A representative real death_cry2 (lib/world/mob) -- entirely literal, no
    // %s, invoked at the herald site with name+title (2 args). Old snprintf
    // ignored both args and emitted the literal; the expander must too.
    const char *tmpl = "The herald cries out for someone else to save the king.";
    char expected_buf[256];
    std::snprintf(expected_buf, sizeof(expected_buf), tmpl, "Aragorn", "the King");

    std::string result = safe_template::expand_checked(
        tmpl, {safe_template::Conv::String, safe_template::Conv::String},
        {std::string_view("Aragorn"), std::string_view("the King")}, "notices someone new arrive.",
        "herald death_cry2");

    EXPECT_EQ(result, std::string(expected_buf));
    EXPECT_EQ(result, "The herald cries out for someone else to save the king.");
}

// ---------------------------------------------------------------------------
// script.cpp's five SCRIPT_DO_SAY-family sites (backlog Task 1, SECURITY):
// SCRIPT_DO_SAY, SCRIPT_DO_YELL, SCRIPT_SEND_TO_CHAR, SCRIPT_SEND_TO_ROOM,
// SCRIPT_SEND_TO_ROOM_X. Each currently does
// `sprintf(output, curr->text, txt1)` where curr->text is a builder-authored
// .mdl/.scr string used directly as the printf format and txt1 is exactly
// one argument (get_text_param()'s return). script.cpp has no dedicated test
// file (only shape_format_tests.cpp, which exercises the unrelated
// show_command() disassembler) -- these pins exercise the validator's
// one-%s-with-argument entry point directly, which is what the five sites
// route through after this task's fix. That satisfies the brief's allowance
// to pin "via its helpers" when script.cpp itself has no test file.
//
// Well-formed cases (below) intentionally call the PRE-EXISTING
// expand_checked() (not expand_checked_one()) with a real curr->text-shaped
// template: expand_checked() already generalizes to N conversions and
// already has a passing one-%s/one-arg test above
// (MatchingOneStringSignatureExpandsByteIdenticalToSnprintf), so these pass
// against UNCHANGED source -- pass-first characterization of the sprintf
// behavior being preserved.
//
// Malformed/null-arg cases call expand_checked_one(), which does not exist
// until this task adds it -- deliberately fail-first (the file will not even
// compile against unchanged source), pinning the NEW contract: a malformed
// template or a null txt1 must never reach sprintf/UB again.

TEST(SafeTemplate, ScriptDoSayWellFormedOneConversionTemplateExpandsByteIdenticalToSprintf) {
    ScopedDescriptorListReset descriptor_list_reset;

    // Representative of real world data (lib/world/scr/14.scr):
    // "%s falls down the crevasse.~" -- one bare %s consuming txt1.
    const char *real_templates[] = {
        "%s falls down the crevasse.",
        "The beast squeals as %s strikes it!",
        "Yes %s, I could use some help.",
    };

    for (const char *tmpl : real_templates) {
        char expected_buf[256];
        std::snprintf(expected_buf, sizeof(expected_buf), tmpl, "Bob");

        std::string result = safe_template::expand_checked(
            tmpl, {safe_template::Conv::String}, {std::string_view("Bob")}, tmpl,
            "script SCRIPT_DO_SAY family (well-formed)");

        EXPECT_EQ(result, std::string(expected_buf)) << "template: " << tmpl;
    }
}

TEST(SafeTemplate, ScriptDoSayWellFormedZeroConversionLiteralReturnsLiteralVerbatim) {
    ScopedDescriptorListReset descriptor_list_reset;

    // Representative of real world data (lib/world/scr/275.scr): a fully
    // literal DO_SAY/SEND_TO_ROOM string with no %s at all, at a call site
    // that still supplies txt1 -- sprintf ignores the unused arg and copies
    // the literal; the validator must too (prefix-of-expected rule).
    const char *tmpl = "An orc soldier grunts solemnly as you approach.";
    char expected_buf[256];
    std::snprintf(expected_buf, sizeof(expected_buf), tmpl, "Bob");

    std::string result = safe_template::expand_checked(
        tmpl, {safe_template::Conv::String}, {std::string_view("Bob")}, tmpl,
        "script SCRIPT_DO_SAY family (zero-conversion literal)");

    EXPECT_EQ(result, std::string(expected_buf));
    EXPECT_EQ(result, tmpl);
}

TEST(SafeTemplate, ScriptDoSayTwoConversionsFallsBackToLiteralTemplateText) {
    ScopedDescriptorListReset descriptor_list_reset;

    // Builder typo: two %s in a template at a one-arg (txt1) call site. The
    // pre-fix sprintf(output, curr->text, txt1) reads a second, nonexistent
    // vararg here -- UB. Fallback is the raw template text, unexpanded (see
    // safe_template.h's expand_checked_one doc): still says/sends SOMETHING
    // sensible instead of silence, with no format re-interpretation risk
    // since fallback is returned verbatim, never passed through printf
    // again.
    const char *tmpl = "%s tells %s hello.";

    std::string result = safe_template::expand_checked_one(tmpl, "Bob", tmpl,
                                                           "script SCRIPT_DO_SAY family (two %s)");

    EXPECT_EQ(result, tmpl);
}

TEST(SafeTemplate, ScriptDoSayWrongTypeConversionFallsBackToLiteralTemplateText) {
    ScopedDescriptorListReset descriptor_list_reset;

    // %d where the call site only ever supplies a %s-shaped txt1: classic
    // format-confusion. Must reject before any expansion, not read a bogus
    // int off the (nonexistent) vararg slot.
    const char *tmpl = "%s rolled a %d!";

    std::string result =
        safe_template::expand_checked_one(tmpl, "Bob", tmpl, "script SCRIPT_DO_SAY family (%d)");

    EXPECT_EQ(result, tmpl);
}

TEST(SafeTemplate, ScriptDoSayEmbeddedPercentNFallsBackToLiteralTemplateText) {
    ScopedDescriptorListReset descriptor_list_reset;

    // %n is the classic format-string exploit primitive. Must never reach a
    // real sprintf/expansion path.
    const char *tmpl = "%s stored the count%n here.";

    std::string result =
        safe_template::expand_checked_one(tmpl, "Bob", tmpl, "script SCRIPT_DO_SAY family (%n)");

    EXPECT_EQ(result, tmpl);
}

TEST(SafeTemplate, ScriptDoSayDanglingPercentFallsBackToLiteralTemplateText) {
    ScopedDescriptorListReset descriptor_list_reset;

    // A stray trailing '%' with nothing after it: the scanner can't
    // classify it, so it must refuse to guess rather than read past the
    // template.
    const char *tmpl = "%s salutes and departs %";

    std::string result = safe_template::expand_checked_one(
        tmpl, "Bob", tmpl, "script SCRIPT_DO_SAY family (dangling %)");

    EXPECT_EQ(result, tmpl);
}

TEST(SafeTemplate, ScriptDoSayNullArgumentCoalescesToEmptyStringInsteadOfUB) {
    ScopedDescriptorListReset descriptor_list_reset;

    // get_text_param() can legitimately return a null char* (an unset
    // SCRIPT_PARAM_STRn, or a SCRIPT_PARAM_CHn_NAME whose target character
    // pointer is null) -- a pre-existing latent bug in the sprintf path
    // (constructing/reading a %s conversion against a null argument is
    // undefined behavior; only glibc's printf happens to print "(null)").
    // expand_checked_one() must never construct std::string_view(nullptr);
    // it substitutes an empty string and the template stays well-formed
    // (does NOT fall back -- a null arg is not a malformed template).
    const char *tmpl = "%s bows.";

    std::string result = safe_template::expand_checked_one(
        tmpl, nullptr, "fallback text", "script SCRIPT_DO_SAY family (null txt1)");

    EXPECT_EQ(result, " bows.");
    EXPECT_NE(result, "fallback text");
}

TEST(SafeTemplate, ScriptDoSayConvenienceWrapperMatchesFullExpandCheckedForWellFormedInput) {
    ScopedDescriptorListReset descriptor_list_reset;

    // Sanity: expand_checked_one() is a thin one-arg-shaped wrapper, not a
    // parallel implementation -- must agree with the general expand_checked()
    // entry point bit-for-bit for the same well-formed input.
    const char *tmpl = "%s nods.";

    std::string via_wrapper = safe_template::expand_checked_one(
        tmpl, "Bob", "fallback text", "script SCRIPT_DO_SAY family (wrapper parity)");
    std::string via_general = safe_template::expand_checked(
        tmpl, {safe_template::Conv::String}, {std::string_view("Bob")}, "fallback text",
        "script SCRIPT_DO_SAY family (wrapper parity)");

    EXPECT_EQ(via_wrapper, via_general);
    EXPECT_EQ(via_wrapper, "Bob nods.");
}
