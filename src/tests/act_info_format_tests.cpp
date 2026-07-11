#include "../db.h"
#include "../handler.h"
#include "../interpre.h"
#include "../rots_rng.h"
#include "../spells.h"
#include "../structs.h"
#include "../utils.h"
#include "test_platform_compat.h"
#include "test_world.h"

#include <gtest/gtest.h>

#include <cstring>
#include <format>
#include <string>

// Characterization tests for Phase 4 Wave 3 Task 1 (Chunk I1 -- act_info.cpp
// perception/do_look family: do_look, do_read, do_examine, do_exits,
// do_search, do_map, do_small_map, plus the room/object display helpers
// do_look shares -- list_obj_to_char/show_obj_to_char/show_char_to_char/
// list_char_to_char, reached via do_look's "look in"/"look ''" branches).
// Per the wave's binding pattern, these pin the CURRENT byte-for-byte output
// -- confirmed passing against the pre-conversion source -- before the
// chunk's sprintf/strcpy/strcat sites were converted to std::format/
// std::string composition, and green again after.
//
// Deliberately NOT unit-tested here (documented exclusions, not oversights):
//  - do_map / do_small_map: read as part of this chunk (act_info.cpp:3206,
//    :3275); neither contains a single sprintf/strcpy/strcat call (both are
//    pure symbol_to_map()/send_to_char() plumbing over pre-built world_map/
//    small_map buffers), so there is nothing in them for this task's
//    transform to touch and nothing to pin.
//  - do_look's case 8 ("look" with no argument) is pinned only at its
//    shallow fixture-reachable depth (name + exits line + description, via
//    the do_examine no-target delegation test below); the deeper rendering
//    -- the room-flags line's sprintf sites at act_info.cpp:1328/1332/1335
//    (PRF_ROOMFLAGS/PRF_ADVANCED_VIEW + sprintbit), weather/affection
//    lines, and show_obj_to_char's mode-0 sprintf at :336 reached through
//    list_obj_to_char(..., mode=0, ...) at :1438 -- needs a fully-populated
//    room, exactly the "deep room rendering" the task brief calls out as
//    covered by scripts/boot-golden.sh's real room output rather than a
//    fixture-driven unit test.
//  - do_look's case 7 ("look at <target>") and do_examine's own object path,
//    when the target has no ex_description match, route through
//    show_obj_to_char's mode 5, which calls call_trigger(ON_EXAMINE_OBJECT,
//    ...) -- safe with an empty script_table (this test binary loads no
//    world/script data), but exercising that mode is still one layer removed
//    from a plain unit fixture; do_examine's own "in %s" conversion site is
//    covered instead via the drink-container path below, which reaches
//    show_obj_to_char through the DRINKCON branch (call_trigger only, no
//    ex_description machinery) and is asserted byte-for-byte.
//  - diag_char_to_char (act_info.cpp:479) is textually inside the
//    100-1036 helper band the task brief points at, but its only caller
//    (do_diagnose, act_info.cpp:2986/2989) is outside this chunk's function
//    list (do_look/do_read/do_examine/do_exits/do_search/do_map/
//    do_small_map) -- not converted or tested by this task.

extern char buf[];

ACMD(do_look);
ACMD(do_read);
ACMD(do_examine);
ACMD(do_exits);
ACMD(do_search);

void clear_char(struct char_data* ch, int mode);

namespace {

// Mirrors act_format_tests.cpp's helper (Phase 4 Wave 2 Task 4): points a
// descriptor's output at its OWN small_outbuf so send_to_char()/act() output
// can be inspected directly instead of going to a real socket.
//
// CRITICAL: this mutates the caller's descriptor_data in place and must NEVER
// be replaced by a version that returns a descriptor_data by value.
// descriptor_data::output is a self-pointer into the same object's
// small_outbuf[]; copying/moving a descriptor_data (across a `return`, an
// `x = f()`, or a member-initializer) copies that pointer bytewise and leaves
// `output` aimed at the SOURCE object's buffer -- a dangling pointer once the
// source (a returned temporary) is destroyed. On MSVC's Debug config (NRVO
// disabled) that dangling write_to_output() target produced empty/garbage
// output, cross-descriptor bleed (a victim's message surfacing in the actor's
// buffer), and an eventual SEH 0xc0000005 access violation; Linux/macOS masked
// it via copy elision. Always declare the descriptor, then reset it in place.
void reset_capturing_descriptor(descriptor_data& descriptor, char_data* character)
{
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = 0; // CON_PLAYING
    descriptor.character = character;
}

// Saves, overrides, and restores the process-global weather_info.sunlight
// (utils.h) around a test -- IS_SUNLIT_EXIT/SUN_PENALTY read it, and the
// monolithic runner shares one weather_info across every suite, so a test
// that stamps SUN_LIGHT must put the prior value back.
struct ScopedSunlight {
    // The sunlight value in effect before this guard, restored on scope exit.
    int saved_sunlight;

    explicit ScopedSunlight(int sunlight)
        : saved_sunlight(weather_info.sunlight)
    {
        weather_info.sunlight = sunlight;
    }

    ~ScopedSunlight()
    {
        weather_info.sunlight = saved_sunlight;
    }
};

// A single standing PC in room 0 of a fresh single-room test world, with no
// exits configured -- covers do_exits's "no exits" branch and do_search's
// "no passage" replies (both only need ch->in_room to resolve to a real
// room, not NOWHERE, which SoloCharacterContext-style fixtures leave unset).
// A local `knowledge` array is wired up so do_search's GET_SKILL(ch,
// SKILL_SEARCH) lookup (utils.h) reads real bytes instead of the
// `ch->knowledge == nullptr` fallback (utils.h's 80-and-no-confuse-modifier
// default), matching char_utils_tests.cpp's convention for setting a
// deterministic skill value.
struct RoomCharacterContext {
    ScopedTestWorld test_world;
    char_data character {};
    descriptor_data descriptor {};
    byte knowledge[MAX_SKILLS] {};
    char_data* original_people = nullptr;

    RoomCharacterContext()
    {
        clear_char(&character, MOB_VOID);
        reset_capturing_descriptor(descriptor, &character);
        // do_look (and, transitively, do_read/do_examine) bails out at its
        // very first line unless ch->desc->descriptor is non-zero (a real
        // socket fd in production); reset_capturing_descriptor doesn't set
        // this field (act_othe.cpp's send_to_char()/act() paths never check
        // it), so this chunk's own fixtures must, same as
        // interpre_account_menu_tests.cpp's descriptor fixtures do.
        descriptor.descriptor = 7;
        character.knowledge = knowledge;

        original_people = test_world.room().people;
        character.in_room = 0;
        character.next_in_room = nullptr;
        test_world.room().people = &character;

        character.specials.position = POSITION_STANDING;
        character.player.race = RACE_HUMAN;
        character.desc = &descriptor;
    }

    ~RoomCharacterContext()
    {
        test_world.room().people = original_people;
        character.in_room = NOWHERE;
    }
};

// RoomCharacterContext plus one configured NORTH exit, mirroring
// act_format_tests.cpp's DoorContext -- but with `to_room` pointed at room 0
// itself (rather than left NOWHERE/0-via-value-init and never read), so
// do_exits's/do_search's world[EXIT(ch,door)->to_room] lookups resolve to a
// real, already-populated room ("The Testing Meadow", set by
// ScopedTestWorld's constructor) without needing a second room.
struct RoomWithExitContext {
    static constexpr int door_direction = 0; // NORTH

    ScopedTestWorld test_world;
    char_data character {};
    descriptor_data descriptor {};
    room_direction_data exit {};
    byte knowledge[MAX_SKILLS] {};
    char_data* original_people = nullptr;

    RoomWithExitContext()
    {
        clear_char(&character, MOB_VOID);
        reset_capturing_descriptor(descriptor, &character);
        // See RoomCharacterContext's comment: do_look requires a non-zero fd.
        descriptor.descriptor = 7;
        character.knowledge = knowledge;

        original_people = test_world.room().people;
        character.in_room = 0;
        character.next_in_room = nullptr;
        test_world.room().people = &character;
        test_world.room().dir_option[door_direction] = &exit;
        exit.to_room = 0;

        // The do_look SCMD_LOOK_EXAM delegation path (do_examine with no
        // target) renders the room, which ends in show_blood_trail() reading
        // room 0's bleed_track[] -- neither room_data's constructor nor
        // dummy_room_data() initializes that array, so zero it here to keep
        // the "no blood trails" branch (char_number == 0) deterministic.
        std::memset(&test_world.room().bleed_track, 0, sizeof(test_world.room().bleed_track));

        character.specials.position = POSITION_STANDING;
        character.player.race = RACE_HUMAN;
        character.player.level = 1;
        character.desc = &descriptor;
    }

    ~RoomWithExitContext()
    {
        test_world.room().people = original_people;
        test_world.room().dir_option[door_direction] = nullptr;
        // Tests set DARK on the shared room 0 (do_exits's "Too dark to
        // tell" branch); clear it so later suites in the monolithic runner
        // see the flag state ScopedTestWorld's reuse branch assumes.
        test_world.room().room_flags = 0;
        character.in_room = NOWHERE;
    }
};

// RoomWithExitContext's two-room sibling for "look <direction>" into a
// DIFFERENT room: the dark-exit branch needs the actor's own room lit (or
// do_look bails out at its "It is pitch black..." gate) while the exit's
// target room is dark -- impossible with a self-loop exit. Room 1 is
// stamped with a known name (mirroring ScopedTestWorld's own free-then-
// str_dup room-0 pattern, since the reuse branch guarantees nothing about
// room 1's contents) and explicitly cleared flags/light/sector so
// IS_DARK(1) is driven only by the DARK bit a test chooses to set.
// exit.general_description is a non-null empty string: unlike do_exits,
// do_look's direction cases dereference it unconditionally (real world
// data always allocates it; a default-constructed fixture exit does not).
struct TwoRoomLookContext {
    static constexpr int door_direction = 0; // NORTH

    ScopedTestWorld test_world { 2 };
    char_data character {};
    descriptor_data descriptor {};
    room_direction_data exit {};
    byte knowledge[MAX_SKILLS] {};
    char_data* original_people = nullptr;

    TwoRoomLookContext()
    {
        clear_char(&character, MOB_VOID);
        reset_capturing_descriptor(descriptor, &character);
        // See RoomCharacterContext's comment: do_look requires a non-zero fd.
        descriptor.descriptor = 7;
        character.knowledge = knowledge;

        original_people = test_world.room().people;
        character.in_room = 0;
        character.next_in_room = nullptr;
        test_world.room().people = &character;
        test_world.room().dir_option[door_direction] = &exit;
        exit.to_room = 1;
        exit.general_description = const_cast<char*>("");

        std::free(world[1].name);
        world[1].name = str_dup("A Northern Clearing");
        world[1].room_flags = 0;
        world[1].light = 0;
        world[1].sector_type = 0; // SECT_INSIDE: IS_DARK() then keys off DARK only

        character.specials.position = POSITION_STANDING;
        character.player.race = RACE_HUMAN;
        character.player.level = 1;
        character.desc = &descriptor;
    }

    ~TwoRoomLookContext()
    {
        world[1].room_flags = 0;
        test_world.room().people = original_people;
        test_world.room().dir_option[door_direction] = nullptr;
        character.in_room = NOWHERE;
    }
};

// Two ordinary PCs sharing room 0 -- for do_search's act(..., TO_ROOM)
// announcement, which a bystander (not the searching actor) receives.
// Mirrors act_format_tests.cpp's RoomPairContext; PRF_HOLYLIGHT on the
// bystander bypasses CAN_SEE()'s darkness gate the same way, since this
// fixture has no light-source/room-light bookkeeping to model.
struct RoomWithBystanderContext {
    ScopedTestWorld test_world;
    char_data actor {};
    char_data bystander {};
    descriptor_data actor_descriptor {};
    descriptor_data bystander_descriptor {};
    byte knowledge[MAX_SKILLS] {};
    char_data* original_people = nullptr;

    RoomWithBystanderContext()
    {
        clear_char(&actor, MOB_VOID);
        clear_char(&bystander, MOB_VOID);
        reset_capturing_descriptor(actor_descriptor, &actor);
        reset_capturing_descriptor(bystander_descriptor, &bystander);
        actor_descriptor.descriptor = 7;
        bystander_descriptor.descriptor = 7;
        actor.knowledge = knowledge;

        original_people = test_world.room().people;
        actor.in_room = 0;
        bystander.in_room = 0;
        actor.next_in_room = &bystander;
        bystander.next_in_room = nullptr;
        test_world.room().people = &actor;

        actor.specials.position = POSITION_STANDING;
        bystander.specials.position = POSITION_STANDING;
        actor.player.race = RACE_HUMAN;
        bystander.player.race = RACE_HUMAN;
        SET_BIT(bystander.specials2.pref, PRF_HOLYLIGHT);

        actor.desc = &actor_descriptor;
        bystander.desc = &bystander_descriptor;
    }

    ~RoomWithBystanderContext()
    {
        test_world.room().people = original_people;
        actor.next_in_room = nullptr;
        bystander.next_in_room = nullptr;
        actor.in_room = NOWHERE;
        bystander.in_room = NOWHERE;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// do_exits (act_info.cpp:1518)
// ---------------------------------------------------------------------------

// do_exits: a standing PC in room 0 with one closed, non-hidden north exit
// pins the "%-7s - Closed %s\n\r" line (act_info.cpp do_exits) byte-for-byte,
// including the width-7 left-justified direction column.
TEST(ActInfoPerception, DoExitsFormatsClosedDoorLineWithKeyword)
{
    RoomWithExitContext context;
    context.exit.keyword = const_cast<char*>("gate");
    context.exit.exit_info = EX_ISDOOR | EX_CLOSED;

    do_exits(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_STREQ(context.descriptor.output, "Obvious exits:\n\rNorth   - Closed gate\n\r");
}

// Regression pin for the null-guard idiom (utils.h's nz()): a direction-only
// exit's keyword can be null (find_door, act_move.cpp, already treats a null
// keyword as "no keyword required"). RoomWithExitContext's exit is
// value-initialized ({}), so leaving keyword unset here reproduces that null
// state directly. Old sprintf("%s", NULL) printed glibc's literal "(null)";
// nz() must reproduce that exact string so std::format doesn't call
// strlen(nullptr) instead.
TEST(ActInfoPerception, DoExitsFormatsClosedDoorLineWithNullKeywordAsGlibcNullLiteral)
{
    RoomWithExitContext context;
    ASSERT_EQ(context.exit.keyword, nullptr);
    context.exit.exit_info = EX_ISDOOR | EX_CLOSED;

    do_exits(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_STREQ(context.descriptor.output, "Obvious exits:\n\rNorth   - Closed (null)\n\r");
}

// do_exits: a hidden, closed door is only reported to a god-level (>=
// LEVEL_GOD) viewer, with the "*Hidden*" marker line -- pins the second
// EXIT(ch,door)->keyword conversion site (act_info.cpp:1561).
TEST(ActInfoPerception, DoExitsFormatsHiddenDoorLineForImmortalGod)
{
    RoomWithExitContext context;
    context.character.player.level = LEVEL_GOD;
    context.exit.keyword = const_cast<char*>("trapdoor");
    context.exit.exit_info = EX_ISDOOR | EX_CLOSED | EX_ISHIDDEN;

    do_exits(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_STREQ(context.descriptor.output, "Obvious exits:\n\rNorth   - *Hidden* trapdoor\n\r");
}

// do_exits: an immortal (>= LEVEL_IMMORT) viewing an open exit sees the
// destination room's vnum and exit_width -- pins the
// "%-7s - [%7d][w:%2d] %s\n\r" conversion (act_info.cpp:1533), including the
// {:>7}/{:>2} width conversions.
TEST(ActInfoPerception, DoExitsFormatsOpenExitForImmortalWithRoomNumberAndWidth)
{
    RoomWithExitContext context;
    context.character.player.level = LEVEL_IMMORT;
    context.exit.exit_width = 4;
    context.test_world.room().number = 3001;

    do_exits(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_STREQ(context.descriptor.output,
        "Obvious exits:\n\rNorth   - [   3001][w: 4] The Testing Meadow\n\r");
}

// do_exits: a mortal (below LEVEL_IMMORT) viewing an open exit whose target
// room is visible (CAN_SEE holds after do_exits temporarily re-homes the
// character into the target room) sees the plain direction + room-name line
// -- pins the "%-7s - %s\n\r" conversion (the mortal sibling of the
// immortal [vnum][width] line above), including nz() on the room name.
TEST(ActInfoPerception, DoExitsFormatsOpenExitRoomNameForMortal)
{
    RoomWithExitContext context;

    do_exits(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_STREQ(context.descriptor.output, "Obvious exits:\n\rNorth   - The Testing Meadow\n\r");
}

// do_exits: the same mortal open-exit walk when the target room is dark
// (DARK room flag; the fixture's zero `light` count does the rest per
// utils.h's IS_DARK) and the viewer has neither PRF_HOLYLIGHT nor infravision
// -- pins the "%-7s - Too dark to tell\n\r" conversion. The fixture's
// destructor clears the DARK bit off the shared room 0 afterwards.
TEST(ActInfoPerception, DoExitsFormatsTooDarkLineForMortalWhenTargetRoomIsDark)
{
    RoomWithExitContext context;
    context.test_world.room().room_flags = DARK;

    do_exits(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_STREQ(context.descriptor.output, "Obvious exits:\n\rNorth   - Too dark to tell\n\r");
}

// do_exits: an orc viewing an open exit into a sunlit room under SUN_LIGHT
// takes the sun_exits[] ("#North#") direction column instead of exits[]
// ("North") -- pins the sunlit-marker variant of the open-exit line
// (IS_SUNLIT_EXIT needs the exit open, the target room un-DARK/un-SHADOWY/
// un-INDOORS, and weather_info.sunlight == SUN_LIGHT, guarded/restored by
// ScopedSunlight). "#North#" is exactly 7 characters, so the {:<7} column
// adds no padding here -- the one direction whose sunlit marker consumes
// the entire field width.
TEST(ActInfoPerception, DoExitsFormatsSunlitExitMarkerForOrcUnderSunlight)
{
    RoomWithExitContext context;
    context.character.player.race = RACE_ORC;
    ScopedSunlight sunlight(SUN_LIGHT);

    do_exits(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_STREQ(context.descriptor.output, "Obvious exits:\n\r#North# - The Testing Meadow\n\r");
}

// do_exits: no exits configured at all -- pins the "buf stays empty" branch
// surviving the accumulation-to-std::string conversion (act_info.cpp's
// `if (*buf) ... else send_to_char(" None.\n\r", ch);`), a pure-literal
// regression guard for the surrounding restructure rather than a format
// conversion in its own right.
TEST(ActInfoPerception, DoExitsSendsNoneLineWhenNoExitsConfigured)
{
    RoomCharacterContext context;

    do_exits(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_STREQ(context.descriptor.output, "Obvious exits:\n\r None.\n\r");
}

// ---------------------------------------------------------------------------
// do_search (act_info.cpp:3292)
// ---------------------------------------------------------------------------

// do_search case 1 (post-WAIT_STATE_FULL callback): searching a direction
// with no configured exit pins the "There is no passage %s.\n\r" conversion
// (act_info.cpp:3352). Skill is forced to 200 (knowledge[SKILL_SEARCH]) so
// `skill > number(0, 99)` always succeeds -- number(0,99)'s max draw is 99 --
// keeping the test deterministic without seeding rots_rng.
TEST(ActInfoPerception, DoSearchCase1FormatsNoPassageMessageWhenExitMissing)
{
    RoomCharacterContext context;
    context.knowledge[SKILL_SEARCH] = 200;

    waiting_type wtl {};
    wtl.flg = 0; // NORTH

    do_search(&context.character, const_cast<char*>(""), &wtl, 0, 1);

    EXPECT_STREQ(context.descriptor.output, "There is no passage north.\n\r");
}

// do_search case 1: an exit with a keyword containing an apostrophe pins the
// "You found %s at %s.\n" conversion (act_info.cpp:3355) -- note the
// original literal's trailing "\n" only (no "\r"), preserved verbatim.
TEST(ActInfoPerception, DoSearchCase1FormatsFoundKeywordMessageWithApostrophe)
{
    RoomWithExitContext context;
    context.knowledge[SKILL_SEARCH] = 200;
    context.exit.keyword = const_cast<char*>("O'Rourke's Gate");
    context.exit.exit_info = EX_ISDOOR;

    waiting_type wtl {};
    wtl.flg = RoomWithExitContext::door_direction;

    do_search(&context.character, const_cast<char*>(""), &wtl, 0, 1);

    EXPECT_STREQ(context.descriptor.output, "You found O'Rourke's Gate at the north.\n");
}

// do_search case 1: an exit with a null keyword pins the "no name, please
// notify immortals" fallback conversion (act_info.cpp:3357/3358).
TEST(ActInfoPerception, DoSearchCase1FormatsUnnamedExitMessageWhenKeywordIsNull)
{
    RoomWithExitContext context;
    context.knowledge[SKILL_SEARCH] = 200;
    ASSERT_EQ(context.exit.keyword, nullptr);
    context.exit.exit_info = EX_ISDOOR;

    waiting_type wtl {};
    wtl.flg = RoomWithExitContext::door_direction;

    do_search(&context.character, const_cast<char*>(""), &wtl, 0, 1);

    EXPECT_STREQ(context.descriptor.output,
        "The exit north has no name, please notify immortals.\n\r");
}

// do_search case 1's opening act(..., TO_ROOM) announcement -- pins the
// "$n searches for something to %s." conversion (act_info.cpp:3338),
// delivered before the ex/skill checks run (so no exit needs to be
// configured). Uses a substring match (like act_format_tests.cpp's
// DoOrderFormatsMessageToVictimWithApostrophe) since act() capitalizes/
// substitutes $n from PERS(), whose exact bytes aren't this conversion
// site's concern.
TEST(ActInfoPerception, DoSearchCase1SendsSearchAnnouncementToRoomBystander)
{
    RoomWithBystanderContext context;
    context.knowledge[SKILL_SEARCH] = 200;

    waiting_type wtl {};
    wtl.flg = 0; // NORTH

    do_search(&context.actor, const_cast<char*>(""), &wtl, 0, 1);

    const std::string output(context.bystander_descriptor.output);
    EXPECT_NE(output.find("searches for something to the north."), std::string::npos)
        << "actual output: " << output;
}

// ---------------------------------------------------------------------------
// do_look (act_info.cpp:1037) -- cases 0-5, "look <direction>"
// ---------------------------------------------------------------------------

// "examine <direction>" (SCMD_LOOK_EXAM) for an orc under SUN_LIGHT: the
// sun-penalty early return pins both the "To the %s you see:\n\r" header
// conversion AND the bare "%s\n\r" room-name conversion (plus the literal
// light-penalty line) without entering the recursive full-room do_look the
// non-penalized path takes. The exit self-loops to room 0, whose flags stay
// clear, so SUN_PENALTY(ch) (orc + OUTSIDE + SUN_LIGHT) holds after do_look
// re-homes the character into the target room.
TEST(ActInfoPerception, DoLookDirectionExamFormatsHeaderAndRoomNameUnderSunPenalty)
{
    RoomWithExitContext context;
    context.character.player.race = RACE_ORC;
    context.exit.general_description = const_cast<char*>("");
    ScopedSunlight sunlight(SUN_LIGHT);

    do_look(&context.character, const_cast<char*>("north"), nullptr, 0, SCMD_LOOK_EXAM);

    EXPECT_STREQ(context.descriptor.output,
        "To the north you see:\n\r"
        "The Testing Meadow\n\r"
        "The power of light makes it hard to see.\n\r");
}

// "look <direction>" (subcmd 0) into a dark target room: the actor's own
// room stays lit (or do_look's !CAN_SEE gate would fire first), room 1
// carries the DARK flag, and the viewer has no PRF_HOLYLIGHT -- pins the
// "It's too dark to the %s to see anything.\n\r" conversion. The exit's
// empty (non-null) general_description routes past the exit-description
// branch into the to_room rendering, and the trailing door-state block
// appends nothing (no door flags, null keyword).
TEST(ActInfoPerception, DoLookDirectionFormatsTooDarkMessageWhenTargetRoomIsDark)
{
    TwoRoomLookContext context;
    world[1].room_flags = DARK;

    do_look(&context.character, const_cast<char*>("north"), nullptr, 0, 0);

    EXPECT_STREQ(context.descriptor.output, "It's too dark to the north to see anything.\n\r");
}

// "look <direction>" (subcmd 0) into a visible target room -- pins the
// "To the %s you see %s.\n\r" conversion, including nz() on the target
// room's name.
TEST(ActInfoPerception, DoLookDirectionFormatsTargetRoomNameWhenVisible)
{
    TwoRoomLookContext context;

    do_look(&context.character, const_cast<char*>("north"), nullptr, 0, 0);

    EXPECT_STREQ(context.descriptor.output, "To the north you see A Northern Clearing.\n\r");
}

// ---------------------------------------------------------------------------
// do_look (act_info.cpp:1037) -- case 6, "look in <container>"
// ---------------------------------------------------------------------------

// do_look "look in <drink container>": a partially-full waterskin pins the
// "It's %sfull of a %s liquid.\n\r" conversion (act_info.cpp:1176), including
// the fullness[]/color_liquid[] table-lookup composition.
TEST(ActInfoPerception, DoLookInDrinkContainerFormatsFullnessAndLiquidColor)
{
    RoomCharacterContext context;

    obj_data waterskin {};
    waterskin.name = const_cast<char*>("waterskin");
    waterskin.short_description = const_cast<char*>("a waterskin");
    waterskin.obj_flags.type_flag = ITEM_DRINKCON;
    waterskin.obj_flags.value[0] = 4; // max content
    waterskin.obj_flags.value[1] = 2; // current content -> (2*3)/4 == 1 -> "about half "
    waterskin.obj_flags.value[2] = 0; // color_liquid[0] == "clear"
    waterskin.next_content = context.character.carrying;
    context.character.carrying = &waterskin;

    do_look(&context.character, const_cast<char*>("in waterskin"), nullptr, 0, 0);

    EXPECT_STREQ(context.descriptor.output, "It's about half full of a clear liquid.\n\r");
}

// do_look "look in <drink container>": a container whose max_content is zero
// pins the (literal, no format specifiers) "beware!" branch
// (act_info.cpp:1179) surviving the transform.
TEST(ActInfoPerception, DoLookInDrinkContainerFormatsMaxContentZeroWarning)
{
    RoomCharacterContext context;

    obj_data waterskin {};
    waterskin.name = const_cast<char*>("waterskin");
    waterskin.short_description = const_cast<char*>("a waterskin");
    waterskin.obj_flags.type_flag = ITEM_DRINKCON;
    waterskin.obj_flags.value[0] = 0; // max content zero
    waterskin.obj_flags.value[1] = 1; // (irrelevant: value[0] falsy takes the other branch)
    waterskin.next_content = context.character.carrying;
    context.character.carrying = &waterskin;

    do_look(&context.character, const_cast<char*>("in waterskin"), nullptr, 0, 0);

    EXPECT_STREQ(context.descriptor.output, "It's max_content is zero, beware!\n\r");
}

// do_look "look in <container>" with one item inside pins show_obj_to_char's
// shared mode-1/2/3/4 short_description conversion (act_info.cpp:338),
// reached here via list_obj_to_char(contains, ch, 2, true).
TEST(ActInfoPerception, DoLookInContainerListsContentsUsingShortDescription)
{
    RoomCharacterContext context;

    obj_data pebble {};
    pebble.short_description = const_cast<char*>("a small pebble");

    obj_data pouch {};
    pouch.name = const_cast<char*>("pouch");
    pouch.short_description = const_cast<char*>("a leather pouch");
    pouch.obj_flags.type_flag = ITEM_CONTAINER;
    pouch.contains = &pebble;
    pouch.next_content = context.character.carrying;
    context.character.carrying = &pouch;

    do_look(&context.character, const_cast<char*>("in pouch"), nullptr, 0, 0);

    EXPECT_STREQ(context.descriptor.output, "pouch (carried) : \n\ra small pebble\n\r");
}

// ---------------------------------------------------------------------------
// do_read (act_info.cpp:1470) / do_examine (act_info.cpp:1485)
// ---------------------------------------------------------------------------

// do_read composes its "at %s" argument (act_info.cpp:1477) before
// delegating to do_look; with no matching object/character/keyword in the
// room, do_look's case-7 "look at" falls through to the plain
// "You do not see that here.\n\r" literal. That literal endpoint still pins
// the sprintf site: had the composed argument lost its separating space (a
// real risk of a std::format typo, e.g. "at{}" instead of "at {}"),
// argument_split_2's word-split inside do_look would see a single garbled
// token instead of the keyword "at" plus a target, and do_look would take
// the entirely different `keyword_no == -1` "Sorry, I didn't understand
// that!\n\r" branch instead -- so this assertion distinguishes a correct
// "at sword" composition from a broken one.
TEST(ActInfoPerception, DoReadComposesAtPrefixedArgumentForDoLookDelegate)
{
    RoomCharacterContext context;
    context.character.specials.position = POSITION_STANDING;

    do_read(&context.character, const_cast<char*>("sword"), nullptr, 0, 0);

    EXPECT_STREQ(context.descriptor.output, "You do not see that here.\n\r");
}

// do_examine on a carried drink container pins the "in %s" conversion
// (act_info.cpp:1512) that composes do_examine's own delegate call to
// do_look's "look in" case -- exercised end-to-end: do_examine's own
// do_look(ch, argument, ...) call first reports the container's generic
// "It looks like a drink container.\n\r" description (show_obj_to_char mode
// 5, via call_trigger(ON_EXAMINE_OBJECT, ...) -- a safe no-op here since this
// test binary loads no script_table entries), then do_examine's "When you
// look inside..." literal, then the delegated do_look("in waterskin") call
// re-enters the fullness-message conversion pinned above.
TEST(ActInfoPerception, DoExamineContainerDelegatesInPrefixedLookMessageForDrinkContainer)
{
    RoomCharacterContext context;

    obj_data waterskin {};
    waterskin.name = const_cast<char*>("waterskin");
    waterskin.short_description = const_cast<char*>("a waterskin");
    waterskin.obj_flags.type_flag = ITEM_DRINKCON;
    waterskin.obj_flags.value[0] = 4;
    waterskin.obj_flags.value[1] = 4; // full -> (4*3)/4 == 3 -> fullness[3] == ""
    waterskin.obj_flags.value[2] = 0; // "clear"
    waterskin.next_content = context.character.carrying;
    context.character.carrying = &waterskin;

    do_examine(&context.character, const_cast<char*>("waterskin"), nullptr, 0, 0);

    EXPECT_STREQ(context.descriptor.output,
        "It looks like a drink container.\n\r"
        "When you look inside, you see:\n\r"
        "It's full of a clear liquid.\n\r");
}

// do_examine with NO target delegates to do_look(ch, "", wtl, CMD_LOOK,
// SCMD_LOOK_EXAM) with PRF_SPAM force-toggled on, which renders the full
// room (do_look case 8) -- pinning the case-8 composition this fixture can
// reach: room name + the "    Exits are:" line with the plain " N" exit
// mark (exit_mark[1], the unconverted runtime-format-table site) + the
// PRF_SPAM-gated room description. No room flags/colors/objects/other
// characters are configured, sunlight stays at the fixture default, and
// bleed_track is zeroed by the fixture, so nothing else contributes bytes.
// (The deeper case-8 variants -- PRF_ROOMFLAGS/PRF_ADVANCED_VIEW headers,
// weather, affections -- remain boot-golden territory per the task brief.)
TEST(ActInfoPerception, DoExamineWithoutTargetRendersRoomViaLookExamDelegation)
{
    RoomWithExitContext context;
    ASSERT_FALSE(PRF_FLAGGED(&context.character, PRF_SPAM));

    do_examine(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_STREQ(context.descriptor.output,
        "The Testing Meadow    Exits are: N\n\r"
        "A quiet room used for account-menu tests.\n\r");
    // do_examine restores the toggled PRF_SPAM bit on its way out.
    EXPECT_FALSE(PRF_FLAGGED(&context.character, PRF_SPAM));
}
