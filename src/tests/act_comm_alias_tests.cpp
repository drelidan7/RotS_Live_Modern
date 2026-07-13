#include "../db.h"
#include "../interpre.h"
#include "../structs.h"
#include "../utils.h"
#include "test_char_cleanup.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <string>

// Characterization tests for RAII Lifecycle-Audit Task 4 (char_data's
// specials.alias list -- see docs/superpowers/ownership-map.md's alias entry
// and .superpowers/sdd/task-4-report.md). These pin do_alias()'s (act_comm.cpp)
// add/list/replace/remove/overflow behavior and replace_aliases()'s
// (interpre.cpp) command-expansion behavior BYTE-FOR-BYTE -- confirmed
// passing against the pre-conversion `struct alias_list*` raw-pointer field
// -- before specials.alias became the owning `owned_alias_list` RAII wrapper,
// and green again after. Task 4 is the CONSERVATIVE-RAII option: the
// intrusive alias_list node shape, the on-disk format (already decoupled via
// objects_json::AliasData; see Crash_alias_load()/Crash_collect_alias_data()
// in objsave.cpp), and every read/CRUD algorithm in do_alias()/
// replace_aliases() are byte-for-byte unchanged -- only the ownership
// mechanism (a manual free_alias_list() call in free_char()) became
// RAII-explicit. These tests exist because neither function had any prior
// test coverage at all (do_show's "aliases" display branch was already
// pinned in act_wiz_format_tests.cpp; add/remove/replace/expand were not).

extern char buf[];

ACMD(do_alias);
void replace_aliases(struct char_data* ch, char* line);

namespace {

// Mirrors act_format_tests.cpp's helper: points a descriptor's output at its
// own small_outbuf so send_to_char() output can be inspected directly
// instead of going to a real socket. See that file's comment for why this
// must mutate the caller's descriptor_data in place rather than return one
// by value (descriptor_data::output is a self-pointer into small_outbuf[]).
void reset_capturing_descriptor(descriptor_data& descriptor, char_data* character)
{
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = 0; // CON_PLAYING
    descriptor.character = character;
}

// A single awake PC with a capturing descriptor, no world dependency --
// do_alias()/replace_aliases() never touch in_room/world[].
struct AliasTestContext {
    char_data character { };
    descriptor_data descriptor { };
    // Releases character.profs/skills/knowledge (clear_char() heap
    // allocations) at scope exit (Phase 5 T6 leak sweep).
    ScopedClearCharFields character_cleanup { character };
    // Releases character.specials.alias's chain at scope exit. Post-RAII-T4
    // this is belt-and-suspenders (owned_alias_list's own destructor, run
    // when `character`'s real ~char_data() executes, would free it too) --
    // kept because it's still load-bearing for a calloc'd/RELEASE()'d
    // fixture shape elsewhere in the suite, and harmless here (free_alias_list
    // is a no-op on an already-null chain).
    ScopedAliasListRelease character_alias_cleanup { character };
    // Returns descriptor.large_outbuf to bufpool at scope exit --
    // DoAliasRejectsNewAliasOncePerLevelLimitReached accumulates 32+
    // "You added the alias..." lines into the SAME captured buffer (it never
    // calls reset_capturing_descriptor() mid-loop), which overflows
    // small_outbuf and promotes to a heap-allocated large_outbuf block
    // (comm.cpp's write_to_output()); confirmed via LeakSanitizer
    // (rots64 linux-x64-sanitize) that this fixture leaks without this guard
    // -- same pre-existing pattern as SoloCharacterContext elsewhere (Phase 5
    // T6 leak sweep), unrelated to the alias RAII conversion itself.
    ScopedDescriptorLargeOutbufReturn descriptor_large_outbuf_cleanup { descriptor };

    AliasTestContext()
    {
        clear_char(&character, MOB_VOID);
        reset_capturing_descriptor(descriptor, &character);
        character.desc = &descriptor;
        character.specials.position = POSITION_STANDING;
        character.player.level = 1; // MAX_ALIAS = 30 + level*2 = 32
    }
};

char* mutable_argument(char* buffer, size_t buffer_size, const char* text)
{
    std::snprintf(buffer, buffer_size, "%s", text);
    return buffer;
}

} // namespace

TEST(ActCommAlias, DoAliasWithNoArgumentAndNoneDefinedReportsNoAliases)
{
    AliasTestContext ctx;

    char argument[MAX_INPUT_LENGTH];
    do_alias(&ctx.character, mutable_argument(argument, sizeof(argument), ""), nullptr, 0, 0);

    EXPECT_EQ(std::string(ctx.descriptor.output), "You have no aliases defined.\n\r");
}

TEST(ActCommAlias, DoAliasAddsNewAliasAndReportsAddition)
{
    AliasTestContext ctx;

    char argument[MAX_INPUT_LENGTH];
    do_alias(&ctx.character, mutable_argument(argument, sizeof(argument), "k kill"), nullptr, 0, 0);

    EXPECT_EQ(std::string(ctx.descriptor.output), "You added the alias 'k'.\n\r");
    ASSERT_NE(static_cast<alias_list*>(ctx.character.specials.alias), nullptr);
    EXPECT_STREQ(ctx.character.specials.alias->keyword, "k");
    EXPECT_STREQ(ctx.character.specials.alias->command, "kill");
    EXPECT_EQ(ctx.character.specials.alias->next, nullptr);
}

TEST(ActCommAlias, DoAliasWithNoArgumentListsDefinedAliasesMostRecentFirst)
{
    AliasTestContext ctx;

    char argument[MAX_INPUT_LENGTH];
    do_alias(&ctx.character, mutable_argument(argument, sizeof(argument), "k kill"), nullptr, 0, 0);
    do_alias(&ctx.character, mutable_argument(argument, sizeof(argument), "gr greet"), nullptr, 0,
        0);

    reset_capturing_descriptor(ctx.descriptor, &ctx.character);
    do_alias(&ctx.character, mutable_argument(argument, sizeof(argument), ""), nullptr, 0, 0);

    const std::string output = ctx.descriptor.output;
    EXPECT_EQ(output, "You have the following aliases defined:\n\r"
                      "gr                  : greet\n\r"
                      "k                   : kill\n\r");
}

TEST(ActCommAlias, DoAliasReplacesExistingAliasCommand)
{
    AliasTestContext ctx;

    char argument[MAX_INPUT_LENGTH];
    do_alias(&ctx.character, mutable_argument(argument, sizeof(argument), "k kill"), nullptr, 0, 0);

    reset_capturing_descriptor(ctx.descriptor, &ctx.character);
    do_alias(&ctx.character, mutable_argument(argument, sizeof(argument), "k kill all"), nullptr, 0,
        0);

    EXPECT_EQ(std::string(ctx.descriptor.output), "You replaced the alias 'k'.\n\r");
    ASSERT_NE(static_cast<alias_list*>(ctx.character.specials.alias), nullptr);
    EXPECT_STREQ(ctx.character.specials.alias->keyword, "k");
    EXPECT_STREQ(ctx.character.specials.alias->command, "kill all");
    // Replacing reuses the existing node rather than allocating a new one --
    // still a single-entry chain.
    EXPECT_EQ(ctx.character.specials.alias->next, nullptr);
}

TEST(ActCommAlias, DoAliasRemovesExistingAliasAndLeavesListEmpty)
{
    AliasTestContext ctx;

    char argument[MAX_INPUT_LENGTH];
    do_alias(&ctx.character, mutable_argument(argument, sizeof(argument), "k kill"), nullptr, 0, 0);

    reset_capturing_descriptor(ctx.descriptor, &ctx.character);
    do_alias(&ctx.character, mutable_argument(argument, sizeof(argument), "k"), nullptr, 0, 0);

    EXPECT_EQ(std::string(ctx.descriptor.output), "You removed the alias 'k'.\n\r");
    EXPECT_EQ(static_cast<alias_list*>(ctx.character.specials.alias), nullptr);
}

TEST(ActCommAlias, DoAliasRemovesHeadAliasLeavingRemainderIntact)
{
    AliasTestContext ctx;

    char argument[MAX_INPUT_LENGTH];
    do_alias(&ctx.character, mutable_argument(argument, sizeof(argument), "k kill"), nullptr, 0, 0);
    do_alias(&ctx.character, mutable_argument(argument, sizeof(argument), "gr greet"), nullptr, 0,
        0);

    // "gr" is the head (most recently added); remove it and confirm "k"
    // survives as the new head.
    reset_capturing_descriptor(ctx.descriptor, &ctx.character);
    do_alias(&ctx.character, mutable_argument(argument, sizeof(argument), "gr"), nullptr, 0, 0);

    EXPECT_EQ(std::string(ctx.descriptor.output), "You removed the alias 'gr'.\n\r");
    ASSERT_NE(static_cast<alias_list*>(ctx.character.specials.alias), nullptr);
    EXPECT_STREQ(ctx.character.specials.alias->keyword, "k");
    EXPECT_EQ(ctx.character.specials.alias->next, nullptr);
}

TEST(ActCommAlias, DoAliasRemoveOfNonexistentAliasReportsNoSuchAlias)
{
    AliasTestContext ctx;

    char argument[MAX_INPUT_LENGTH];
    do_alias(&ctx.character, mutable_argument(argument, sizeof(argument), "zzz"), nullptr, 0, 0);

    EXPECT_EQ(std::string(ctx.descriptor.output), "You have no such alias.\n\r");
}

TEST(ActCommAlias, DoAliasRejectsNewAliasOncePerLevelLimitReached)
{
    AliasTestContext ctx;
    // MAX_ALIAS = 30 + level*2; AliasTestContext sets level = 1, so the
    // limit is 32. Fill it exactly, then confirm the next add is rejected.
    char argument[MAX_INPUT_LENGTH];
    for (int index = 0; index < 32; ++index) {
        char keyword_and_command[32];
        std::snprintf(keyword_and_command, sizeof(keyword_and_command), "a%d x", index);
        do_alias(&ctx.character, mutable_argument(argument, sizeof(argument), keyword_and_command),
            nullptr, 0, 0);
    }

    reset_capturing_descriptor(ctx.descriptor, &ctx.character);
    do_alias(&ctx.character, mutable_argument(argument, sizeof(argument), "overflow x"), nullptr, 0,
        0);

    EXPECT_EQ(std::string(ctx.descriptor.output),
        "You reached the limit on alias number already.\n\r");
}

TEST(ActCommAlias, ReplaceAliasesExpandsMatchingKeywordPrefix)
{
    AliasTestContext ctx;

    char argument[MAX_INPUT_LENGTH];
    do_alias(&ctx.character, mutable_argument(argument, sizeof(argument), "k kill"), nullptr, 0, 0);

    char line[MAX_INPUT_LENGTH];
    mutable_argument(line, sizeof(line), "k orc");
    replace_aliases(&ctx.character, line);

    EXPECT_STREQ(line, "kill orc");
}

TEST(ActCommAlias, ReplaceAliasesLeavesNonMatchingLineUnchanged)
{
    AliasTestContext ctx;

    char argument[MAX_INPUT_LENGTH];
    do_alias(&ctx.character, mutable_argument(argument, sizeof(argument), "k kill"), nullptr, 0, 0);

    char line[MAX_INPUT_LENGTH];
    mutable_argument(line, sizeof(line), "look");
    replace_aliases(&ctx.character, line);

    EXPECT_STREQ(line, "look");
}

TEST(ActCommAlias, ReplaceAliasesLeavesLineUnchangedWhenNoAliasesDefined)
{
    AliasTestContext ctx;

    char line[MAX_INPUT_LENGTH];
    mutable_argument(line, sizeof(line), "k orc");
    replace_aliases(&ctx.character, line);

    EXPECT_STREQ(line, "k orc");
}

TEST(ActCommAlias, ReplaceAliasesDoesNotMatchKeywordThatIsOnlyAPrefixOfTheTypedWord)
{
    AliasTestContext ctx;

    char argument[MAX_INPUT_LENGTH];
    // "k" is a strict prefix of the typed word "kick" but replace_aliases()
    // requires the character right after the matched keyword to be
    // whitespace-or-end (`*(line + begin + tmp) <= ' '`), so "kick" must NOT
    // expand.
    do_alias(&ctx.character, mutable_argument(argument, sizeof(argument), "k kill"), nullptr, 0, 0);

    char line[MAX_INPUT_LENGTH];
    mutable_argument(line, sizeof(line), "kick orc");
    replace_aliases(&ctx.character, line);

    EXPECT_STREQ(line, "kick orc");
}
