// visibility_tests.cpp

// New test TU (spell-family closure wave, Task 1; sf-task-1-brief.md;
// sf-census.md section 4.1). Coverage-gap rider: report_wrong_target()/
// target_from_word() (relocated verbatim from interpre.cpp into
// visibility.cpp this task) had ZERO prior test coverage anywhere in the
// tree -- neither function name appears in any existing tests/*.cpp file.
// These tests exercise a handful of deterministic branches of each
// (chosen to need no world[]/room state) so the relocated bodies are not
// merely moved untested code; they are not exhaustive branch coverage of
// either function.

#include "../comm.h"
#include "rots/core/character.h"
#include "rots/core/descriptor.h"
#include "rots/core/room.h"
#include "rots/core/types.h"

#include <gtest/gtest.h>

#include <cstring>

// Local extern declarations, matching every existing caller's own
// convention (neither function is declared in a shared header --
// interpre.h has none, see visibility.cpp's relocation comment).
extern void report_wrong_target(struct char_data* ch, int mask, char has_arg);
extern char* target_from_word(struct char_data* ch, char* argument, int mask, struct target_data* t1);

namespace {

// Same shape as comm_output_tests.cpp's ConnectedCharacterContext: a
// char_data wired to a descriptor that captures send_to_char() output
// without opening a network connection.
struct ConnectedCharacterContext {
    char_data character {};
    descriptor_data descriptor {};

    ConnectedCharacterContext()
    {
        descriptor.output = descriptor.small_outbuf;
        descriptor.small_outbuf[0] = '\0';
        descriptor.bufptr = 0;
        descriptor.bufspace = SMALL_BUFSIZE - 1;
        descriptor.connected = 0;
        descriptor.character = &character;
        character.desc = &descriptor;
        character.abs_number = 4207;
    }
};

} // namespace

TEST(ReportWrongTarget, TarTextAllReportsTheGodsMessageRegardlessOfHasArg)
{
    ConnectedCharacterContext ctx;

    report_wrong_target(&ctx.character, TAR_TEXT_ALL, 0);

    EXPECT_STREQ(ctx.descriptor.output, "Strange. Please report to gods what you just did (1).\n\r");
}

TEST(ReportWrongTarget, TarGoldWithAnArgumentReportsTheMoneyOnlyMessage)
{
    ConnectedCharacterContext ctx;

    report_wrong_target(&ctx.character, TAR_GOLD, 1);

    EXPECT_STREQ(ctx.descriptor.output, "You can do that with money only.\n\r");
}

TEST(ReportWrongTarget, UnmatchedMaskWithNoArgumentFallsBackToTheGenericMessage)
{
    ConnectedCharacterContext ctx;

    report_wrong_target(&ctx.character, 0, 0);

    EXPECT_STREQ(ctx.descriptor.output, "You can not do it this way.\n\r");
}

TEST(TargetFromWord, TarGoldParsesADigitPrefixedGoldTokenIntoCopperValue)
{
    char_data character {};
    target_data target {};
    char argument[] = "100gold";

    char* remainder = target_from_word(&character, argument, TAR_GOLD, &target);

    EXPECT_EQ(target.type, TARGET_GOLD);
    EXPECT_EQ(target.choice, TAR_GOLD);
    EXPECT_EQ(target.ch_num, 100 * COPP_IN_GOLD)
        << "Expected the parsed gold amount to convert to copper via COPP_IN_GOLD.";
    EXPECT_STREQ(remainder, "") << "Expected the whole token to be consumed.";
}

TEST(TargetFromWord, TarNoneOkWithAnEmptyArgumentLeavesTheTargetAtItsInitializedDefaults)
{
    char_data character {};
    target_data target {};
    char argument[] = "";

    char* remainder = target_from_word(&character, argument, TAR_NONE_OK, &target);

    EXPECT_EQ(target.type, TARGET_NONE);
    EXPECT_EQ(target.choice, TAR_IGNORE);
    EXPECT_EQ(remainder, argument)
        << "Expected an empty, TAR_NONE_OK-satisfied argument to return the original pointer "
           "unchanged.";
}
