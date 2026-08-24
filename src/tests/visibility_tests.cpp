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

// ---------------------------------------------------------------------------
// TASK-025: summon targets by player name regardless of the dark-room sight
// arm. summon's skills[] mask (consts.cpp) is TAR_CHAR_ROOM | TAR_CHAR_WORLD |
// TAR_DARK_OK -- the `tell` precedent (interpre.cpp's COMMANDO(19) row) --
// so target_from_word()'s TAR_CHAR_WORLD arm calls get_char_vis(ch, word,
// dark_ok=1) and CAN_SEE's light arm (the target-room-is-dark refusal,
// this file's CAN_SEE light_mode==0 block) is skipped. The repro pin below
// records AC#1's code-confirmed root cause: under the OLD mask (no
// TAR_DARK_OK) the SAME setup refuses, and the refusing arm is the dark
// TARGET room -- not caster-blind, not hiding, not invisibility, all of
// which are absent from this fixture and deliberately still refuse under
// (a)'s scope.
// ---------------------------------------------------------------------------

#include "../db.h"
#include "../handler.h"
#include "../spells.h"
#include "test_world.h"

// consts.cpp's global skill table -- not declared in any header (matching
// spell_registry_tests.cpp's own local extern).
extern struct skill_data skills[MAX_SKILLS];
// The process-global character list get_char_vis() walks (db.h declares it;
// act_wiz_format_tests.cpp uses the same push/restore idiom used here).
extern struct char_data* character_list;
// interpre.cpp's re-validation gate for an already-resolved target -- no
// shared header declares it (same local-extern convention as
// target_from_word above).
extern int target_check_one(struct char_data* ch, int mask, struct target_data* t1);

namespace {

// Caster in room 1, player victim in room 2 with the DARK room flag set and
// no light -- the exact "target cannot see" shape the owner reported.
// Construction publishes the victim at the head of character_list and
// darkens room 2; destruction restores both (fixture-hygiene rule: the
// monolithic single-process runner must never observe this state after the
// test ends). The victim is deliberately NOT chained into a room occupant
// list -- get_char_vis() walks character_list, and CAN_SEE() reads only
// room_of() on either side, so set_location() is the honest placement here.
struct DarkRoomSummonContext {
    ScopedTestWorld test_world { 3 };
    char_data caster {};
    char_data victim {};
    // Writable name storage for the victim (player.name is a char*).
    char victim_name[8] = "frodo";
    // Saved room-2 state restored on destruction.
    long saved_dark_room_flags;
    int saved_dark_room_light;
    // Saved character_list head restored on destruction.
    char_data* saved_character_list_head;

    DarkRoomSummonContext()
    {
        caster.player.race = RACE_HUMAN;
        caster.player.level = 30;
        caster.specials.position = POSITION_STANDING;
        caster.abs_number = 4301;

        victim.player.race = RACE_HUMAN;
        victim.player.level = 30;
        victim.player.name = victim_name;
        victim.specials.position = POSITION_STANDING;
        victim.abs_number = 4302;

        set_location(&caster, 1);
        set_location(&victim, 2);

        room_data* dark_room = room_by_id_total(2);
        saved_dark_room_flags = dark_room->room_flags;
        saved_dark_room_light = dark_room->light;
        dark_room->room_flags |= DARK;
        dark_room->light = 0;

        saved_character_list_head = character_list;
        victim.next = character_list;
        character_list = &victim;
    }

    ~DarkRoomSummonContext()
    {
        character_list = saved_character_list_head;
        room_data* dark_room = room_by_id_total(2);
        dark_room->room_flags = saved_dark_room_flags;
        dark_room->light = saved_dark_room_light;
    }
};

} // namespace

TEST(SummonTargeting, SummonsTargetMaskCarriesDarkOkAlongsideRoomAndWorld)
{
    EXPECT_EQ(skills[SPELL_SUMMON].targets, TAR_CHAR_ROOM | TAR_CHAR_WORLD | TAR_DARK_OK)
        << "Expected summon's consts.cpp mask to match tell's TAR_DARK_OK precedent so a "
           "name-targeted world spell is not refused by the dark-room sight arm.";
}

TEST(SummonTargeting, TargetFromWordUnderTheSummonMaskFindsAPlayerStandingInADarkRoom)
{
    DarkRoomSummonContext ctx;
    target_data target {};
    char argument[] = "frodo";

    char* remainder = target_from_word(&ctx.caster, argument, skills[SPELL_SUMMON].targets, &target);

    ASSERT_NE(remainder, nullptr)
        << "Expected the summon mask's TAR_DARK_OK to let get_char_vis() resolve a player "
           "standing in a dark room.";
    EXPECT_EQ(target.type, TARGET_CHAR);
    EXPECT_EQ(target.ptr.ch, &ctx.victim);
    EXPECT_EQ(target.choice, TAR_CHAR_WORLD);
}

// AC#1's repro pin: the pre-fix mask (TAR_CHAR_ROOM | TAR_CHAR_WORLD, the
// literal 6 consts.cpp carried) refuses the identical setup, and the only
// CAN_SEE arm this fixture can trip is the dark-target-room one. This test
// passes before AND after the fix -- it documents which arm fired, and pins
// that hiding/invisible/blind refusals (absent here) are not what (a) lifts.
TEST(SummonTargeting, TargetFromWordWithoutDarkOkStillRefusesADarkRoomTarget)
{
    DarkRoomSummonContext ctx;
    target_data target {};
    char argument[] = "frodo";

    char* remainder = target_from_word(&ctx.caster, argument, TAR_CHAR_ROOM | TAR_CHAR_WORLD, &target);

    EXPECT_EQ(remainder, nullptr)
        << "Expected the old dark_ok-less mask to keep refusing a dark-room target -- if this "
           "resolves, the repro no longer exercises the dark arm.";
    EXPECT_EQ(target.type, TARGET_NONE);
}

TEST(SummonTargeting, TargetCheckOneUnderTheSummonMaskAcceptsADarkRoomWorldTarget)
{
    DarkRoomSummonContext ctx;
    target_data target {};
    target.type = TARGET_CHAR;
    target.ptr.ch = &ctx.victim;
    target.ch_num = ctx.victim.abs_number;
    set_char_exists(ctx.victim.abs_number);

    int accepted = target_check_one(&ctx.caster, skills[SPELL_SUMMON].targets, &target);

    remove_char_exists(ctx.victim.abs_number);
    EXPECT_EQ(accepted, TAR_CHAR_WORLD)
        << "Expected the delayed-cast re-validation gate to accept a dark-room world target "
           "under summon's TAR_DARK_OK mask.";
}
