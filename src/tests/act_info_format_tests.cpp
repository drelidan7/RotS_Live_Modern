#include "../account_management_storage.h"
#include "../big_brother.h"
#include "../db.h"
#include "../handler.h"
#include "../interpre.h"
#include "../rots_rng.h"
#include "../skill_timer.h"
#include "../spells.h"
#include "rots/core/character.h"
#include "rots/core/object.h"
#include "rots/core/room.h"
#include "rots/core/descriptor.h"
#include "rots/core/types.h"
#include "../utils.h"
#include "ObjFlagDataBuilder.h"
#include "test_char_cleanup.h"
#include "test_platform_compat.h"
#include "test_world.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>

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
ACMD(do_compare);
ACMD(do_stat);
ACMD(do_exploits);
ACMD(do_help);

// Plain (non-ACMD) object-display helpers reached via do_identify_object's
// ITEM_LIGHT/ITEM_FOOD/ITEM_WEAPON switch (act_info.cpp) -- exercised
// directly below rather than only transitively, per the task brief.
void do_food_display(struct char_data* ch, struct obj_data* j);
void do_light_display(struct char_data* ch, struct obj_data* j);
void do_flag_values_display(struct char_data* ch, struct obj_data* j);
void do_weapon_display(struct char_data* ch, struct obj_data* j);
void do_identify_object(struct char_data* ch, struct obj_data* j);
// Signature copied verbatim from its forward declaration in interpre.cpp --
// do_details is a plain function (not registered via the ACMD macro,
// though its parameter shape matches ACMD's expansion).
void do_details(char_data* character, char* argument, waiting_type* wait_list, int command,
    int sub_command);

// The buf-aliasing display cluster (act_info.cpp's "WAVE 3 TASK 9 SWEEP"
// block comment above get_char_position_line) -- none of these are declared
// in a shared header (show_char_to_char is the lone exception, via
// utils.h's default-argument declaration already visible through the
// includes above), so each gets its own forward declaration here, matching
// the convention above for do_food_display/do_light_display/etc.
void get_char_position_line(struct char_data* ch, struct char_data* i, char* str);
void get_char_flag_line(char_data* viewer, char_data* viewed, char* character_message);
void show_mount_to_char(struct char_data* mount, struct char_data* viewer,
    std::string_view singular_rider_text, std::string_view plural_rider_text, int color);
void list_char_to_char(struct char_data* list, struct char_data* ch, int mode);
void show_room_affection(char* str, struct affected_type* aff, int mode);
void show_room_weather(char* str, struct char_data* ch);

void clear_char(struct char_data* ch, int mode);

// Process globals ActInfoWorldSocial's fixtures stamp directly (db.cpp/
// comm.cpp definitions; none of these are declared in a shared header, same
// as this file's existing `extern char buf[];` below).
extern struct time_info_data time_info;
extern struct player_index_element* player_table;
extern int top_of_p_table;
extern struct descriptor_data* descriptor_list;
extern char* help;

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
    // Releases character.profs/skills/knowledge (clear_char() heap
    // allocations) when this context goes out of scope (Phase 5 T6 leak
    // sweep); declared last so it is destroyed BEFORE `character` and
    // `descriptor` above it.
    ScopedClearCharFields character_cleanup { character };
    // Returns descriptor.large_outbuf to bufpool at scope exit -- some of
    // this suite's renders (do_info, do_toggle, etc.) overflow
    // descriptor.small_outbuf, promoting it to a heap-allocated large_outbuf
    // block (Phase 5 T6 leak sweep; see test_char_cleanup.h).
    ScopedDescriptorLargeOutbufReturn descriptor_large_outbuf_cleanup { descriptor };

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
// clear_char(&character, MOB_VOID) sizes character.knowledge to MAX_SKILLS
// zeros (mode != MOB_ISNPC), so do_search's GET_SKILL(ch, SKILL_SEARCH)
// lookup (utils.h) reads real bytes instead of the empty-vector 80-and-no-
// confuse-modifier fallback; tests set a deterministic value directly on
// character.knowledge, matching char_utils_tests.cpp's convention.
struct RoomCharacterContext {
    ScopedTestWorld test_world;
    char_data character {};
    descriptor_data descriptor {};
    char_data* original_people = nullptr;
    // Releases character.profs (clear_char() heap allocation) when this
    // context goes out of scope (Phase 5 T6 leak sweep); character.skills/
    // character.knowledge are owning std::vector<byte> members (RAII T3) that
    // release themselves automatically when `character` goes out of scope.
    ScopedClearCharFields character_cleanup { character };
    ScopedDescriptorLargeOutbufReturn descriptor_large_outbuf_cleanup { descriptor };

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
    char_data* original_people = nullptr;
    // See RoomCharacterContext's ScopedClearCharFields comment (Phase 5 T6
    // leak sweep).
    ScopedClearCharFields character_cleanup { character };
    ScopedDescriptorLargeOutbufReturn descriptor_large_outbuf_cleanup { descriptor };

    RoomWithExitContext()
    {
        clear_char(&character, MOB_VOID);
        reset_capturing_descriptor(descriptor, &character);
        // See RoomCharacterContext's comment: do_look requires a non-zero fd.
        descriptor.descriptor = 7;

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
    char_data* original_people = nullptr;
    // See RoomCharacterContext's ScopedClearCharFields comment (Phase 5 T6
    // leak sweep).
    ScopedClearCharFields character_cleanup { character };
    ScopedDescriptorLargeOutbufReturn descriptor_large_outbuf_cleanup { descriptor };

    TwoRoomLookContext()
    {
        clear_char(&character, MOB_VOID);
        reset_capturing_descriptor(descriptor, &character);
        // See RoomCharacterContext's comment: do_look requires a non-zero fd.
        descriptor.descriptor = 7;

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
    char_data* original_people = nullptr;
    // See RoomCharacterContext's ScopedClearCharFields comment (Phase 5 T6
    // leak sweep).
    ScopedClearCharFields actor_cleanup { actor };
    ScopedClearCharFields bystander_cleanup { bystander };
    ScopedDescriptorLargeOutbufReturn actor_descriptor_large_outbuf_cleanup { actor_descriptor };
    ScopedDescriptorLargeOutbufReturn bystander_descriptor_large_outbuf_cleanup { bystander_descriptor };

    RoomWithBystanderContext()
    {
        clear_char(&actor, MOB_VOID);
        clear_char(&bystander, MOB_VOID);
        reset_capturing_descriptor(actor_descriptor, &actor);
        reset_capturing_descriptor(bystander_descriptor, &bystander);
        actor_descriptor.descriptor = 7;
        bystander_descriptor.descriptor = 7;

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
    // See RoomCharacterContext's ScopedClearCharFields comment (Phase 5 T6
    // leak sweep); no knowledge override here, so the guard releases all
    // three clear_char() fields as-is.
    ScopedClearCharFields character_cleanup { character };
    ScopedDescriptorLargeOutbufReturn descriptor_large_outbuf_cleanup { descriptor };

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
    // See RoomCharacterContext's ScopedClearCharFields comment (Phase 5 T6
    // leak sweep); neither char overrides its knowledge pointer, so both
    // guards release all three clear_char() fields as-is.
    ScopedClearCharFields viewer_cleanup { viewer };
    ScopedClearCharFields other_cleanup { other };
    ScopedDescriptorLargeOutbufReturn viewer_descriptor_large_outbuf_cleanup { viewer_descriptor };
    ScopedDescriptorLargeOutbufReturn other_descriptor_large_outbuf_cleanup { other_descriptor };

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

// Per-file copy of act_wiz_tests.cpp's TemporaryDirectory (Phase 4 Wave 3
// Task 4 -- do_exploits/print_exploits needs a real, throwaway
// "<root>/exploits/<bucket>/" directory to read a legacy .exploits binary
// record from). Same rots_mkdtemp portable-mkdtemp shim, same
// remove_all-not-system("rm -rf") cleanup; duplicated rather than shared,
// matching this file's existing reset_capturing_descriptor/
// SoloCharacterContext per-file-copy convention.
class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        char directory_template[] = "/tmp/rots-act-info-tests-XXXXXX";
        char* created_path = rots_mkdtemp(directory_template);
        EXPECT_NE(created_path, nullptr);
        if (created_path != nullptr)
            m_path = created_path;
    }

    ~TemporaryDirectory()
    {
        if (!m_path.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(m_path, ec);
        }
    }

    const std::string& path() const { return m_path; }

private:
    std::string m_path;
};

// Per-file copy of act_wiz_tests.cpp's/account_management_tests.cpp's
// ScopedWorkingDirectory: print_exploits (act_info.cpp) hardcodes its
// exploit-history root directory as "." (relative to the process's current
// working directory), so a test that wants it to resolve into a throwaway
// TemporaryDirectory must chdir there for the duration of the call.
// std::filesystem::current_path() is the portable getcwd()/chdir() stand-in
// (no <unistd.h> dependency, unavailable on MSVC).
class ScopedWorkingDirectory {
public:
    explicit ScopedWorkingDirectory(const std::string& path)
    {
        std::error_code ec;
        m_original_path = std::filesystem::current_path(ec);
        EXPECT_FALSE(ec) << "Expected current_path() to report this test process's working directory.";

        std::filesystem::current_path(path, ec);
        EXPECT_FALSE(ec) << "Expected current_path(" << path << ") to succeed.";
    }

    ~ScopedWorkingDirectory()
    {
        if (!m_original_path.empty()) {
            std::error_code ec;
            std::filesystem::current_path(m_original_path, ec);
            EXPECT_FALSE(ec);
        }
    }

private:
    std::filesystem::path m_original_path;
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
    context.character.knowledge[SKILL_SEARCH] = 200;

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
    context.character.knowledge[SKILL_SEARCH] = 200;
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
    context.character.knowledge[SKILL_SEARCH] = 200;
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
    context.actor.knowledge[SKILL_SEARCH] = 200;

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
// ActInfoDisplayCluster (Backlog Cleanup Task 3) -- the buf-aliasing display
// cluster act_info.cpp's "WAVE 3 TASK 9 SWEEP" block comment (above
// get_char_position_line) names as a deliberate skip: show_char_to_char,
// list_char_to_char, get_char_position_line, get_char_flag_line,
// show_mount_to_char, show_room_affection, show_room_weather, plus do_look
// case 8's exit_mark[] table. A new suite rather than an ActInfoPerception
// extension, since this is its own previously-unpinned web (three prior
// waves left it alone for exactly the reasons that block comment gives).
// These pin the CURRENT byte-for-byte output -- confirmed passing against
// the pre-conversion source -- before this task's pragma-guarded sprintf()
// sites (show_char_to_char's title/no-title/pos-line sites; show_room_
// affection's self-referencing ROOMAFF_SPELL stat line and its non-self-
// referencing default-case line; show_room_weather's self-referencing snow
// line; do_look case 8's exit_mark[] runtime-selected-format-string switch)
// convert to std::format via the materialize-then-strcpy idiom, and green
// again after.
//
// Deliberately NOT pinned here (documented exclusions, not oversights):
//  - get_char_position_line/get_char_flag_line/show_mount_to_char/
//    list_char_to_char contain NO sprintf() of their own (pure strcat()) --
//    this task converts nothing inside them. They are pinned anyway because
//    they read/write the SAME global `buf' the sprintf sites write into
//    (some branches -- get_char_flag_line's "(red aura)" line,
//    get_char_position_line's POSITION_FIGHTING "someone else" branch --
//    strcat() into `buf' directly rather than through their `str'/
//    `character_message' parameter, relying on every real call site passing
//    `buf + strlen(buf)' as that parameter); these pins are the regression
//    net proving the sprintf conversions don't disturb that aliasing.
//  - get_char_flag_line's holy-protection/fame-identifier/marked/shadow/
//    sanctuary/writing/AFK branches need extra process-singleton
//    (big_brother) or race-table (other_side/pc_star_types) bootstrap
//    unrelated to this task's actual conversion sites; the hide/invisible/
//    linkless combos below are the representative pins the task brief asks
//    for.
//  - show_char_to_char's mode-1 ("look at <char>") branches: its one sprintf
//    (act_info.cpp:1013, "show_char: No description on %s.\n") writes into
//    `buf' and is UNCONDITIONALLY clobbered by `*buf = 0;' two lines later
//    before anything reads it (see the "Justified skip" comment at that call
//    site) -- its bytes are provably unobservable, and reaching them needs
//    report_char_health()/show_equipment_to_char() fixture depth this task's
//    actual risk (the sprintf conversion itself) doesn't warrant.
//  - do_look case 8's PRF_ROOMFLAGS/PRF_ADVANCED_VIEW room-name-suffix
//    sprintf sites (act_info.cpp:1543-1558) were already converted to
//    std::format in an earlier wave -- nothing left there for this task.
namespace {

// Two ordinary PCs sharing room 0 -- `viewer' is the observer (PRF_HOLYLIGHT
// so CAN_SEE()'s room-light gate never blocks it; this suite has no interest
// in darkness/light branches), `target' is the character under test. Mirrors
// RoomWithBystanderContext above, but `target' is meant to be dressed up
// differently per test rather than playing a fixed "bystander" role.
// `target_descriptor' defaults to "online" (a live `.descriptor' fd) so
// get_char_flag_line's "(linkless)" branch doesn't fire unless a test
// explicitly asks for it (by nulling `target.desc').
struct DisplayClusterContext {
    ScopedTestWorld test_world;
    char_data viewer {};
    char_data target {};
    descriptor_data viewer_descriptor {};
    descriptor_data target_descriptor {};
    char_data* original_people = nullptr;
    // See RoomCharacterContext's ScopedClearCharFields comment (Phase 5 T6
    // leak sweep).
    ScopedClearCharFields viewer_cleanup { viewer };
    ScopedClearCharFields target_cleanup { target };
    ScopedDescriptorLargeOutbufReturn viewer_descriptor_large_outbuf_cleanup { viewer_descriptor };

    DisplayClusterContext()
    {
        clear_char(&viewer, MOB_VOID);
        clear_char(&target, MOB_VOID);
        reset_capturing_descriptor(viewer_descriptor, &viewer);
        reset_capturing_descriptor(target_descriptor, &target);
        viewer_descriptor.descriptor = 7;
        target_descriptor.descriptor = 7;

        original_people = test_world.room().people;
        viewer.in_room = 0;
        target.in_room = 0;
        viewer.next_in_room = &target;
        target.next_in_room = nullptr;
        test_world.room().people = &viewer;

        viewer.specials.position = POSITION_STANDING;
        viewer.player.race = RACE_HUMAN;
        target.player.race = RACE_HUMAN;
        // big_brother::is_level_range_appropriate() treats level 0 vs level 0
        // as a >=3x ratio (0 >= 0*3) and calls it INappropriate -- a same-
        // level-0 pair otherwise falls through get_char_flag_line's
        // "!IS_NPC(viewed)" clause (true for any PC target) into a spurious
        // "(holy protection)" flag. Real characters are never level 0; a
        // shared non-zero level for both keeps that branch's "no protection
        // needed" path the one under test here.
        viewer.player.level = 20;
        target.player.level = 20;
        SET_BIT(viewer.specials2.pref, PRF_HOLYLIGHT);
        viewer.desc = &viewer_descriptor;
        target.desc = &target_descriptor;
    }

    ~DisplayClusterContext()
    {
        test_world.room().people = original_people;
        viewer.in_room = NOWHERE;
        target.in_room = NOWHERE;
    }
};

// Saves/restores weather_info.snow[sector] around a show_room_weather test
// (mirrors ScopedSunlight above) -- the monolithic runner shares one
// weather_info across every suite.
struct ScopedSnow {
    // The sector index this guard touches, remembered so the destructor
    // restores the SAME slot the constructor overwrote.
    int sector;
    // The snow value in effect before this guard, restored on scope exit.
    int saved_value;

    ScopedSnow(int sector_type, int value)
        : sector(sector_type)
        , saved_value(weather_info.snow[sector_type])
    {
        weather_info.snow[sector_type] = value;
    }

    ~ScopedSnow() { weather_info.snow[sector] = saved_value; }
};

// RoomWithExitContext's sibling for do_look's case 8 (full "look", no
// argument) exit_mark[] pins that need a SECOND room -- exit_choice 5/6
// (sunlit/shadowy) key off the EXIT TARGET room's flags while do_look's own
// SUN_PENALTY check (fired after the exits render, act_info.cpp:1691-1693)
// keys off the VIEWER's own room; room 0 is stamped INDOORS so SUN_PENALTY
// stays false (OUTSIDE(ch) fails) regardless of race/sunlight, letting room 1
// carry the sunlit/shadowy flags exit_choice 5/6 actually test without
// do_orc_delay() firing and adding unrelated output.
struct ExitMarkTwoRoomContext {
    static constexpr int door_direction = 0; // NORTH

    ScopedTestWorld test_world { 2 };
    char_data character {};
    descriptor_data descriptor {};
    room_direction_data exit {};
    char_data* original_people = nullptr;
    // See RoomCharacterContext's ScopedClearCharFields comment (Phase 5 T6
    // leak sweep).
    ScopedClearCharFields character_cleanup { character };
    ScopedDescriptorLargeOutbufReturn descriptor_large_outbuf_cleanup { descriptor };

    ExitMarkTwoRoomContext()
    {
        clear_char(&character, MOB_VOID);
        reset_capturing_descriptor(descriptor, &character);
        // See RoomCharacterContext's comment: do_look requires a non-zero fd.
        descriptor.descriptor = 7;

        original_people = test_world.room().people;
        character.in_room = 0;
        character.next_in_room = nullptr;
        test_world.room().people = &character;
        test_world.room().dir_option[door_direction] = &exit;
        test_world.room().room_flags = INDOORS;
        exit.to_room = 1;

        // See RoomWithExitContext's comment: show_blood_trail() needs a
        // zeroed bleed_track.
        std::memset(&test_world.room().bleed_track, 0, sizeof(test_world.room().bleed_track));

        world[1].room_flags = 0;
        world[1].light = 0;
        world[1].sector_type = 0;

        character.specials.position = POSITION_STANDING;
        character.player.race = RACE_HUMAN;
        character.player.level = 1;
        character.desc = &descriptor;
    }

    ~ExitMarkTwoRoomContext()
    {
        world[1].room_flags = 0;
        test_world.room().people = original_people;
        test_world.room().dir_option[door_direction] = nullptr;
        test_world.room().room_flags = 0;
        character.in_room = NOWHERE;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// get_char_position_line (act_info.cpp:595) -- no sprintf of its own (pure
// strcat()), pinned as the aliasing web's regression net (see the suite
// comment above). `str' is passed as the global `buf' directly, matching
// every real call site's `buf + strlen(buf)' aliasing (with buf pre-emptied,
// so `str' and `buf' coincide exactly).
// ---------------------------------------------------------------------------

TEST(ActInfoDisplayCluster, GetCharPositionLineFormatsShapingLine)
{
    DisplayClusterContext context;
    context.target.specials.position = POSITION_SHAPING;
    buf[0] = '\0';

    get_char_position_line(&context.viewer, &context.target, buf);

    EXPECT_STREQ(buf,
        " is sitting here in deep meditation,\r\n"
        "softly humming the ancient song of creation.");
}

TEST(ActInfoDisplayCluster, GetCharPositionLineFormatsStunnedLine)
{
    DisplayClusterContext context;
    context.target.specials.position = POSITION_STUNNED;
    buf[0] = '\0';

    get_char_position_line(&context.viewer, &context.target, buf);

    EXPECT_STREQ(buf, " is lying here, stunned.");
}

TEST(ActInfoDisplayCluster, GetCharPositionLineFormatsIncapLine)
{
    DisplayClusterContext context;
    context.target.specials.position = POSITION_INCAP;
    buf[0] = '\0';

    get_char_position_line(&context.viewer, &context.target, buf);

    EXPECT_STREQ(buf, " is lying here, mortally wounded.");
}

TEST(ActInfoDisplayCluster, GetCharPositionLineFormatsDeadLine)
{
    DisplayClusterContext context;
    context.target.specials.position = POSITION_DEAD;
    buf[0] = '\0';

    get_char_position_line(&context.viewer, &context.target, buf);

    EXPECT_STREQ(buf, " is lying here, dead.");
}

TEST(ActInfoDisplayCluster, GetCharPositionLineFormatsStandingLine)
{
    DisplayClusterContext context;
    context.target.specials.position = POSITION_STANDING;
    buf[0] = '\0';

    get_char_position_line(&context.viewer, &context.target, buf);

    EXPECT_STREQ(buf, " is standing here.");
}

TEST(ActInfoDisplayCluster, GetCharPositionLineFormatsSittingLine)
{
    DisplayClusterContext context;
    context.target.specials.position = POSITION_SITTING;
    buf[0] = '\0';

    get_char_position_line(&context.viewer, &context.target, buf);

    EXPECT_STREQ(buf, " is sitting here.");
}

TEST(ActInfoDisplayCluster, GetCharPositionLineFormatsRestingLine)
{
    DisplayClusterContext context;
    context.target.specials.position = POSITION_RESTING;
    buf[0] = '\0';

    get_char_position_line(&context.viewer, &context.target, buf);

    EXPECT_STREQ(buf, " is resting here.");
}

TEST(ActInfoDisplayCluster, GetCharPositionLineFormatsSleepingLine)
{
    DisplayClusterContext context;
    context.target.specials.position = POSITION_SLEEPING;
    buf[0] = '\0';

    get_char_position_line(&context.viewer, &context.target, buf);

    EXPECT_STREQ(buf, " is sleeping here.");
}

// POSITION_FIGHTING against the viewer themselves -- pins the "YOU!" branch.
TEST(ActInfoDisplayCluster, GetCharPositionLineFormatsFightingYouLine)
{
    DisplayClusterContext context;
    context.target.specials.position = POSITION_FIGHTING;
    context.target.specials.fighting = &context.viewer;
    buf[0] = '\0';

    get_char_position_line(&context.viewer, &context.target, buf);

    EXPECT_STREQ(buf, " is here, fighting YOU!");
}

// POSITION_FIGHTING against a third character in the same room -- pins the
// buf-aliasing quirk the WAVE 3 TASK 9 SWEEP comment documents: this branch
// strcat()s into the global `buf' directly instead of the `str' parameter,
// which only produces correct output because `str' IS `buf' at every real
// call site (reproduced here by passing `buf' itself as `str').
TEST(ActInfoDisplayCluster, GetCharPositionLineFormatsFightingSomeoneElseLine)
{
    DisplayClusterContext context;
    char_data enemy {};
    enemy.player.name = const_cast<char*>("Aragorn");
    enemy.player.race = RACE_HUMAN;

    context.target.specials.position = POSITION_FIGHTING;
    context.target.specials.fighting = &enemy;
    buf[0] = '\0';

    get_char_position_line(&context.viewer, &context.target, buf);

    EXPECT_STREQ(buf, " is here, fighting Aragorn.");
}

// POSITION_FIGHTING with a nil fighting pointer.
TEST(ActInfoDisplayCluster, GetCharPositionLineFormatsFightingThinAirLine)
{
    DisplayClusterContext context;
    context.target.specials.position = POSITION_FIGHTING;
    context.target.specials.fighting = nullptr;
    buf[0] = '\0';

    get_char_position_line(&context.viewer, &context.target, buf);

    EXPECT_STREQ(buf, " is here struggling with thin air.");
}

// An out-of-range position value pins the switch's default branch.
TEST(ActInfoDisplayCluster, GetCharPositionLineFormatsDefaultFloatingLine)
{
    DisplayClusterContext context;
    context.target.specials.position = POSITION_STANDING + 1;
    buf[0] = '\0';

    get_char_position_line(&context.viewer, &context.target, buf);

    EXPECT_STREQ(buf, " is floating here.");
}

// ---------------------------------------------------------------------------
// get_char_flag_line (act_info.cpp:648) -- no sprintf of its own (pure
// strcat()); pinned as the aliasing web's regression net. Representative
// hide/invisible/linkless combos per the task brief, not every one of this
// function's dozen flag branches -- the rest need extra process-singleton or
// race-table bootstrap unrelated to this task's actual sprintf sites (see
// the suite comment above).
// ---------------------------------------------------------------------------

TEST(ActInfoDisplayCluster, GetCharFlagLineFormatsEmptyStringWhenNoFlagsSet)
{
    DisplayClusterContext context;
    buf[0] = '\0';

    get_char_flag_line(&context.viewer, &context.target, buf);

    EXPECT_STREQ(buf, "");
}

TEST(ActInfoDisplayCluster, GetCharFlagLineFormatsHidingFlag)
{
    DisplayClusterContext context;
    SET_BIT(context.target.specials.affected_by, AFF_HIDE);
    buf[0] = '\0';

    get_char_flag_line(&context.viewer, &context.target, buf);

    EXPECT_STREQ(buf, " (hiding)");
}

TEST(ActInfoDisplayCluster, GetCharFlagLineFormatsInvisibleFlag)
{
    DisplayClusterContext context;
    SET_BIT(context.target.specials.affected_by, AFF_INVISIBLE);
    buf[0] = '\0';

    get_char_flag_line(&context.viewer, &context.target, buf);

    EXPECT_STREQ(buf, " (invisible)");
}

TEST(ActInfoDisplayCluster, GetCharFlagLineFormatsHidingAndInvisibleCombo)
{
    DisplayClusterContext context;
    SET_BIT(context.target.specials.affected_by, AFF_HIDE);
    SET_BIT(context.target.specials.affected_by, AFF_INVISIBLE);
    buf[0] = '\0';

    get_char_flag_line(&context.viewer, &context.target, buf);

    EXPECT_STREQ(buf, " (hiding) (invisible)");
}

// Linkless: a non-NPC target with no live descriptor (DisplayClusterContext
// defaults `target.desc' to an "online" descriptor; this test overrides it).
TEST(ActInfoDisplayCluster, GetCharFlagLineFormatsLinklessFlagWhenTargetHasNoDescriptor)
{
    DisplayClusterContext context;
    context.target.desc = nullptr;
    buf[0] = '\0';

    get_char_flag_line(&context.viewer, &context.target, buf);

    EXPECT_STREQ(buf, " (linkless)");
}

// ---------------------------------------------------------------------------
// show_char_to_char (act_info.cpp:902) -- pins all four of this task's
// pragma-guarded sprintf() sites: the long-descr/pos-line "no title" and
// "with title" branches (act_info.cpp:938/941), and the caller-supplied-
// pos_line branch (act_info.cpp:963); plus the plain long_descr branch (no
// sprintf, included as a baseline sanity check that the aliasing web still
// works end to end). mode 1's sprintf (act_info.cpp:1013) is NOT pinned here
// -- see the suite comment above for why its bytes are provably
// unobservable.
// ---------------------------------------------------------------------------

TEST(ActInfoDisplayCluster, ShowCharToCharFormatsLongDescriptionWhenPositionMatchesDefault)
{
    DisplayClusterContext context;
    context.target.player.long_descr = const_cast<char*>("A weary traveler rests here.\r\n");
    context.target.specials.position = POSITION_STANDING;
    context.target.specials.default_pos = POSITION_STANDING;

    show_char_to_char(&context.target, &context.viewer, 0);

    EXPECT_STREQ(context.viewer_descriptor.output, "A weary traveler rests here.\r\n\n\r");
}

TEST(ActInfoDisplayCluster, ShowCharToCharFormatsNoTitleLineForNpcWithoutLongDescr)
{
    DisplayClusterContext context;
    SET_BIT(context.target.specials2.act, MOB_ISNPC);
    context.target.player.short_descr = const_cast<char*>("a wandering hermit");
    context.target.specials.position = POSITION_STANDING;

    show_char_to_char(&context.target, &context.viewer, 0);

    EXPECT_STREQ(context.viewer_descriptor.output, "A wandering hermit is standing here.\n\r");
}

TEST(ActInfoDisplayCluster, ShowCharToCharFormatsTitleLineForPcWithoutLongDescr)
{
    DisplayClusterContext context;
    context.target.player.name = const_cast<char*>("Bob");
    context.target.player.title = const_cast<char*>("the Wanderer");
    context.target.specials.position = POSITION_STANDING;

    show_char_to_char(&context.target, &context.viewer, 0);

    EXPECT_STREQ(context.viewer_descriptor.output, "Bob the Wanderer is standing here.\n\r");
}

TEST(ActInfoDisplayCluster, ShowCharToCharFormatsSuppliedPosLineForRider)
{
    DisplayClusterContext context;
    context.target.player.name = const_cast<char*>("Bob");

    show_char_to_char(
        &context.target, &context.viewer, 0, const_cast<char*>(" is riding a horse"));

    EXPECT_STREQ(context.viewer_descriptor.output, "Bob is riding a horse\n\r");
}

// ---------------------------------------------------------------------------
// list_char_to_char (act_info.cpp:1044) -- no sprintf of its own; a single
// integration pin confirming its should_show/IS_RIDDEN dispatch reaches
// show_char_to_char correctly (already pinned in isolation above) and skips
// the viewer themselves.
// ---------------------------------------------------------------------------

TEST(ActInfoDisplayCluster, ListCharToCharSkipsViewerAndShowsOtherRoomOccupants)
{
    DisplayClusterContext context;
    context.target.player.long_descr = const_cast<char*>("A quiet onlooker stands here.\r\n");
    context.target.specials.position = POSITION_STANDING;
    context.target.specials.default_pos = POSITION_STANDING;

    list_char_to_char(context.test_world.room().people, &context.viewer, 0);

    EXPECT_STREQ(context.viewer_descriptor.output, "A quiet onlooker stands here.\r\n\n\r");
}

// ---------------------------------------------------------------------------
// show_mount_to_char (act_info.cpp:719) -- no sprintf of its own; pinned as
// the aliasing web's regression net (it accumulates directly into the
// global `buf', same as show_char_to_char, and delegates to it for the
// "riderless mount" case).
// ---------------------------------------------------------------------------

TEST(ActInfoDisplayCluster, ShowMountToCharFormatsSingleSelfRiderLine)
{
    DisplayClusterContext context;
    char_data mount {};
    clear_char(&mount, MOB_VOID);
    ScopedClearCharFields mount_cleanup { mount };
    // clear_char() sets in_room to NOWHERE unconditionally; CAN_SEE()'s
    // "different room + NPC target" early-out (utility.cpp) treats NOWHERE
    // as a room mismatch against the viewer's room 0 and returns false
    // before HOLYLIGHT ever gets consulted, so this must be re-homed to
    // room 0 same as DisplayClusterContext does for viewer/target.
    mount.in_room = 0;
    SET_BIT(mount.specials2.act, MOB_ISNPC);
    mount.player.short_descr = const_cast<char*>("a horse");

    mount.mount_data.rider = &context.viewer;
    mount.mount_data.rider_number = 9001;
    context.viewer.mount_data.next_rider = nullptr;
    context.viewer.mount_data.next_rider_number = 0;
    set_char_exists(9001);

    const std::array<char, 11> single_rider_text {
        ' ', 'r', 'i', 'd', 'i', 'n', 'g', ' ', 'o', 'n', ' '
    };
    const std::array<char, 19> multiple_rider_text {
        ' ', 'r', 'i', 'd', 'i', 'n', 'g', ' ', 'o', 'n', ' ', '\0',
        'i', 'g', 'n', 'o', 'r', 'e', 'd'
    };
    show_mount_to_char(&mount, &context.viewer,
        std::string_view(single_rider_text.data(), single_rider_text.size()),
        std::string_view(multiple_rider_text.data(), multiple_rider_text.size()), FALSE);

    remove_char_exists(9001);

    EXPECT_STREQ(context.viewer_descriptor.output, "You are riding on a horse.\n\r");
}

TEST(ActInfoDisplayCluster, ShowMountToCharUsesBoundedSingleRiderText)
{
    DisplayClusterContext context;
    context.target.player.name = const_cast<char*>("Bob");

    char_data mount {};
    clear_char(&mount, MOB_VOID);
    ScopedClearCharFields mount_cleanup { mount };
    mount.in_room = 0;
    SET_BIT(mount.specials2.act, MOB_ISNPC);
    mount.player.short_descr = const_cast<char*>("a horse");
    mount.mount_data.rider = &context.target;
    mount.mount_data.rider_number = 9002;
    context.target.mount_data.next_rider = nullptr;
    context.target.mount_data.next_rider_number = 0;
    set_char_exists(9002);

    const std::array<char, 23> single_rider_text {
        ' ', 'r', 'i', 'd', 'i', 'n', 'g', ' ', 'b', 'e', 's', 'i', 'd', 'e', ' ', '\0',
        'i', 'g', 'n', 'o', 'r', 'e', 'd'
    };
    show_mount_to_char(&mount, &context.viewer,
        std::string_view(single_rider_text.data(), single_rider_text.size()), " unused ", FALSE);

    EXPECT_STREQ(context.viewer_descriptor.output, "Bob is riding beside a horse.\n\r");

    reset_capturing_descriptor(context.viewer_descriptor, &context.viewer);
    const std::string oversized_rider_text(MAX_STRING_LENGTH + 100, 'x');
    show_mount_to_char(
        &mount, &context.viewer, oversized_rider_text, " unused ", FALSE);

    remove_char_exists(9002);

    EXPECT_LT(strlen(context.viewer_descriptor.output), static_cast<std::size_t>(MAX_STRING_LENGTH));
}

TEST(GameplayBigBrother, TargetRedirectSignatureAcceptsBoundedText)
{
    using ExpectedSignature = char_data* (game_rules::big_brother::*)(
        char_data*, const char_data*, std::string_view) const;
    static_assert(std::is_same_v<decltype(static_cast<ExpectedSignature>(
                                     &game_rules::big_brother::get_valid_target)),
        ExpectedSignature>);

    const std::array<char, 12> argument {
        't', 'a', 'r', 'g', 'e', 't', '\0', 'o', 't', 'h', 'e', 'r'
    };
    EXPECT_EQ(game_rules::big_brother::instance().get_valid_target(
                  nullptr, nullptr, std::string_view(argument.data(), argument.size())),
        nullptr);
}

TEST(ActInfoDisplayCluster, ShowMountToCharDelegatesToShowCharToCharWhenNoVisibleRiders)
{
    DisplayClusterContext context;
    char_data mount {};
    clear_char(&mount, MOB_VOID);
    ScopedClearCharFields mount_cleanup { mount };
    // See the self-rider test above: clear_char() leaves in_room at NOWHERE.
    mount.in_room = 0;
    SET_BIT(mount.specials2.act, MOB_ISNPC);
    mount.player.short_descr = const_cast<char*>("a riderless pony");
    mount.specials.position = POSITION_STANDING;
    mount.specials.default_pos = POSITION_STANDING;

    show_mount_to_char(&mount, &context.viewer, " riding on ", " riding on ", FALSE);

    EXPECT_STREQ(context.viewer_descriptor.output, "A riderless pony is standing here.\n\r");
}

// ---------------------------------------------------------------------------
// show_room_affection (act_info.cpp:1072) -- pins both of this task's
// pragma-guarded sprintf() sites: the self-referencing ROOMAFF_SPELL "stat
// room" line (mode 1; also -Wrestrict, `str' aliases its own %s source) and
// the non-self-referencing default-case line (mode 1). `location = -1' keeps
// both independent of the un-booted skills[] table (this test binary never
// runs boot_db()), same technique as shape_format_tests.cpp's
// ShapeRoom.ListRoomFormatsAllFieldsWithNoExitsAndNegativeAffection.
// ---------------------------------------------------------------------------

TEST(ActInfoDisplayCluster, ShowRoomAffectionFormatsSelfReferencingSpellStatLine)
{
    char str[256];
    strcpy(str, "Affections:\n\r");

    affected_type affection {};
    affection.type = ROOMAFF_SPELL;
    affection.location = -1;
    affection.modifier = -5;
    affection.duration = 10;
    affection.bitvector = 0;

    show_room_affection(str, &affection, 1);

    EXPECT_STREQ(str, "Affections:\n\r Spell none(-1) level -5, 10hrs, sets <NONE>.\r\n");
}

TEST(ActInfoDisplayCluster, ShowRoomAffectionFormatsUnknownAffectTypeDefaultLine)
{
    char str[256];
    strcpy(str, "stale-data-overwritten");

    affected_type affection {};
    affection.type = 99; // neither ROOMAFF_SPELL nor ROOMAFF_EXIT

    show_room_affection(str, &affection, 1);

    EXPECT_STREQ(str, "Unknown room affect (99).\n\r");
}

// ---------------------------------------------------------------------------
// show_room_weather (act_info.cpp:1168) -- pins this task's remaining
// pragma-guarded (also -Wrestrict) self-referencing sprintf() site: `str'
// aliases its own %s source, same class as show_room_affection's stat line
// above.
// ---------------------------------------------------------------------------

TEST(ActInfoDisplayCluster, ShowRoomWeatherAppendsSelfReferencingSnowLine)
{
    DisplayClusterContext context;
    context.test_world.room().sector_type = 0;
    ScopedSnow snow(0, 1);

    char str[64];
    strcpy(str, "Weather: ");

    show_room_weather(str, &context.viewer);

    EXPECT_STREQ(str, "Weather: Snow lies upon the ground.\n\r");
}

TEST(ActInfoDisplayCluster, ShowRoomWeatherLeavesStringUnchangedWhenNoSnow)
{
    DisplayClusterContext context;
    context.test_world.room().sector_type = 0;
    ScopedSnow snow(0, 0);

    char str[64];
    strcpy(str, "Weather: ");

    show_room_weather(str, &context.viewer);

    EXPECT_STREQ(str, "Weather: ");
}

// ---------------------------------------------------------------------------
// do_look (act_info.cpp:1223) case 8 ("look", no argument) -- exit_mark[]
// table (act_info.cpp:1204). Pins this task's remaining pragma-guarded
// sprintf() site: a runtime-selected format string from a small, file-local,
// fully-enumerable 7-entry table (unlike add_prompt's prompt_text[]/
// prompt_hit[]/etc., which are large tables in a DIFFERENT file -- see this
// task's report for the reconciliation of the "same dynamic-format-string
// class as add_prompt" comment this task's conversion supersedes).
// exit_choice 1 (the plain, unmarked exit) is already pinned by
// ActInfoPerception.DoExamineWithoutTargetRendersRoomViaLookExamDelegation
// above; the six tests below cover the remaining exit_choice values 0-6
// (0/2/3/4 via a single self-looping room+door, 5/6 via a two-room fixture so
// SUN_PENALTY's own-room gate and IS_SUNLIT_EXIT/IS_SHADOWY_EXIT's
// target-room gate can be independently controlled). None of these fixtures
// sets PRF_SPAM, so (unlike the do_examine-delegated pin above) no room
// description line follows the exits line.
// ---------------------------------------------------------------------------

TEST(ActInfoDisplayCluster, DoLookCaseEightExitMarkHidesHiddenClosedDoorFromMortal)
{
    RoomWithExitContext context;
    context.exit.exit_info = EX_ISDOOR | EX_CLOSED | EX_ISHIDDEN;

    do_look(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_STREQ(context.descriptor.output, "The Testing Meadow    Exits are:\n\r");
}

TEST(ActInfoDisplayCluster, DoLookCaseEightExitMarkShowsClosedDoorInParens)
{
    RoomWithExitContext context;
    context.exit.exit_info = EX_ISDOOR | EX_CLOSED;

    do_look(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_STREQ(context.descriptor.output, "The Testing Meadow    Exits are: (N)\n\r");
}

TEST(ActInfoDisplayCluster, DoLookCaseEightExitMarkShowsHiddenClosedDoorAsterisksForImmortal)
{
    RoomWithExitContext context;
    context.character.player.level = LEVEL_GOD;
    context.exit.exit_info = EX_ISDOOR | EX_CLOSED | EX_ISHIDDEN;

    do_look(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_STREQ(context.descriptor.output, "The Testing Meadow    Exits are: *N*\n\r");
}

TEST(ActInfoDisplayCluster, DoLookCaseEightExitMarkShowsNowalkBracesForImmortal)
{
    RoomWithExitContext context;
    context.character.player.level = LEVEL_GOD;
    context.exit.exit_info = EX_NOWALK;

    do_look(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_STREQ(context.descriptor.output, "The Testing Meadow    Exits are: {N}\n\r");
}

TEST(ActInfoDisplayCluster, DoLookCaseEightExitMarkShowsSunlitMarkerForOrc)
{
    ExitMarkTwoRoomContext context;
    context.character.player.race = RACE_ORC;
    ScopedSunlight sunlight(SUN_LIGHT);

    do_look(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_STREQ(context.descriptor.output, "The Testing Meadow    Exits are: #N#\n\r");
}

TEST(ActInfoDisplayCluster, DoLookCaseEightExitMarkShowsShadowyMarkerForOrc)
{
    ExitMarkTwoRoomContext context;
    context.character.player.race = RACE_ORC;
    world[1].room_flags = SHADOWY;
    ScopedSunlight sunlight(SUN_LIGHT);

    do_look(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_STREQ(context.descriptor.output, "The Testing Meadow    Exits are: %N%\n\r");
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

TEST(ActInfoWorldSocial, DoHelpTreatsMissingGlobalHelpTextAsNoOutput)
{
    SoloCharacterContext context;
    char* previous_help = help;
    help = nullptr;
    char argument[] = "";

    do_help(&context.character, argument, nullptr, 0, 0);

    help = previous_help;
    EXPECT_STREQ(context.descriptor.output, "");
}

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

// ---------------------------------------------------------------------------
// Characterization tests for Phase 4 Wave 3 Task 4 (Chunk I4 --
// act_info.cpp's object-identification family: do_compare, do_stat,
// do_exploits/print_exploits, do_food_display, do_light_display,
// do_flag_values_display, do_weapon_display, do_identify_object,
// do_details). Suite ActInfoObjectId. Same binding pattern as Tasks 1-3:
// these pin CURRENT byte-for-byte output -- confirmed passing against the
// pre-conversion source -- before this chunk's sprintf sites convert to
// std::format/std::string composition, and green again after. This is
// act_info.cpp's LAST chunk -- after Task 4's transform the file is
// sprintf/strcpy/strcat-free (Task 9 verifies).
//
// Deliberately NOT unit-tested here (documented exclusion, not an
// oversight): do_details (act_info.cpp) contains no sprintf/strcpy/strcat
// call of its own -- every branch already composes via std::string
// (extra_specialization_data::to_string / damage_details::
// get_damage_report / group_data::get_damage_report) and sends via
// send_to_char(std::string::c_str()). One test below still pins its
// default/no-target reply for chunk completeness, but there is nothing for
// this task's transform to touch in this function.

TEST(ActInfoObjectId, DoCompareReportsMissingFirstObject)
{
    SoloCharacterContext context;

    do_compare(&context.character, const_cast<char*>("sword dagger"), nullptr, 0, 0);

    EXPECT_STREQ(context.descriptor.output, "You don't seem to have any sword.\n\r");
}

// do_compare: lev = obj2.level - obj1.level < -10 pins the "much better"
// verdict branch (act_info.cpp do_compare).
TEST(ActInfoObjectId, DoCompareFormatsMuchBetterVerdictForLargeLevelGap)
{
    RoomCharacterContext context;

    obj_data sword {};
    sword.name = const_cast<char*>("sword");
    sword.short_description = const_cast<char*>("a gleaming sword");
    sword.obj_flags = builders::ObjFlagDataBuilder().setLevel(50).build();
    // ObjFlagDataBuilder value-initializes its backing struct (fixed after
    // the Wave 3 finalization-CI MSVC 0xCC-fill failure -- see the header),
    // so builder-untouched fields are deterministically zero now. The
    // explicit zero below is kept as a local, self-documenting guard for
    // the exact field this test's path reads: get_obj_in_list_vis's
    // CAN_SEE_OBJ() visibility gate checks extra_flags's ITEM_INVISIBLE
    // bit, and a non-zero value here silently empties the comparison
    // output (this bit us as a test-order-dependent flake before the
    // builder fix).
    sword.obj_flags.extra_flags = 0;

    obj_data dagger {};
    dagger.name = const_cast<char*>("dagger");
    dagger.short_description = const_cast<char*>("a rusty dagger");
    dagger.obj_flags = builders::ObjFlagDataBuilder().setLevel(1).build();
    dagger.obj_flags.extra_flags = 0;

    dagger.next_content = context.character.carrying;
    sword.next_content = &dagger;
    context.character.carrying = &sword;

    do_compare(&context.character, const_cast<char*>("sword dagger"), nullptr, 0, 0);

    EXPECT_STREQ(context.descriptor.output, "a gleaming sword seems much better than a rusty dagger.\n\r");
}

// do_compare: lev within [-3, 3] pins the "about the same" verdict branch.
TEST(ActInfoObjectId, DoCompareFormatsAboutSameVerdictForSmallLevelGap)
{
    RoomCharacterContext context;

    obj_data sword {};
    sword.name = const_cast<char*>("sword");
    sword.short_description = const_cast<char*>("a gleaming sword");
    sword.obj_flags = builders::ObjFlagDataBuilder().setLevel(20).build();
    // See the sibling test above: explicit zero kept for the one field
    // CAN_SEE_OBJ() reads (deterministic since the builder fix).
    sword.obj_flags.extra_flags = 0;

    obj_data dagger {};
    dagger.name = const_cast<char*>("dagger");
    dagger.short_description = const_cast<char*>("a rusty dagger");
    dagger.obj_flags = builders::ObjFlagDataBuilder().setLevel(21).build();
    dagger.obj_flags.extra_flags = 0;

    dagger.next_content = context.character.carrying;
    sword.next_content = &dagger;
    context.character.carrying = &sword;

    do_compare(&context.character, const_cast<char*>("sword dagger"), nullptr, 0, 0);

    EXPECT_STREQ(context.descriptor.output, "a gleaming sword and a rusty dagger seems about the same.\n\r");
}

// do_stat: the "!wtl" branch (a plain "stat" with no active wizstat target)
// pins the fatigue/willpower/statistic-sum/six-attribute-pairs line
// (act_info.cpp do_stat) -- every number is two digits so none of the
// format's "%2d" fields introduce a padding space, keeping this a plain
// value substitution.
TEST(ActInfoObjectId, DoStatFormatsFatigueWillpowerAndStatisticsLine)
{
    SoloCharacterContext context;
    context.character.player.level = 10; // >= 6, past do_stat's "too young" gate

    context.character.specials.mental_delay = 40; // / PULSE_MENTAL_FIGHT(8) == 5
    context.character.points.willpower = 55;

    context.character.abilities.str = 80;
    context.character.abilities.intel = 70;
    context.character.abilities.wil = 60;
    context.character.abilities.dex = 50;
    context.character.abilities.con = 90;
    context.character.abilities.lea = 40;

    context.character.tmpabilities.str = 75;
    context.character.tmpabilities.intel = 65;
    context.character.tmpabilities.wil = 55;
    context.character.tmpabilities.dex = 45;
    context.character.tmpabilities.con = 85;
    context.character.tmpabilities.lea = 35;

    do_stat(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_STREQ(context.descriptor.output,
        "Your fatigue is 5; Your willpower is 55; Your statistic sum is 390\n\r"
        "Your statistics are\n\r"
        "Str: 75/80, Int: 65/70, Wil: 55/60, Dex: 45/50, Con: 85/90, Lea: 35/40.\n\r");
}

// do_exploits/print_exploits: a single legacy-binary .exploits record on
// disk (root "." resolves into a throwaway TemporaryDirectory via
// ScopedWorkingDirectory) pins the header line plus one EXPLOIT_PK row and
// the trailing totals line. Asserted with strstr rather than a full
// EXPECT_STREQ: the per-row "%-39s" left-justify padding (act_info.cpp
// print_exploits) is exact but not the point of this pin -- the header,
// the composed row text, and the totals line are.
TEST(ActInfoObjectId, DoExploitsFormatsHeaderAndOneRow)
{
    TemporaryDirectory temp_directory;
    ASSERT_TRUE(std::filesystem::create_directories(temp_directory.path() + "/exploits/A-E"));
    ScopedWorkingDirectory working_directory(temp_directory.path());

    exploit_record record {};
    record.type = EXPLOIT_PK;
    std::strncpy(record.chtime, "Mon Jan  1 00:00:00 2024", sizeof(record.chtime) - 1);
    std::strncpy(record.chVictimName, "Sauron", sizeof(record.chVictimName) - 1);
    record.iKillerLevel = 20;
    record.iVictimLevel = 15;

    const std::string exploits_path = account::legacy_exploits_file_path(".", "aragorn");
    FILE* file = std::fopen(exploits_path.c_str(), "wb");
    ASSERT_NE(file, nullptr);
    ASSERT_EQ(std::fwrite(&record, sizeof(record), 1, file), 1u);
    ASSERT_EQ(std::fclose(file), 0);

    SoloCharacterContext context;
    context.character.player.name = const_cast<char*>("aragorn");

    do_exploits(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_TRUE(strstr(context.descriptor.output,
                    "Exploits for aragorn\n\r"
                    "Numbers in brackets indicate (your,their) level at time of a kill\n\r\n\r")
        != nullptr)
        << context.descriptor.output;
    EXPECT_TRUE(strstr(context.descriptor.output, "Jan  1, 24: Killed Sauron (20,15)") != nullptr)
        << context.descriptor.output;
    EXPECT_TRUE(strstr(context.descriptor.output,
                    "Total: 1 pkill, 0 pdeaths, 0 mobdeaths, 0 notes.\n\r\n\r")
        != nullptr)
        << context.descriptor.output;
}

// do_food_display: pins the fullness-hours message-array line (act_info.cpp)
// for a builder-made food object whose value[3] quality flag is 0 (the
// "wholesome" branch). ObjFlagDataBuilder has no dedicated food-value
// setter -- value[0] (make_full) is set via setObCoef() (the same
// value[0] slot the builder's weapon setter also targets; obj_flag_data's
// value[] array is reused per item type) and value[3] is poked directly,
// matching object_utils_tests.cpp's established convention of setting
// fields the builder doesn't expose directly on the built struct.
TEST(ActInfoObjectId, DoFoodDisplayFormatsWholesomeQualityLine)
{
    SoloCharacterContext context;
    obj_data food {};
    food.short_description = const_cast<char*>("a piece of bread");
    food.obj_flags = builders::ObjFlagDataBuilder().setObCoef(1).build(); // value[0] == 1 hour
    food.obj_flags.value[3] = 0; // wholesome

    do_food_display(&context.character, &food);

    EXPECT_STREQ(context.descriptor.output,
        "a piece of bread is barely a morsel of food, and will\r\n"
        "do little to aid against the pangs of hunger. It is also of a wholesome quality.\r\n");
}

// do_food_display: value[3] != 0 pins the "less than wholesome" branch.
TEST(ActInfoObjectId, DoFoodDisplayFormatsLessThanWholesomeQualityLine)
{
    SoloCharacterContext context;
    obj_data food {};
    food.short_description = const_cast<char*>("a piece of bread");
    food.obj_flags = builders::ObjFlagDataBuilder().setObCoef(1).build();
    food.obj_flags.value[3] = 1; // not wholesome

    do_food_display(&context.character, &food);

    EXPECT_STREQ(context.descriptor.output,
        "a piece of bread is barely a morsel of food, and will\r\n"
        "do little to aid against the pangs of hunger. However it seems to be of"
        " a less than wholesome quality.\r\n");
}

// do_light_display: pins the duration-range message-array line
// (act_info.cpp). value[2] (duration_range) is set via setBulk() -- the
// builder's value[2] slot is reused across item types, same as the
// food-display test's value[0]/setObCoef() reuse above.
TEST(ActInfoObjectId, DoLightDisplayFormatsDurationMessage)
{
    SoloCharacterContext context;
    obj_data lantern {};
    lantern.obj_flags = builders::ObjFlagDataBuilder().setBulk(3).build(); // value[2] == 3

    do_light_display(&context.character, &lantern);

    EXPECT_STREQ(context.descriptor.output,
        "This source of light is extremely weak, and will not last very long.\r\n");
}

// do_flag_values_display: an ITEM_WEAPON object pins the Offensive
// Bonus/Parry Bonus/Bulk value_array row (act_info.cpp), including both the
// "%-15s"-style pre-padded label strings AND the positive-vs-negative value
// spacing branch ("\t  %d.\r\n" vs "\t %d.\r\n") -- Parry Bonus is set
// negative specifically to pin that second branch.
TEST(ActInfoObjectId, DoFlagValuesDisplayFormatsWeaponBonusRowsWithNegativeSpacing)
{
    SoloCharacterContext context;
    obj_data weapon {};
    weapon.obj_flags = builders::ObjFlagDataBuilder().setObCoef(10).setParryCoef(-3).setBulk(2).build();
    weapon.obj_flags.type_flag = ITEM_WEAPON;

    do_flag_values_display(&context.character, &weapon);

    EXPECT_STREQ(context.descriptor.output,
        "Offensive Bonus\t  10.\r\n"
        "Parry Bonus    \t -3.\r\n"
        "Bulk           \t  2.\r\n");
}

// do_weapon_display: pins the weapon-type name line plus the Damage Rating
// line (act_info.cpp) for a builder-constructed, unowned (carried_by ==
// nullptr) slashing weapon -- get_weapon_damage(obj_data*) (utils.h/
// utility.cpp:434) is the live pointer overload every caller in this file
// (act_wiz.cpp, fight.cpp, ranger.cpp) binds to; Wave 2 Task 5 resolved the
// "twin overload" question and found no live `const obj_data&` caller left
// to worry about, so this call site needed no adaptation of its own.
//
// fp-interiors Task 2c (fpi-census.md's B13 boundary): get_weapon_damage()'s
// dam_coef chain now computes in double with a single rots::fp::to_game_int
// rounding at return, instead of per-step integer truncation. This is an
// indirect caller the T0 census's grep (direct calls to the four converted
// functions by name in src/tests/) did not enumerate -- do_weapon_display()
// calls get_weapon_damage() internally, one level removed from this test's
// own assertion. The 42/10 -> 44/10 shift for this exact vector is the same
// documented rounding-drift class every other converted formula in this
// wave produces (verified: pre-conversion this test asserted 42; the
// live-driven B13 conversion changes it to 44), not a bug in the
// conversion -- repointed here per the wave's standing drift-documentation
// practice (see fpi-task-2c-report.md).
TEST(ActInfoObjectId, DoWeaponDisplayFormatsWeaponTypeAndDamageRatingLine)
{
    SoloCharacterContext context;
    obj_data weapon {};
    weapon.obj_flags = builders::ObjFlagDataBuilder()
                           .setWeaponType(game_types::WT_SLASHING)
                           .setObCoef(10)
                           .setParryCoef(5)
                           .setBulk(3)
                           .setLevel(20)
                           .setWeight(50)
                           .build();
    weapon.obj_flags.type_flag = ITEM_WEAPON;

    do_weapon_display(&context.character, &weapon);

    EXPECT_STREQ(context.descriptor.output,
        "The weapon you hold is a slashing weapon.\r\n"
        "\n\rDamage Rating \t   44/10.\r\n");
}

// do_identify_object: end-to-end pin for an ITEM_TREASURE object (a type
// with no do_light_display/do_food_display/do_weapon_display delegation and
// an empty value_array row, keeping do_flag_values_display's contribution
// to nothing) with a null action_description -- pins the ternary
// null-guard fallback (act_info.cpp; kept as a ternary per the transform
// idiom catalog, NOT wrapped in nz(), since the old code already guarded
// it) and the sprintbit-composed wear/extra-flags lines (sprintbit itself
// is out of scope this wave -- transform idiom catalog item 7).
TEST(ActInfoObjectId, DoIdentifyObjectFormatsDescriptionMaterialWeightAndFlagLines)
{
    SoloCharacterContext context;
    obj_data idol {};
    idol.short_description = const_cast<char*>("a golden idol");
    idol.action_description = nullptr;
    idol.obj_flags = builders::ObjFlagDataBuilder().setMaterial(0).setWeight(250).build();
    idol.obj_flags.type_flag = ITEM_TREASURE;
    // ObjFlagDataBuilder value-initializes its backing struct (fixed after
    // the Wave 3 finalization-CI MSVC 0xCC-fill failure -- see the header),
    // so wear_flags/extra_flags/value[] are deterministically zero. The
    // explicit zeroes below stay as local documentation of the two fields
    // this test's sprintbit() lines pin.
    idol.obj_flags.wear_flags = 0;
    idol.obj_flags.extra_flags = 0;

    do_identify_object(&context.character, &idol);

    EXPECT_STREQ(context.descriptor.output,
        "   You feel certain the object you have is a golden idol. \r\n"
        "No object description, please report. \r\n \r\n"
        "This piece of treasure is made of the usual stuff, and weighs 2.5lbs.\r\n"
        "This piece of treasure can behas no additional attributes. \r\n"
        "\r\nThis item has no additional attributes. \r\n"
        "\r\n");
}

// do_identify_object: a null short_description pins the OTHER ternary
// null-guard fallback (act_info.cpp's first sprintf site).
TEST(ActInfoObjectId, DoIdentifyObjectFormatsMissingShortDescriptionFallback)
{
    SoloCharacterContext context;
    obj_data mystery {};
    mystery.short_description = nullptr;
    mystery.action_description = const_cast<char*>("glows.");
    mystery.obj_flags = builders::ObjFlagDataBuilder().setMaterial(0).setWeight(100).build();
    mystery.obj_flags.type_flag = ITEM_TREASURE;
    // See the sibling test above: explicit zeroes kept for the flag fields
    // this call reads via sprintbit() (deterministic since the builder fix).
    mystery.obj_flags.wear_flags = 0;
    mystery.obj_flags.extra_flags = 0;

    do_identify_object(&context.character, &mystery);

    EXPECT_TRUE(strstr(context.descriptor.output,
                    "You feel certain the object you have is No object description found,"
                    " please report. . \r\n")
        != nullptr)
        << context.descriptor.output;
}

// do_details: no sprintf/strcpy/strcat call of its own (see the suite-level
// comment above) -- this pins its default/no-target reply for chunk
// completeness rather than exercising any conversion.
TEST(ActInfoObjectId, DoDetailsFormatsAcceptedArgumentsWhenNoWaitListTarget)
{
    SoloCharacterContext context;

    do_details(&context.character, const_cast<char*>(""), nullptr, 0, 0);

    EXPECT_STREQ(context.descriptor.output,
        "Accepted arguments: spec, group, damage (optional: reset) \r\n");
}
