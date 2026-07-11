#include "../big_brother.h"
#include "../db.h"
#include "../handler.h"
#include "../interpre.h"
#include "../rots_rng.h"
#include "../skill_timer.h"
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
//
// Characterization tests for Phase 4 Wave 3 Task 2 (Chunk I2 -- act_info.cpp
// self-status/do_score family: do_score, do_info, do_toggle, do_affections,
// do_gen_ps's SCMD_WHOAMI branch). Suite ActInfoSelfStatus below. Same
// binding pattern as Task 1: these pin CURRENT byte-for-byte output --
// confirmed passing against the pre-conversion source -- before the
// bufpt-accumulation/strcpy+strcat sites in this chunk convert to
// std::string/std::format composition, and green again after.
//
// Deliberately NOT unit-tested here (documented exclusions, not oversights):
//  - do_squareroot (act_info.cpp:1879): pure integer/table arithmetic, no
//    sprintf/strcpy/strcat/send_to_char call at all -- nothing for this
//    task's transform to touch.
//  - do_inventory / do_equipment (act_info.cpp:2522/2528): each is a single
//    literal send_to_char() plus a delegate call (list_obj_to_char /
//    show_equipment_to_char, both defined elsewhere and out of this chunk) --
//    no format site of their own.
//  - do_diagnose (act_info.cpp:3019): no sprintf/strcpy of its own; its only
//    formatting happens inside diag_char_to_char (act_info.cpp:479), which
//    Task 1's exclusion note already places outside this chunk's function
//    list (still not do_diagnose's own code, and still not converted here).
//  - do_orc_delay (act_info.cpp:3444): every branch is either a literal
//    send_to_char() or a commented-out WAIT_STATE_FULL/REMOVE_BIT -- no
//    format site.
//  - do_gen_ps's non-WHOAMI branches (CREDITS/NEWS/INFO/WIZLIST/IMMLIST/
//    HANDBOOK/POLICIES/CLEAR/VERSION): each pages or sends a literal/extern
//    string buffer with no sprintf of its own; only SCMD_WHOAMI
//    (strcat(strcpy(buf, GET_NAME(ch)), "\n\r")) has a conversion site, and
//    is pinned below.
//  - do_score's weapon-master interop (player_spec::weapon_master_handler::
//    append_score_message, warrior_spec_handlers.h/weapon_master_handler.cpp)
//    is a legacy helper out of this chunk's file list; its char* signature is
//    kept as-is per the transform idiom catalog (a local `char stage[...]`
//    staging buffer bridges it into the new std::string accumulation) and is
//    exercised (not modified) by the weapon-master score-message test below.

extern char buf[];

ACMD(do_look);
ACMD(do_read);
ACMD(do_examine);
ACMD(do_exits);
ACMD(do_search);
ACMD(do_score);
ACMD(do_info);
ACMD(do_toggle);
ACMD(do_affections);
ACMD(do_gen_ps);
ACMD(do_time);
ACMD(do_who);
ACMD(do_levels);
ACMD(do_consider);
ACMD(do_rank);

void clear_char(struct char_data* ch, int mode);

// Process globals ActInfoWorldSocial's fixtures stamp directly (db.cpp/
// comm.cpp definitions; none of these are declared in a shared header, same
// as this file's existing `extern char buf[];` below).
extern struct time_info_data time_info;
extern struct player_index_element* player_table;
extern int top_of_p_table;
extern struct descriptor_data* descriptor_list;

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

// A single awake PC, connected to a capturing descriptor, with no world
// dependency -- mirrors act_format_tests.cpp's SoloCharacterContext (Phase 4
// Wave 2 Task 4), duplicated per-file rather than shared across translation
// units (each test file in this suite defines its own copy of
// reset_capturing_descriptor/SoloCharacterContext, same convention as the
// fixtures above). Covers every ActInfoSelfStatus site reached only through
// send_to_char()/act(..., TO_CHAR), where no room/light bootstrap is needed
// -- e.g. do_score and do_toggle, neither of which touches ch->in_room.
//
// One deliberate deviation from the act_format_tests.cpp original: strength
// is set non-zero. do_score's derived-stat line walks get_real_OB()/
// get_real_dodge(), whose get_skill_penalty()/get_dodge_penalty()
// (char_utils.cpp) divide by get_bal_strength() -- 0 for a fully-zeroed
// character. x86 traps that integer division as SIGFPE ("Exception:
// Numerical" in the i386 container's ctest) while ARM silently yields 0, so
// the suite passed on macOS and crashed on i386 until this was set. Real
// characters always have non-zero strength; none of this suite's pinned
// bytes include a derived combat stat, so the value itself is arbitrary.
struct SoloCharacterContext {
    char_data character {};
    descriptor_data descriptor {};

    SoloCharacterContext()
    {
        clear_char(&character, MOB_VOID);
        reset_capturing_descriptor(descriptor, &character);
        character.desc = &descriptor;
        character.specials.position = POSITION_STANDING;
        character.tmpabilities.str = 100; // see fixture comment: div-by-zero guard
    }
};

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

// Ensures game_timer::skill_timer's AND game_rules::big_brother's
// process-wide singletons (skill_timer.h / big_brother.h) are constructed
// before a test exercises do_affections/do_info -- both call
// report_skill_timer() then game_rules::big_brother::instance()
// (act_info.cpp), unconditionally, back to back. Production reaches these
// via boot-time skill_timer::create()/big_brother::create() calls that this
// unit test binary's main() never runs; without them, instance()
// (singleton.h's world_singleton<T>) returns `*m_pInstance` while
// m_pInstance is still null -- a guaranteed null-pointer-dereference SIGSEGV
// the very first time ANY suite in this process calls do_affections/do_info
// (confirmed by bisection: no pre-existing suite touched either path, so
// nothing had hit this before). Each create()'s storage is a function-local
// static, so repeat calls across multiple tests in the monolithic runner are
// idempotent -- they just re-point the pointer at the same
// already-constructed instance; this is NOT modeling boot's real
// weather_info/world wiring (do_affections's own reads -- m_skill_timer,
// is_target_looting()'s corpse map -- never touch get_weather()/
// get_world()), just satisfying instance()'s non-null precondition.
void ensure_skill_timer_created()
{
    game_timer::skill_timer::create(weather_info, nullptr);
    game_rules::big_brother::create(weather_info, nullptr);
}

// Saves/restores the process-global `time_info` calendar fields plus the
// two `weather_info` fields do_time reads (moonlight/moonphase) -- the
// monolithic runner shares one process, so a test that stamps a specific
// year/month/day/hour must put the prior values back (same hygiene as
// ScopedSunlight above).
struct ScopedTimeAndWeather {
    // Full time_info_data snapshot taken at construction, written back on
    // scope exit.
    time_info_data saved_time_info;
    // weather_info.moonlight/moonphase snapshot (weather_info itself is a
    // shared global; only these two fields are touched by do_time).
    int saved_moonlight;
    int saved_moonphase;

    ScopedTimeAndWeather()
        : saved_time_info(time_info)
        , saved_moonlight(weather_info.moonlight)
        , saved_moonphase(weather_info.moonphase)
    {
    }

    ~ScopedTimeAndWeather()
    {
        time_info = saved_time_info;
        weather_info.moonlight = saved_moonlight;
        weather_info.moonphase = saved_moonphase;
    }
};

// do_rank consults the process-global player index (`player_table`/
// `top_of_p_table`, db.h's player_index_element) via GET_INDEX(ch) ==
// ch->player_index rather than any pkill leaderboard machinery for the
// character's OWN rank number: pkill_get_totalrank_by_character_id() reads
// player_table[idx].rank directly. A single-entry player_table with
// .rank == 0 is therefore enough to drive do_rank's "ranked" header without
// booting real pkill data. The leaderboard walk that follows
// (pkill_get_leader_by_rank, for the "3 above/3 below" listing) consults
// pkill.cpp's file-local good_ranking/evil_ranking RANKING tables instead;
// those are static globals nothing in this test binary ever populates
// (rank_len stays 0), so pkill_get_leader_by_rank immediately returns an
// invalid dummy leader and do_rank's per-rank loop stops after one no-op
// iteration -- never touching player_table beyond the rank lookup above.
struct RankedCharacterContext {
    char_data character {};
    descriptor_data descriptor {};
    // Single-entry stand-in for the real player_table; player_index 0
    // resolves into this entry via GET_INDEX(ch)/pkill_get_rank_by_character.
    player_index_element entry {};
    player_index_element* saved_player_table;
    int saved_top_of_p_table;

    explicit RankedCharacterContext(int race)
    {
        clear_char(&character, MOB_VOID);
        reset_capturing_descriptor(descriptor, &character);
        character.desc = &descriptor;
        character.specials.position = POSITION_STANDING;
        character.player.race = race;
        character.player_index = 0;

        entry.name = const_cast<char*>("Ranked");
        entry.rank = 0;
        entry.totalrank = 0;

        saved_player_table = player_table;
        saved_top_of_p_table = top_of_p_table;
        player_table = &entry;
        top_of_p_table = 0;
    }

    ~RankedCharacterContext()
    {
        player_table = saved_player_table;
        top_of_p_table = saved_top_of_p_table;
    }
};

// Mirrors act_wiz_tests.cpp's ScopedDescriptorList (Phase 4 Wave 1): saves
// and clears the process-global descriptor_list around a do_who/do_users
// test, restoring it on scope exit. Deliberately simpler than the
// act_wiz_tests.cpp original -- this file's descriptor-list tests never
// touch the account-character-selection-unlock map that class also resets,
// so that call is left out rather than pulled in for no reason. Per-file
// duplication (not a shared header) matches this suite's existing
// reset_capturing_descriptor/SoloCharacterContext convention.
class ScopedDescriptorList {
public:
    ScopedDescriptorList()
        : m_previous_descriptor_list(descriptor_list)
    {
        descriptor_list = nullptr;
    }

    ~ScopedDescriptorList()
    {
        descriptor_list = m_previous_descriptor_list;
    }

private:
    // The process-global descriptor_list value in effect before this guard,
    // restored on scope exit.
    descriptor_data* m_previous_descriptor_list;
};

// Two connected PCs wired into a minimal, from-scratch descriptor_list --
// covers do_who's header/dashline/footer-count formatting without needing
// any world/room data (do_who only touches ch->in_room for the "-z"/"-r"
// filters, neither of which this fixture exercises).
struct WhoDescriptorListContext {
    ScopedDescriptorList descriptor_list_scope;
    char_data viewer {};
    char_data other {};
    descriptor_data viewer_descriptor {};
    descriptor_data other_descriptor {};

    WhoDescriptorListContext()
    {
        clear_char(&viewer, MOB_VOID);
        clear_char(&other, MOB_VOID);
        reset_capturing_descriptor(viewer_descriptor, &viewer);
        reset_capturing_descriptor(other_descriptor, &other);
        viewer_descriptor.connected = CON_PLYNG;
        other_descriptor.connected = CON_PLYNG;
        viewer.desc = &viewer_descriptor;
        other.desc = &other_descriptor;

        viewer.player.race = RACE_HUMAN;
        other.player.race = RACE_HUMAN;
        viewer.player.level = 50;
        other.player.level = 30;
        viewer.player.name = const_cast<char*>("Viewer");
        other.player.name = const_cast<char*>("Other");
        viewer.player.title = const_cast<char*>("");
        other.player.title = const_cast<char*>("the Wanderer");
        viewer.specials.position = POSITION_STANDING;
        other.specials.position = POSITION_STANDING;

        other_descriptor.next = nullptr;
        viewer_descriptor.next = &other_descriptor;
        descriptor_list = &viewer_descriptor;
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

// ---------------------------------------------------------------------------
// do_score (act_info.cpp:1900)
// ---------------------------------------------------------------------------

// do_score: pins the two leading stat lines for a solo PC with known
// abilities. bufpt-accumulation converts to a single std::string in the
// transform; these bytes must not move. (Exemplar from the task brief; the
// OB/DB/PB/Speed/Gold line that follows depends on get_real_OB()/
// get_real_dodge()/get_real_parry()/utils::get_energy_regen() -- derived
// combat stats this fixture doesn't pin -- so only the leading substring is
// asserted here.)
TEST(ActInfoSelfStatus, DoScoreFormatsHitStaminaMovesSpiritAndCombatLine)
{
    SoloCharacterContext context;
    context.character.tmpabilities.hit = 50;
    context.character.abilities.hit = 100;
    context.character.tmpabilities.mana = 40;
    context.character.abilities.mana = 80;
    context.character.tmpabilities.move = 30;
    context.character.abilities.move = 60;
    do_score(&context.character, const_cast<char*>(""), nullptr, 0, 0);
    EXPECT_TRUE(strstr(context.descriptor.small_outbuf,
                    "You have 50/100 hit, 40/80 stamina, 30/60 moves")
        != nullptr)
        << context.descriptor.small_outbuf;
}

// do_score: the XP-needed blurb's plain "%d" branch (|tmp| < 1000) --
// pins the non-K-suffixed conversion (act_info.cpp:1920). Level 50's
// next-level threshold (xp_to_level(51) == 51*51*1500 == 3,901,500) minus an
// exp total 500 short of it keeps tmp deterministic without needing to pin
// the derived OB/DB/PB combat line first.
TEST(ActInfoSelfStatus, DoScoreFormatsXpNeededUnderThousandUsesPlainNumber)
{
    SoloCharacterContext context;
    context.character.player.level = 50;
    context.character.points.exp = 51 * 51 * 1500 - 500;

    do_score(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_TRUE(strstr(context.descriptor.output, ", XP Needed: 500.\n\r") != nullptr)
        << context.descriptor.output;
}

// do_score: the XP-needed blurb's "%dK" branch (|tmp| >= 1000) -- pins the
// K-suffixed conversion (act_info.cpp:1922). Same level-50 threshold, this
// time 5000 exp short so tmp / 1000 == 5.
TEST(ActInfoSelfStatus, DoScoreFormatsXpNeededOverThousandUsesKSuffix)
{
    SoloCharacterContext context;
    context.character.player.level = 50;
    context.character.points.exp = 51 * 51 * 1500 - 5000;

    do_score(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_TRUE(strstr(context.descriptor.output, ", XP Needed: 5K.\n\r") != nullptr)
        << context.descriptor.output;
}

// do_score: immortals (level >= LEVEL_IMMORT - 1) get no XP blurb at all --
// pins the bare ".\r\n" tail appended straight after the Gold figure
// (act_info.cpp:1925). Zeroing the gold purse keeps "Gold: 0.\r\n" exact and
// distinguishes this branch (which ends the line right there) from the
// sibling branch that inserts ", XP Needed: ..." first.
TEST(ActInfoSelfStatus, DoScoreOmitsXpBlurbForImmortalLevel)
{
    SoloCharacterContext context;
    context.character.player.level = LEVEL_IMMORT;
    context.character.points.gold = 0;

    do_score(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_TRUE(strstr(context.descriptor.output, "Gold: 0.\r\n") != nullptr)
        << context.descriptor.output;
    EXPECT_EQ(strstr(context.descriptor.output, "XP Needed"), nullptr) << context.descriptor.output;
}

// do_score: PS_LightFighting with a light (bulk <= 2) wielded weapon pins
// the specialization bonus literal (act_info.cpp:1931) -- no format
// specifiers, so this is a pure survive-the-transform pin.
TEST(ActInfoSelfStatus, DoScoreFormatsLightFightingBonusLine)
{
    SoloCharacterContext context;
    context.character.profs->specialization = game_types::PS_LightFighting;

    obj_data weapon {};
    weapon.obj_flags.value[2] = 1; // bulk <= 2
    context.character.equipment[WIELD] = &weapon;

    do_score(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_TRUE(strstr(context.descriptor.output,
                    "The lightness of your weapon lends precision to your strikes.\r\n")
        != nullptr)
        << context.descriptor.output;
}

// do_score: PS_HeavyFighting with a heavy (bulk >= 4) wielded weapon pins
// the sibling specialization bonus literal (act_info.cpp:1936).
TEST(ActInfoSelfStatus, DoScoreFormatsHeavyFightingBonusLine)
{
    SoloCharacterContext context;
    context.character.profs->specialization = game_types::PS_HeavyFighting;

    obj_data weapon {};
    weapon.obj_flags.value[2] = 4; // bulk >= 4
    context.character.equipment[WIELD] = &weapon;

    do_score(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_TRUE(strstr(context.descriptor.output,
                    "The heft of your weapon lends power to your blows.\r\n")
        != nullptr)
        << context.descriptor.output;
}

// do_score: PS_WildFighting + TACTICS_BERSERK at <= 45% health pins the
// berserker-fury literal (act_info.cpp:1941).
TEST(ActInfoSelfStatus, DoScoreFormatsWildFightingBerserkLowHealthLine)
{
    SoloCharacterContext context;
    context.character.profs->specialization = game_types::PS_WildFighting;
    context.character.specials.tactics = TACTICS_BERSERK;
    context.character.tmpabilities.hit = 40;
    context.character.abilities.hit = 100; // 40% <= 0.45f threshold

    do_score(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_TRUE(strstr(context.descriptor.output, "Your fury lends speed to your attacks!\r\n")
        != nullptr)
        << context.descriptor.output;
}

// do_score: PS_WeaponMaster's append_score_message() interop
// (player_spec::weapon_master_handler, warrior_spec_handlers.h) -- pins the
// char* staging-buffer bridge (transform idiom catalog item 3's named
// exception: the legacy helper's char* signature is kept as-is, fed by a
// local `char stage[MAX_STRING_LENGTH]`) with a stabbing weapon's specific
// message text.
TEST(ActInfoSelfStatus, DoScoreFormatsWeaponMasterScoreMessage)
{
    SoloCharacterContext context;
    context.character.profs->specialization = game_types::PS_WeaponMaster;

    obj_data weapon {};
    weapon.obj_flags.type_flag = ITEM_WEAPON;
    weapon.obj_flags.value[3] = game_types::WT_STABBING;
    context.character.equipment[WIELD] = &weapon;

    do_score(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_TRUE(strstr(context.descriptor.output,
                    "Your mastery grants defensive prowess and armor piercing blows.\r\n")
        != nullptr)
        << context.descriptor.output;
}

// do_score: GET_COND(DRUNK) > 10 pins the intoxication literal
// (act_info.cpp:1949) -- note the "\n\r" byte order (not "\r\n"), preserved
// verbatim from the legacy source (do_info's sibling intoxication line a few
// hundred lines up uses the opposite "\r\n" order -- see the do_info test
// below -- so this pins the two sites don't accidentally converge under the
// transform).
TEST(ActInfoSelfStatus, DoScoreFormatsDrunkLineWithLegacyByteOrder)
{
    SoloCharacterContext context;
    context.character.specials2.conditions[DRUNK] = 11;

    do_score(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_TRUE(strstr(context.descriptor.output, "You are intoxicated.\n\r") != nullptr)
        << context.descriptor.output;
}

// do_score: GET_COND(FULL) == 0 pins the plain hunger literal
// (act_info.cpp:1953).
TEST(ActInfoSelfStatus, DoScoreFormatsHungryLineWhenFullConditionIsZero)
{
    SoloCharacterContext context;
    context.character.specials2.conditions[FULL] = 0;

    do_score(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_TRUE(strstr(context.descriptor.output, "You are hungry.\r\n") != nullptr)
        << context.descriptor.output;
}

// do_score: 0 < GET_COND(FULL) < 4 pins the "getting hungry" literal
// (act_info.cpp:1955).
TEST(ActInfoSelfStatus, DoScoreFormatsGettingHungryLineWhenFullConditionIsLow)
{
    SoloCharacterContext context;
    context.character.specials2.conditions[FULL] = 2;

    do_score(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_TRUE(strstr(context.descriptor.output, "You are getting hungry.\r\n") != nullptr)
        << context.descriptor.output;
}

// do_score: GET_COND(THIRST) == 0 pins the plain thirst literal
// (act_info.cpp:1959).
TEST(ActInfoSelfStatus, DoScoreFormatsThirstyLineWhenThirstConditionIsZero)
{
    SoloCharacterContext context;
    context.character.specials2.conditions[THIRST] = 0;

    do_score(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_TRUE(strstr(context.descriptor.output, "You are thirsty.\r\n") != nullptr)
        << context.descriptor.output;
}

// do_score: 0 < GET_COND(THIRST) < 4 pins the "getting thirsty" literal
// (act_info.cpp:1961).
TEST(ActInfoSelfStatus, DoScoreFormatsGettingThirstyLineWhenThirstConditionIsLow)
{
    SoloCharacterContext context;
    context.character.specials2.conditions[THIRST] = 2;

    do_score(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_TRUE(strstr(context.descriptor.output, "You are getting thirsty.\r\n") != nullptr)
        << context.descriptor.output;
}

// ---------------------------------------------------------------------------
// do_info (act_info.cpp:1670)
// ---------------------------------------------------------------------------

// do_info: pins the leading identity line's nz()-wrapped null-title case
// (act_info.cpp:1682) -- GET_TITLE(ch) is null for a freshly-cleared PC
// (clear_char() never allocates player.title), so the old sprintf("%s", NULL)
// glibc "(null)" literal must survive the std::format conversion exactly,
// same regression class as Task 1's do_exits null-keyword test.
TEST(ActInfoSelfStatus, DoInfoFormatsIdentityLineWithNullTitleAsGlibcNullLiteral)
{
    RoomCharacterContext context;
    context.character.tmpabilities.str = 100; // avoid room_move_cost() / 0
    context.character.player.name = const_cast<char*>("Frodo");
    ASSERT_EQ(context.character.player.title, nullptr);

    ensure_skill_timer_created();
    do_info(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_TRUE(strstr(context.descriptor.output,
                    "You are Frodo (null), a neutral (0) neutral Human.\n\r")
        != nullptr)
        << context.descriptor.output;
}

// do_info: GET_SPEC-less/unspecialized PC pins the "not specialized" literal
// (act_info.cpp:1702).
TEST(ActInfoSelfStatus, DoInfoFormatsNotSpecializedLine)
{
    RoomCharacterContext context;
    context.character.tmpabilities.str = 100;
    ASSERT_EQ(context.character.profs->specialization, (int)game_types::PS_None);

    ensure_skill_timer_created();
    do_info(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_TRUE(strstr(context.descriptor.output, "You are not specialized in anything.\n\r")
        != nullptr)
        << context.descriptor.output;
}

// do_info: a specialized PC pins the "specialized in %s" conversion
// (act_info.cpp:1704), including the specialize_name[] table lookup.
TEST(ActInfoSelfStatus, DoInfoFormatsSpecializedLine)
{
    RoomCharacterContext context;
    context.character.tmpabilities.str = 100;
    context.character.profs->specialization = game_types::PS_LightFighting;

    ensure_skill_timer_created();
    do_info(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_TRUE(strstr(context.descriptor.output, "You are specialized in light fighting.\n\r")
        != nullptr)
        << context.descriptor.output;
}

// do_info: pins the add_move_report() interop (act_info.cpp:1798) surviving
// the accumulation-to-std::string conversion -- add_move_report() strcat()s
// into its `bf` argument, so the transform must feed it a zeroed local
// staging buffer (same "legacy helper appends into char*" shape as
// do_score's weapon_master interop) rather than a raw std::string. With
// tmpabilities.str == 100 and no equipment/encumbrance, room_move_cost()'s
// only variable term (a `number(0, 99)` draw) lands in [100, 199] after the
// sector-2 multiplier and always collapses to 1 after the final /100
// integer division -- so real_move == 1 deterministically regardless of the
// RNG draw, landing in the "You move easily indeed." bucket (1 < 3, not < 1).
TEST(ActInfoSelfStatus, DoInfoAppendsMoveReportViaStagingBufferInterop)
{
    RoomCharacterContext context;
    context.character.tmpabilities.str = 100;

    ensure_skill_timer_created();
    do_info(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    // add_move_report()'s "easily indeed" literal uses "\n\r" byte order
    // (act_info.cpp:1657), not "\r\n" -- verified against the captured
    // pre-transform buffer.
    EXPECT_TRUE(strstr(context.descriptor.output, "You move easily indeed.\n\r") != nullptr)
        << context.descriptor.output;
}

// do_info: the XP-needed blurb's plain "%d" branch (act_info.cpp:1803-1806),
// do_info's own sibling of do_score's identically-shaped conversion.
TEST(ActInfoSelfStatus, DoInfoFormatsXpBlurbUnderCapForNonImmortal)
{
    RoomCharacterContext context;
    context.character.tmpabilities.str = 100;
    context.character.player.level = 50;
    context.character.points.exp = 51 * 51 * 1500 - 500;

    ensure_skill_timer_created();
    do_info(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_TRUE(strstr(context.descriptor.output,
                    "You have scored 3901000 experience points, and need 500 more to advance.\n\r")
        != nullptr)
        << context.descriptor.output;
}

// do_info: the immortal-level branch (act_info.cpp:1808) omits the
// "need N more to advance" clause entirely.
TEST(ActInfoSelfStatus, DoInfoOmitsXpBlurbForImmortalLevel)
{
    RoomCharacterContext context;
    context.character.tmpabilities.str = 100;
    context.character.player.level = LEVEL_IMMORT;
    context.character.points.exp = 12345;

    ensure_skill_timer_created();
    do_info(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_TRUE(strstr(context.descriptor.output, "You have scored 12345 experience points.\r\n")
        != nullptr)
        << context.descriptor.output;
}

// do_info: POSITION_FIGHTING with no `ch->specials.fighting` opponent pins
// the "fighting thin air" literal (act_info.cpp:1851).
TEST(ActInfoSelfStatus, DoInfoFormatsPositionFightingThinAirLine)
{
    RoomCharacterContext context;
    context.character.tmpabilities.str = 100;
    context.character.specials.position = POSITION_FIGHTING;
    ASSERT_EQ(context.character.specials.fighting, nullptr);

    ensure_skill_timer_created();
    do_info(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_TRUE(strstr(context.descriptor.output, "You are fighting thin air.\r\n") != nullptr)
        << context.descriptor.output;
}

// do_info: all three special-condition lines (act_info.cpp:1864-1869) --
// note the "\r\n" byte order here, the opposite of do_score's "\n\r" for the
// same intoxication message (see the do_score drunk-line test above).
TEST(ActInfoSelfStatus, DoInfoFormatsSpecialConditionLinesWithLegacyByteOrder)
{
    RoomCharacterContext context;
    context.character.tmpabilities.str = 100;
    context.character.specials2.conditions[DRUNK] = 11;
    context.character.specials2.conditions[FULL] = 0;
    context.character.specials2.conditions[THIRST] = 0;

    ensure_skill_timer_created();
    do_info(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_TRUE(strstr(context.descriptor.output,
                    "You are intoxicated.\r\nYou are hungry.\r\nYou are thirsty.\r\n")
        != nullptr)
        << context.descriptor.output;
}

// ---------------------------------------------------------------------------
// do_toggle (act_info.cpp:2813)
// ---------------------------------------------------------------------------

// do_toggle: the default (all-off) preference table for a non-immortal PC --
// pins the entire %-3s-padded three-column table plus the trailing tactics/
// language line (act_info.cpp:2848-2894), fully determined by
// SoloCharacterContext's clear_char() defaults (every PRF_* bit off,
// WIMP_LEVEL 0, TACTICS_NORMAL, no spoken language). No immortal-only
// roomflags block and no archer/arcane shooting/casting line, since GET_SPEC
// stays PS_None/0.
TEST(ActInfoSelfStatus, DoToggleFormatsDefaultPreferenceTableForMortalPc)
{
    SoloCharacterContext context;
    ASSERT_LT(context.character.player.level, LEVEL_IMMORT);
    ASSERT_EQ(GET_SPEC(&context.character), 0);

    do_toggle(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_STREQ(context.descriptor.output,
        "         Prompt: OFF         Brief Mode: OFF             NoTell: OFF\r\n"
        "   Compact Mode: OFF    Narrate Channel: OFF          MSDP Mode: OFF\r\n"
        "      Spam Mode: OFF       Chat Channel: OFF     Incognito Mode: ON \r\n"
        "           Echo: OFF       Sing Channel: OFF        Auto Mental: OFF\r\n"
        "      Wrap Mode: OFF     Summon Protect: OFF         Wimp Level: OFF\r\n"
        "           Swim: OFF            Latin-1: OFF            Spinner: OFF\r\n"
        "  Advanced View: OFF    Advanced Prompt: OFF    "
        "\r\nYou are employing normal tactics, and are speaking common tongue.\r\n");
}

// do_toggle: an immortal (>= LEVEL_IMMORT) additionally sees the roomflags/
// holylight/nohassle/wiznet block (act_info.cpp:2882-2891) appended before
// the tactics/language line.
TEST(ActInfoSelfStatus, DoToggleAppendsImmortalRoomflagsBlockForGodLevel)
{
    SoloCharacterContext context;
    context.character.player.level = LEVEL_IMMORT;

    do_toggle(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_TRUE(strstr(context.descriptor.output,
                    "      Roomflags: OFF\r\n"
                    "      Holylight: OFF    "
                    "       Nohassle: OFF    "
                    " Wiznet Channel: OFF\r\n")
        != nullptr)
        << context.descriptor.output;
}

// ---------------------------------------------------------------------------
// do_affections (act_info.cpp:3520)
// ---------------------------------------------------------------------------

// do_affections: an unaffected PC pins the strcpy "not affected" literal
// (act_info.cpp:3539) as the entire buffer -- fully deterministic once the
// idnum sentinel keeps this test's report_skill_timer() lookup (skill_timer.h,
// a process-global singleton shared by the monolithic runner) from matching
// any other suite's registered skill timers.
TEST(ActInfoSelfStatus, DoAffectionsFormatsNotAffectedLineWhenNoActiveEffects)
{
    SoloCharacterContext context;
    context.character.specials2.idnum = 90210001; // sentinel: no other suite uses this id
    ASSERT_EQ(context.character.affected, nullptr);

    ensure_skill_timer_created();
    do_affections(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_STREQ(context.descriptor.output, "You are not affected by anything.\n\r");
}

// do_affections: one active affect pins the self-referential
// `sprintf(buf, "%s%s", buf, str)` accumulation (act_info.cpp:3545)
// converting cleanly to std::string concatenation -- report_affection()
// itself (act_info.cpp:3485) is a helper outside this chunk's file list and
// is not modified; this only exercises do_affections' own loop wrapping it.
TEST(ActInfoSelfStatus, DoAffectionsFormatsAffectedByListEntryWithDurationTag)
{
    SoloCharacterContext context;
    context.character.specials2.idnum = 90210002;

    affected_type aff {};
    aff.type = SKILL_SEARCH;
    aff.duration = 5; // 3 <= duration < 12 -> durations[2] == "medium"
    aff.next = nullptr;
    context.character.affected = &aff;

    ensure_skill_timer_created();
    do_affections(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    std::string expected = "You are affected by:\n\r" + std::format("{:<30} ({})\n\r", "search", "medium");
    EXPECT_STREQ(context.descriptor.output, expected.c_str());
}

// do_affections: AFF_SNEAK pins the sneaking-announcement literal
// (act_info.cpp:3527), independent of/preceding the affected-list section.
TEST(ActInfoSelfStatus, DoAffectionsFormatsSneakLineBeforeAffectedByList)
{
    SoloCharacterContext context;
    context.character.specials2.idnum = 90210003;
    SET_BIT(context.character.specials.affected_by, AFF_SNEAK);

    ensure_skill_timer_created();
    do_affections(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_STREQ(context.descriptor.output,
        "You are trying to sneak.\n\rYou are not affected by anything.\n\r");
}

// ---------------------------------------------------------------------------
// do_gen_ps (act_info.cpp:2534) -- SCMD_WHOAMI branch
// ---------------------------------------------------------------------------

// do_gen_ps's SCMD_WHOAMI branch pins the
// `strcat(strcpy(buf, GET_NAME(ch)), "\n\r")` conversion (act_info.cpp:2567).
TEST(ActInfoSelfStatus, DoGenPsWhoamiEchoesCharacterNameWithCrlf)
{
    SoloCharacterContext context;
    context.character.player.name = const_cast<char*>("Aragorn");

    do_gen_ps(&context.character, const_cast<char*>(""), nullptr, 0, SCMD_WHOAMI);

    EXPECT_STREQ(context.descriptor.output, "Aragorn\n\r");
}

// ---------------------------------------------------------------------------
// Characterization tests for Phase 4 Wave 3 Task 3 (Chunk I3 --
// act_info.cpp world/social family: do_time, do_rank, do_consider, do_who,
// do_levels; plus every act_info RAII allocation site in the file --
// do_time's nth(), do_fame's pkill_get_string x3 + rots_asprintf, do_rank's
// nth() -- this chunk owns them all). Suite ActInfoWorldSocial below. Same
// binding pattern as Tasks 1-2: these pin CURRENT byte-for-byte output --
// confirmed passing against the pre-conversion source -- before the
// bufpt-accumulation/sprintf-chain sites convert to std::format/std::string
// composition and the four malloc'd-char* RAII sites become immediate
// capture-and-free, and green again after.
//
// Deliberately NOT unit-tested here (documented exclusions, not oversights):
//  - do_weather (act_info.cpp:2034): every branch is a literal
//    send_to_char() (or a delegate call to the extern weather_to_char()) --
//    no sprintf/strcpy of its own, nothing for this task's transform to
//    touch.
//  - do_consider (act_info.cpp:2781) itself has no sprintf/strcpy either --
//    every diff band is a literal send_to_char() -- so there is no
//    conversion site in do_consider proper; the tests below still pin its
//    level-delta ladder because the brief calls it out by name and the
//    ladder is cheap to lock in, but "converting" it is a no-op.
//  - do_where (act_info.cpp:2701) is a two-line dispatcher with no sprintf
//    of its own; the formatting lives in perform_mortal_where/
//    perform_immort_where (act_info.cpp:2585/2620), which are NOT in this
//    chunk's function list (same exclusion pattern as Task 1's
//    diag_char_to_char) -- not converted or tested here.
//  - do_users (act_info.cpp:2377), do_help (act_info.cpp:2075),
//    do_commands (act_info.cpp:2975), do_whois (act_info.cpp:3125), and
//    do_fame (act_info.cpp:3635, plus do_fame_leader_string:3614) are
//    converted (transform diff) but not pinned by a dedicated fixture-driven
//    test here: do_users/do_who both need a live descriptor_list, and the
//    minimal WhoDescriptorListContext below already exercises that harness
//    shape for do_who; do_help needs lib/text/help_tbl file data (fseek/
//    fgets against help_content[]) this binary never loads; do_commands
//    needs a populated cmd_info[]/sort_commands() table (real command
//    dispatch, not fixture-buildable); do_whois needs a populated
//    player_table AND the on-disk player file load_player() falls back to;
//    do_fame's "war"/"all" paths need real pkill leaderboard data
//    (good_ranking/evil_ranking) and its by-name path needs
//    find_player_in_table() to resolve a real player_table entry by name --
//    all four are covered instead by the transform-diff review plus the
//    macOS/rots64 dual gate's boot-golden smoke test, per this wave's
//    pre-approved fallback for fixture-impractical sites.
//  - get_level_abbr (act_info.cpp:3220), do_whois's static level-string
//    helper, is textually adjacent to do_whois but is NOT in this chunk's
//    function list -- not converted or tested here (same exclusion pattern
//    as Task 1/2's out-of-list static helpers).

TEST(ActInfoWorldSocial, DoTimeFormatsStewardsReckoningYearWithNthOrdinalSuffix)
{
    SoloCharacterContext context;
    ScopedTimeAndWeather time_guard;

    // year % 10 == 1 but year != 11 -> the "st" branch of nth(), not the
    // default "th" -- exercises the ordinal-suffix logic the RAII
    // conversion (nth()'s malloc'd char* -> capture-and-free) must not
    // disturb.
    time_info.year = 21;
    time_info.month = 5;
    time_info.day = 10;
    time_info.hours = 10;
    weather_info.moonlight = 0;
    weather_info.moonphase = 0;

    do_time(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_TRUE(strstr(context.descriptor.output,
                    "By the Steward's Reckoning, it is the 21st year of the "
                    "fourth age of Arda.\r\n")
        != nullptr)
        << context.descriptor.output;
}

// do_rank: pins the ranked-path header INCLUDING nth()'s ordinal suffix
// ("1st"), which the RAII conversion must not disturb, for a good-race PC.
TEST(ActInfoWorldSocial, DoRankFormatsOrdinalRankHeaderForGoodRace)
{
    RankedCharacterContext context(RACE_HUMAN);

    do_rank(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_TRUE(strstr(context.descriptor.output,
                    "You are ranked 1st among the free peoples of "
                    "Middle-earth:\r\n")
        != nullptr)
        << context.descriptor.output;
}

// do_rank: same ranked-path header, evil-race branch -- pins the
// RACE_GOOD(ch) ternary's other side.
TEST(ActInfoWorldSocial, DoRankFormatsOrdinalRankHeaderForEvilRace)
{
    RankedCharacterContext context(RACE_URUK);

    do_rank(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_TRUE(strstr(context.descriptor.output,
                    "You are ranked 1st among the forces of the Shadow:\r\n")
        != nullptr)
        << context.descriptor.output;
}

// do_consider: level-delta ladder, diff <= -10 branch (act_info.cpp:2800).
TEST(ActInfoWorldSocial, DoConsiderFormatsChickenMessageForFarWeakerVictim)
{
    RoomWithBystanderContext context;
    context.actor.player.name = const_cast<char*>("Attacker");
    context.bystander.player.name = const_cast<char*>("Weakling");
    context.actor.player.level = 30;
    context.bystander.player.level = 10; // diff == -20

    do_consider(&context.actor, const_cast<char*>("Weakling"), nullptr, 0, 0);

    EXPECT_STREQ(context.actor_descriptor.output, "Now where did that chicken go?\n\r");
}

// do_consider: level-delta ladder, diff == 0 branch (act_info.cpp:2808).
TEST(ActInfoWorldSocial, DoConsiderFormatsPerfectMatchMessageForEqualLevel)
{
    RoomWithBystanderContext context;
    context.actor.player.name = const_cast<char*>("Attacker");
    context.bystander.player.name = const_cast<char*>("Peer");
    context.actor.player.level = 20;
    context.bystander.player.level = 20; // diff == 0

    do_consider(&context.actor, const_cast<char*>("Peer"), nullptr, 0, 0);

    EXPECT_STREQ(context.actor_descriptor.output, "The perfect match!\n\r");
}

// do_consider: level-delta ladder, diff == 10 branch (act_info.cpp:2818) --
// the upper edge of the "Are you mad!?" band.
TEST(ActInfoWorldSocial, DoConsiderFormatsAreYouMadMessageAtUpperBoundary)
{
    RoomWithBystanderContext context;
    context.actor.player.name = const_cast<char*>("Attacker");
    context.bystander.player.name = const_cast<char*>("Champion");
    context.actor.player.level = 20;
    context.bystander.player.level = 30; // diff == 10

    do_consider(&context.actor, const_cast<char*>("Champion"), nullptr, 0, 0);

    EXPECT_STREQ(context.actor_descriptor.output, "Are you mad!?\n\r");
}

// do_consider: level-delta ladder, diff == 11 branch (act_info.cpp:2820) --
// one past the "Are you mad!?" band, into the final "You ARE mad!" catch-all.
TEST(ActInfoWorldSocial, DoConsiderFormatsYouAreMadMessageBeyondUpperBoundary)
{
    RoomWithBystanderContext context;
    context.actor.player.name = const_cast<char*>("Attacker");
    context.bystander.player.name = const_cast<char*>("Overlord");
    // Both levels stay <= LEVEL_MAX (30) so GET_LEVELB()'s dampening
    // (utils.h: min(level, LEVEL_MAX*2/3 + level/3)) is a no-op here and
    // diff is a plain level subtraction.
    context.actor.player.level = 19;
    context.bystander.player.level = 30; // diff == 11

    do_consider(&context.actor, const_cast<char*>("Overlord"), nullptr, 0, 0);

    EXPECT_STREQ(context.actor_descriptor.output, "You ARE mad!\n\r");
}

// do_levels: pins the header line (profession percentages + table caption)
// and the table's first data row -- a zeroed char_prof_data (from
// clear_char()) keeps every profession percentage/xp-per-level column
// deterministically 0.
TEST(ActInfoWorldSocial, DoLevelsFormatsHeaderAndFirstTableRow)
{
    SoloCharacterContext context;

    do_levels(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_TRUE(strstr(context.descriptor.output,
                    "You are 0% Warrior, 0% Ranger, 0% Mystic, 0% Mage\r\n"
                    "Level:  Exp. to Level  : Warrior :  Ranger :  Mystic : Mage :\r\n")
        != nullptr)
        << context.descriptor.output;
    EXPECT_TRUE(strstr(context.descriptor.output, "[ 1]     1500-6000     :         0         0         0         0\n\r")
        != nullptr)
        << context.descriptor.output;
}

// do_levels: IS_NPC(ch) guard rejects mobiles outright -- no table at all.
TEST(ActInfoWorldSocial, DoLevelsRejectsNpcCaller)
{
    SoloCharacterContext context;
    SET_BIT(context.character.specials2.act, MOB_ISNPC);

    do_levels(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_STREQ(context.descriptor.output, "You ain't nothin' but a hound-dog.\n\r");
}

// do_who: header/dashline/footer-count formatting against a minimal,
// from-scratch descriptor_list (two connected PCs, no world/room data) --
// pins the "Players\r\n-------\r\n" header/dashline pair and the
// "N character(s) displayed." footer's pluralization.
TEST(ActInfoWorldSocial, DoWhoFormatsHeaderDashlineAndFooterCountForTwoPlayers)
{
    WhoDescriptorListContext context;

    do_who(&context.viewer, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_TRUE(strstr(context.viewer_descriptor.output, "Players\r\n-------\r\n") != nullptr)
        << context.viewer_descriptor.output;
    EXPECT_TRUE(strstr(context.viewer_descriptor.output, "\n\r2 characters displayed.\n\r") != nullptr)
        << context.viewer_descriptor.output;
}
