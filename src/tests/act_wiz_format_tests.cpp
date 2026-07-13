#include "../db.h"
#include "../handler.h"
#include "../interpre.h"
#include "../structs.h"
#include "../utils.h"
#include "../zone.h"
#include "test_char_cleanup.h"
#include "test_platform_compat.h"
#include "test_world.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>

// Characterization tests for Phase 4 Wave 3 Task 5 (Chunk W1 -- act_wiz.cpp
// inspection family: do_stat_room/do_stat_object/do_stat_character, the
// dispatchers/utilities that reach them (do_zone, do_wizstat, do_vstat,
// do_vnum), and the remaining standalone inspection commands do_date,
// do_uptime, do_last, do_findzone, do_top, do_show). Suite ActWizInspection
// below. Same binding pattern as Tasks 1-4: these pin the CURRENT
// byte-for-byte output -- confirmed passing against the pre-conversion
// source -- before this chunk's sprintf/strcat/strcpy sites convert to
// std::format/std::string composition, and green again after.
//
// Deliberately NOT unit-tested here (documented exclusions, not oversights):
//  - do_vnum (act_wiz.cpp:397): every branch is a literal send_to_char() --
//    no sprintf/strcpy/strcat call at all -- nothing for this task's
//    transform to touch. Exercised anyway (LightlyExercisesUsageMessage
//    below) purely for a smoke check since it's in this chunk's read range.
//  - do_wizstat (act_wiz.cpp:1071): pure dispatcher to
//    do_stat_room/do_stat_character/do_stat_object -- no format site of its
//    own (only literal send_to_char() "Stats on ..." messages). Its
//    dispatch targets are exercised directly via their own tests below.
//  - do_vstat (act_wiz.cpp:1364): same shape as do_wizstat -- reads a real
//    mobile/object via read_mobile()/read_object() (needs a fully loaded
//    mob_index/obj_index world file, out of reach for a unit fixture) and
//    then delegates to do_stat_character/do_stat_object, which are already
//    covered directly. No sprintf of its own.
//  - do_show's "stats" (case 4), "unused" (case 5), "death traps" (case 6),
//    "godrooms" (case 7), and "affected" (case 8) branches walk large
//    process-global structures (world/zone/character/object counts,
//    buf_switches/txt_block_counter/pkill_get_total()/memory_rec_counter,
//    the full room table, the live affected_list) that would require
//    reconstructing most of db.cpp's boot state to exercise deterministically
//    in a unit fixture; scripts/boot-golden.sh's real-boot smoke test is the
//    correct place these are covered (see Task 1's do_look precedent for the
//    same "deep world rendering" exclusion). The "zones"/"player"/"aliases"
//    branches below, and the top-level no-argument/usage rendering, ARE
//    directly fixture-reachable and are pinned.
//  - do_top's per-race sprintf(buf2, ...) branches (act_wiz.cpp:3995-4054)
//    are exercised only through the "human" race path below (representative
//    of the whole strncmp-chain, which is otherwise a long flat list of
//    textually-identical single-token buf2 formats); the remaining race
//    tokens are not each re-tested since they share the exact same format
//    string shape.

extern char buf[];
extern char buf1[];

ACMD(do_vnum);
ACMD(do_zone);
ACMD(do_findzone);
ACMD(do_top);
ACMD(do_last);
ACMD(do_date);
ACMD(do_uptime);
ACMD(do_show);
ACMD(do_at);
ACMD(do_goto);
ACMD(do_load);
ACMD(do_purge);
ACMD(do_zreset);
ACMD(do_force);
ACMD(do_advance);
ACMD(do_restore);
ACMD(do_invis);
ACMD(do_dc);
ACMD(do_wizlock);
ACMD(do_wizutil);
ACMD(do_wizset);
ACMD(do_delete);
ACMD(do_register);
ACMD(do_setfree);

// Phase 4 Wave 3 Task 8 (Chunk W4 -- act_wiz.cpp Wiz communication family).
ACMD(do_emote);
ACMD(do_send);
ACMD(do_echo);
ACMD(do_gecho);
ACMD(do_poofset);
ACMD(do_wiznet);

// Phase 4 Wave 3 Task 7 (Chunk W3 -- act_wiz.cpp player-administration
// family): restrict/global_release_flag/player_table/descriptor_list are
// process globals do_wizlock/do_setfree/do_delete/do_dc read and mutate
// directly, and top_of_p_table gates find_player_in_table()'s walk (do_delete).
// Deliberately NOT declared here: `wizlock_msg` itself -- its type changes
// from `char*` to `std::string` in this task's transform commit, and every
// ActWizPlayerAdmin test below observes its effect only indirectly (through
// do_wizlock's own SEND_TO_Q'd echo of it back to `ch`), so no test needs an
// extern of its own that would otherwise have to be edited in lockstep with
// the production type change.
extern int restrict;
extern const char* const wizlock_default;
extern int global_release_flag;
extern struct player_index_element* player_table;
extern int top_of_p_table;
extern struct descriptor_data* descriptor_list;

int find_target_room(struct char_data* ch, char* rawroomstr);
void do_stat_room(struct char_data* ch);
void do_stat_object(struct char_data* ch, struct obj_data* j);
void do_stat_character(struct char_data* ch, struct char_data* k);

void clear_char(struct char_data* ch, int mode);
void save_player(struct char_data* ch, int load_room, int index_pos);

extern struct index_data* mob_index;
extern struct index_data* obj_index;
extern struct char_data* character_list;
extern struct player_index_element* player_table;
extern int top_of_p_table;
extern time_t boot_time;
extern char* lastdeath;

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
// dependency -- mirrors act_info_format_tests.cpp's SoloCharacterContext
// (Phase 4 Wave 3 Task 2), duplicated per-file per this wave's convention.
// tmpabilities.str is set non-zero: get_real_OB()/get_real_dodge()/
// get_real_parry() (reached from do_stat_character) divide by
// get_bal_strength(), which is 0 for a fully-zeroed character -- x86 traps
// that as SIGFPE while ARM silently yields 0 (Task 2's lesson), so a zero
// strength here would pass on macOS and crash the i386/rots64 legs.
struct SoloCharacterContext {
    char_data character { };
    descriptor_data descriptor { };
    // Releases character.profs/skills/knowledge (clear_char() heap
    // allocations) at scope exit (Phase 5 T6 leak sweep).
    ScopedClearCharFields character_cleanup { character };
    // Returns descriptor.large_outbuf to bufpool at scope exit -- do_stat_
    // character's output routinely overflows descriptor.small_outbuf, which
    // promotes it to a heap-allocated large_outbuf block (Phase 5 T6 leak
    // sweep; see test_char_cleanup.h for the full rationale).
    ScopedDescriptorLargeOutbufReturn descriptor_large_outbuf_cleanup { descriptor };

    SoloCharacterContext()
    {
        clear_char(&character, MOB_VOID);
        reset_capturing_descriptor(descriptor, &character);
        character.desc = &descriptor;
        character.specials.position = POSITION_STANDING;
        character.player.race = RACE_HUMAN;
        character.player.level = LEVEL_IMPL;
        character.tmpabilities.str = 100; // see fixture comment: div-by-zero guard
    }
};

// A PC target for do_stat_character(ch, k): needs its own char_prof_data
// (char_data::profs is a pointer the production code always has pointed at
// a real allocation; GET_PROF_COOF/GET_PROF_LEVEL dereference it
// unconditionally for a non-NPC target) in addition to the same non-zero
// strength SoloCharacterContext needs, since do_stat_character also computes
// get_real_OB(k)/get_real_parry(k)/get_real_dodge(k) for the STAT TARGET, not
// just the viewer.
struct PcTargetContext {
    char_data character { };
    char_prof_data profs { };
    // Releases character.skills/knowledge (clear_char() heap allocations) at
    // scope exit; character's OWN heap-allocated profs is released right
    // after clear_char() below, before it gets overwritten with the stack
    // `profs` member above, so the destructor's RELEASE(character.profs) is
    // then a no-op on the (non-owned) stack pointer (Phase 5 T6 leak sweep).
    ScopedClearCharFields character_cleanup { character };

    PcTargetContext()
    {
        clear_char(&character, MOB_VOID);
        character_cleanup.release_profs_now();
        character.profs = &profs;
        character.player.race = RACE_HUMAN;
        character.player.level = 50;
        character.specials2.idnum = 4242;
        character.tmpabilities.str = 100;
        character.player.name = const_cast<char*>("aragorn");
    }
};

// An NPC target for do_stat_character(ch, k). char_data::nr defaults to 0
// after clear_char()'s placement-new zero-init, and IS_MOB() is
// `IS_NPC(ch) && (ch->nr > -1)` -- so ANY zeroed NPC fixture reads as a full
// mob (nr == 0), and do_stat_character's "Mob Spec-Proc"/alias/vnum lines
// then dereference mob_index[0] unconditionally. Every NPC fixture in this
// file therefore pairs with a ScopedMobIndexEntry(0, ...) below rather than
// leaving mob_index null.
struct NpcTargetContext {
    char_data character { };
    // Releases character.profs/skills/knowledge (clear_char() heap
    // allocations -- mode is MOB_VOID here despite the struct's name; NPC-
    // ness is set manually below) at scope exit (Phase 5 T6 leak sweep).
    ScopedClearCharFields character_cleanup { character };

    NpcTargetContext()
    {
        clear_char(&character, MOB_VOID);
        character.specials2.act = MOB_ISNPC;
        character.player.race = RACE_HUMAN;
        character.player.level = 20;
        character.player.short_descr = const_cast<char*>("a testing orc");
        character.player.name = const_cast<char*>("orc alias");
        character.tmpabilities.str = 100;
        character.nr = 0;
    }
};

// Points the process-global mob_index at a single fabricated entry for the
// duration of a test, restoring the previous global on scope exit --
// mirrors the ScopedPlayerTableEntry/ScopedDescriptorList RAII convention
// already established in act_wiz_tests.cpp (mirrored here per the task
// brief, not included across test files).
class ScopedMobIndexEntry {
public:
    ScopedMobIndexEntry(int virt_number, bool has_special_proc)
        : m_previous(mob_index)
    {
        mob_index = new index_data[1] { };
        mob_index[0].virt = virt_number;
        mob_index[0].number = 1;
        mob_index[0].func = has_special_proc ? &dummy_special : nullptr;
    }

    ~ScopedMobIndexEntry()
    {
        delete[] mob_index;
        mob_index = m_previous;
    }

private:
    static SPECIAL(dummy_special) { return 0; }

    index_data* m_previous;
};

// obj_index counterpart of ScopedMobIndexEntry, for do_stat_object's/
// do_stat_room's high-level-viewer VNum/RNum/SpecProc lines.
class ScopedObjIndexEntry {
public:
    ScopedObjIndexEntry(int virt_number, bool has_special_proc)
        : m_previous(obj_index)
    {
        obj_index = new index_data[1] { };
        obj_index[0].virt = virt_number;
        obj_index[0].number = 1;
        obj_index[0].func = has_special_proc ? &dummy_special : nullptr;
    }

    ~ScopedObjIndexEntry()
    {
        delete[] obj_index;
        obj_index = m_previous;
    }

private:
    static SPECIAL(dummy_special) { return 0; }

    index_data* m_previous;
};

// Points the process-global zone_table at a single fabricated zone for the
// duration of a test -- mirrors zone_tests.cpp's ScopedZoneTable (mirrored,
// not included, per this file's per-file-fixture convention). The sentinel
// owner (owner == 0) matches the production "owners == nullptr means
// no zone is loaded" walk in zone.cpp/act_wiz.cpp's Check_zone_authority.
class ScopedZoneTable {
public:
    explicit ScopedZoneTable(int zone_number = 0)
        : m_previous_zone_table(zone_table)
        , m_previous_top_of_zone_table(top_of_zone_table)
    {
        zone_table = new zone_data[1] { };
        zone_table[0].number = zone_number;
        zone_table[0].name = const_cast<char*>("The Testing Zone");
        zone_table[0].description = const_cast<char*>("A quiet test zone.\n\r");
        zone_table[0].map = const_cast<char*>("");
        zone_table[0].x = 3;
        zone_table[0].y = 7;
        zone_table[0].symbol = 'Z';
        zone_table[0].level = 0;
        zone_table[0].owners = &m_owner_sentinel;
        top_of_zone_table = 0;
    }

    zone_data& zone() { return zone_table[0]; }

    ~ScopedZoneTable()
    {
        delete[] zone_table;
        zone_table = m_previous_zone_table;
        top_of_zone_table = m_previous_top_of_zone_table;
    }

private:
    zone_data* m_previous_zone_table;
    int m_previous_top_of_zone_table;
    owner_list m_owner_sentinel { };
};

// A single standing, high-level PC in room 0 of a fresh single-room test
// world, paired with a ScopedZoneTable entry whose number matches room 0's
// zone (both default to 0) so Check_zone_authority's owner walk resolves
// (LEVEL_AREAGOD viewer bypasses the owner-name walk entirely, matching
// act_wiz.cpp:1012's `GET_LEVEL(ch) >= LEVEL_AREAGOD` clause).
struct RoomStatContext {
    ScopedZoneTable zone_table_scope;
    ScopedTestWorld test_world;
    char_data character { };
    descriptor_data descriptor { };
    char_data* original_people = nullptr;
    // Releases character.profs/skills/knowledge (clear_char() heap
    // allocations) at scope exit (Phase 5 T6 leak sweep).
    ScopedClearCharFields character_cleanup { character };

    RoomStatContext()
    {
        clear_char(&character, MOB_VOID);
        reset_capturing_descriptor(descriptor, &character);
        character.desc = &descriptor;
        character.specials.position = POSITION_STANDING;
        character.player.race = RACE_HUMAN;
        character.player.level = LEVEL_AREAGOD;
        character.player.name = const_cast<char*>("Gandalf"); // viewer is listed in "Chars present"

        original_people = test_world.room().people;
        // room_data::create_bulk() stamps this single-room world's only
        // index with EXTENSION_ROOM_HEAD (act_wiz.cpp's do_stat_room /
        // do_zone / Check_zone_authority compute their zone number as
        // rm->number / 100, and EXTENSION_ROOM_HEAD/100 doesn't match
        // ScopedZoneTable's zone 0) -- pin it to room 0 of zone 0 so it
        // resolves against this fixture's ScopedZoneTable entry.
        test_world.room().number = 0;
        // room_data's own constructor (db.cpp) only sets number/zone/level/
        // name/description/affected -- funct/room_flags/ex_description/
        // dir_option[] are left as indeterminate bytes from the underlying
        // `new room_data[]` allocation. A plain macOS build's allocator
        // happened to hand back zeroed pages every time (so `rm->funct`/
        // `rm->room_flags` read as null/0 "by luck"), but macOS ASan's
        // allocator does not: it surfaced as SpecProc reading "Exists"
        // instead of "No" under `-fsanitize=address` (Task 5 ASan gate).
        // Zeroing explicitly here removes the reliance on incidental
        // allocator behavior for every RoomStatContext-based test, and
        // guards do_stat_room's `for (i = 0; i < NUM_OF_DIRS; i++) if
        // (rm->dir_option[i])` loop against dereferencing garbage pointers.
        test_world.room().funct = nullptr;
        test_world.room().room_flags = 0;
        test_world.room().ex_description = nullptr;
        for (int direction = 0; direction < NUM_OF_DIRS; direction++)
            test_world.room().dir_option[direction] = nullptr;
        character.in_room = 0;
        character.next_in_room = nullptr;
        test_world.room().people = &character;
    }

    ~RoomStatContext()
    {
        test_world.room().people = original_people;
        character.in_room = NOWHERE;
    }
};

std::string strip_trailing_newline(const std::string& value)
{
    std::string result = value;
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
        result.pop_back();
    return result;
}

} // namespace

// ---------------------------------------------------------------------------
// do_stat_room (act_wiz.cpp:420)
// ---------------------------------------------------------------------------

TEST(ActWizInspection, StatRoomDeniesWhenViewerLacksZoneAuthority)
{
    RoomStatContext context;
    context.character.player.level = 1; // below LEVEL_AREAGOD
    context.character.specials2.idnum = 99; // doesn't match the owner below
    // Check_zone_authority walks this list until it finds owner == 0 (the
    // ScopedZoneTable sentinel, left untouched here); a non-zero-owner node
    // ahead of that sentinel exercises the "not one of the zone's owners"
    // denial path without ever dereferencing past the sentinel's null next.
    owner_list named_owner { };
    named_owner.owner = 4242; // a name that isn't the viewer's
    named_owner.next = context.zone_table_scope.zone().owners;
    context.zone_table_scope.zone().owners = &named_owner;
    do_stat_room(&context.character);
    EXPECT_EQ(std::string(context.descriptor.output), "You have no permissions in this zone.\n\r");
}

TEST(ActWizInspection, StatRoomFormatsTitleLine)
{
    RoomStatContext context;
    do_stat_room(&context.character);
    EXPECT_NE(std::string(context.descriptor.output).find("Room name: The Testing Meadow\n\r"),
        std::string::npos)
        << context.descriptor.output;
}

TEST(ActWizInspection, StatRoomFormatsZoneVnumRnumTypeLine)
{
    RoomStatContext context;
    context.test_world.room().sector_type = 0;
    do_stat_room(&context.character);
    const std::string output = context.descriptor.output;
    EXPECT_NE(output.find("Zone: [  0], VNum: [    0], RNum: [    0], Type: "), std::string::npos)
        << output;
}

TEST(ActWizInspection, StatRoomFormatsSpecProcFlagsLineWithNoFunctAndNoFlags)
{
    RoomStatContext context;
    do_stat_room(&context.character);
    EXPECT_NE(std::string(context.descriptor.output).find("SpecProc: No, Flags: <NONE>\n\r"),
        std::string::npos)
        << context.descriptor.output;
}

TEST(ActWizInspection, StatRoomFormatsLevelLightsLine)
{
    RoomStatContext context;
    context.test_world.room().level = 3;
    context.test_world.room().light = 2;
    do_stat_room(&context.character);
    EXPECT_NE(std::string(context.descriptor.output).find("Level: 3, No. of lights: 2\n\r"),
        std::string::npos)
        << context.descriptor.output;
}

TEST(ActWizInspection, StatRoomFormatsDescriptionNoneFallback)
{
    RoomStatContext context;
    std::free(context.test_world.room().description);
    context.test_world.room().description = nullptr;
    do_stat_room(&context.character);
    EXPECT_NE(std::string(context.descriptor.output).find("Description:\n\r  None.\n\r"),
        std::string::npos)
        << context.descriptor.output;
}

TEST(ActWizInspection, StatRoomFormatsExtraDescsLine)
{
    RoomStatContext context;
    extra_descr_data second_desc { };
    second_desc.keyword = const_cast<char*>("altar");
    second_desc.next = nullptr;
    extra_descr_data first_desc { };
    first_desc.keyword = const_cast<char*>("statue");
    first_desc.next = &second_desc;
    context.test_world.room().ex_description = &first_desc;
    do_stat_room(&context.character);
    context.test_world.room().ex_description = nullptr;
    EXPECT_NE(std::string(context.descriptor.output).find("Extra descs: statue altar\n\r"),
        std::string::npos)
        << context.descriptor.output;
}

TEST(ActWizInspection, StatRoomFormatsCharsPresentLineForSecondPc)
{
    RoomStatContext context;
    char_data bystander { };
    clear_char(&bystander, MOB_VOID);
    // Releases bystander.profs/skills/knowledge (clear_char() heap
    // allocations) at scope exit (Phase 5 T6 leak sweep).
    ScopedClearCharFields bystander_cleanup { bystander };
    bystander.player.name = const_cast<char*>("Legolas");
    bystander.in_room = 0;
    bystander.next_in_room = context.test_world.room().people;
    context.test_world.room().people = &bystander;
    do_stat_room(&context.character);
    context.test_world.room().people = &context.character;
    context.character.next_in_room = nullptr;
    const std::string output = context.descriptor.output;
    EXPECT_NE(output.find("Legolas(PC)"), std::string::npos) << output;
}

TEST(ActWizInspection, StatRoomFormatsCharsPresentLineForMob)
{
    RoomStatContext context;
    ScopedMobIndexEntry mob_index_entry(1234, false);
    char_data mob { };
    clear_char(&mob, MOB_VOID);
    // Releases mob.profs/skills/knowledge (clear_char() heap
    // allocations) at scope exit (Phase 5 T6 leak sweep).
    ScopedClearCharFields mob_cleanup { mob };
    mob.specials2.act = MOB_ISNPC;
    mob.nr = 0;
    mob.in_room = 0;
    mob.player.short_descr = const_cast<char*>("Bilbo");
    mob.next_in_room = context.test_world.room().people;
    context.test_world.room().people = &mob;
    do_stat_room(&context.character);
    context.test_world.room().people = &context.character;
    context.character.next_in_room = nullptr;
    const std::string output = context.descriptor.output;
    EXPECT_NE(output.find("Bilbo(MOB) [1234]"), std::string::npos) << output;
}

TEST(ActWizInspection, StatRoomFormatsContentsLineWithoutVnumForLowLevelViewer)
{
    RoomStatContext context;
    context.character.player.level = 50; // <= 91: no obj_index vnum tag
    // do_stat_room's Contents loop intentionally leaves the global buf1
    // untouched (not reset) when the viewer's level is <= 91 -- a legacy
    // quirk preserved verbatim in the transform (see act_wiz.cpp's comment
    // at that site). That means THIS test's expected output depends on
    // buf1's process-global state at the moment it runs; without resetting
    // it here, a differently-ordered/shuffled/repeated run (e.g. after
    // StatRoomFormatsExitLineWithNoDestinationAndNoKeyword, which leaves
    // buf1 == " NONE") would append that leftover text, exactly reproducing
    // the legacy bug rather than exercising THIS test's own "no tag"
    // expectation. Clear it first so the assertion below is deterministic.
    buf1[0] = '\0';
    obj_data object { };
    object.short_description = const_cast<char*>("a shining sword");
    object.item_number = -1;
    context.test_world.room().contents = &object;
    do_stat_room(&context.character);
    context.test_world.room().contents = nullptr;
    const std::string output = context.descriptor.output;
    EXPECT_NE(output.find("Contents: a shining sword\n\r"), std::string::npos) << output;
}

TEST(ActWizInspection, StatRoomFormatsContentsLineWithVnumForHighLevelViewer)
{
    RoomStatContext context;
    ScopedObjIndexEntry obj_index_entry(5678, false);
    context.character.player.level = LEVEL_IMPL;
    obj_data object { };
    object.short_description = const_cast<char*>("a shining sword");
    object.item_number = 0;
    object.in_room = NOWHERE;
    context.test_world.room().contents = &object;
    do_stat_room(&context.character);
    context.test_world.room().contents = nullptr;
    const std::string output = context.descriptor.output;
    EXPECT_NE(output.find("Contents: a shining sword [5678]"), std::string::npos) << output;
}

TEST(ActWizInspection, StatRoomFormatsExitLineWithNoDestinationAndNoKeyword)
{
    RoomStatContext context;
    room_direction_data exit { };
    exit.to_room = NOWHERE;
    exit.key = -1;
    exit.keyword = nullptr;
    exit.general_description = nullptr;
    context.test_world.room().dir_option[0] = &exit; // NORTH
    do_stat_room(&context.character);
    context.test_world.room().dir_option[0] = nullptr;
    const std::string output = context.descriptor.output;
    EXPECT_NE(output.find("Exit north:  To: [ NONE], Key: [   -1], Keywrd: None, Type: <NONE>"),
        std::string::npos)
        << output;
    EXPECT_NE(output.find("  No exit description.\n\r"), std::string::npos) << output;
}

TEST(ActWizInspection, StatRoomFormatsExitLineWithRealDestinationKeywordAndDescription)
{
    // world[1] is already valid memory within RoomStatContext's single-room
    // ScopedTestWorld allocation (room_data::create_bulk(1) allocates
    // 1 + EXTENSION_SIZE rooms so the trailing extension slots -- normally
    // reserved for zone-loading headroom -- are addressable); reused directly
    // here rather than constructing a second, overlapping ScopedTestWorld.
    RoomStatContext context;
    room_direction_data exit { };
    exit.to_room = 1;
    exit.key = 100;
    exit.keyword = const_cast<char*>("gate");
    exit.general_description = const_cast<char*>("A heavy iron gate.\n\r");
    world[1].number = 100;
    context.test_world.room().dir_option[0] = &exit; // NORTH
    do_stat_room(&context.character);
    context.test_world.room().dir_option[0] = nullptr;
    const std::string output = context.descriptor.output;
    EXPECT_NE(output.find("Keywrd: gate"), std::string::npos) << output;
    EXPECT_NE(output.find("A heavy iron gate.\n\r"), std::string::npos) << output;
}

TEST(ActWizInspection, StatRoomFormatsAffectionsNoneLine)
{
    RoomStatContext context;
    do_stat_room(&context.character);
    EXPECT_NE(std::string(context.descriptor.output).find("Affections: None.\n\r"),
        std::string::npos)
        << context.descriptor.output;
}

// ---------------------------------------------------------------------------
// do_stat_object (act_wiz.cpp:548)
// ---------------------------------------------------------------------------

TEST(ActWizInspection, DoStatObjectFormatsNameAliasesWithShortDescriptionAndColor)
{
    SoloCharacterContext context;
    SET_BIT(PRF_FLAGS(&context.character), PRF_COLOR);
    obj_data object { };
    object.short_description = const_cast<char*>("a gleaming blade");
    object.name = const_cast<char*>("blade sword");
    object.item_number = -1;
    object.in_room = NOWHERE;
    do_stat_object(&context.character, &object);
    const std::string output = context.descriptor.output;
    EXPECT_NE(output.find("Name: '"), std::string::npos) << output;
    EXPECT_NE(output.find("a gleaming blade"), std::string::npos) << output;
    EXPECT_NE(output.find("', Aliases: blade sword\n\r"), std::string::npos) << output;
    EXPECT_NE(output.find("\x1B["), std::string::npos)
        << "expected an ANSI escape with PRF_COLOR set: " << output;
}

TEST(ActWizInspection, DoStatObjectFormatsNameAliasesNoneFallbackWithoutColor)
{
    SoloCharacterContext context;
    obj_data object { };
    object.short_description = nullptr;
    object.name = const_cast<char*>("widget");
    object.item_number = -1;
    object.in_room = NOWHERE;
    do_stat_object(&context.character, &object);
    const std::string output = context.descriptor.output;
    EXPECT_NE(output.find("Name: '<None>', Aliases: widget\n\r"), std::string::npos) << output;
    EXPECT_EQ(output.find("\x1B["), std::string::npos)
        << "expected no ANSI escape without PRF_COLOR: " << output;
}

TEST(ActWizInspection, DoStatObjectFormatsVnumRnumTypeSpecProcNoneBranch)
{
    SoloCharacterContext context;
    obj_data object { };
    object.name = const_cast<char*>("widget");
    object.item_number = -1;
    object.in_room = NOWHERE;
    do_stat_object(&context.character, &object);
    const std::string output = context.descriptor.output;
    EXPECT_NE(output.find("VNum: ["), std::string::npos) << output;
    EXPECT_NE(output.find("0"), std::string::npos) << output;
    EXPECT_NE(output.find("], RNum: [   -1], Type: "), std::string::npos) << output;
    EXPECT_NE(output.find("SpecProc: None\n\r"), std::string::npos) << output;
}

TEST(ActWizInspection, DoStatObjectFormatsVnumRnumTypeSpecProcExistsBranch)
{
    SoloCharacterContext context;
    ScopedObjIndexEntry obj_index_entry(9001, true);
    obj_data object { };
    object.name = const_cast<char*>("widget");
    object.item_number = 0;
    object.in_room = NOWHERE;
    do_stat_object(&context.character, &object);
    const std::string output = context.descriptor.output;
    EXPECT_NE(output.find("VNum: ["), std::string::npos) << output;
    EXPECT_NE(output.find("9001"), std::string::npos) << output;
    EXPECT_NE(output.find("], RNum: [    0], Type: "), std::string::npos) << output;
    EXPECT_NE(output.find("SpecProc: Exists\n\r"), std::string::npos) << output;
}

// Exemplar from the task brief: pins the L-Des line's "None" fallback
// (nullable j->description ternary) for a colorless viewer.
TEST(ActWizInspection, DoStatObjectFormatsLDesNoneFallback)
{
    SoloCharacterContext context;
    obj_data object { };
    object.name = const_cast<char*>("widget");
    object.item_number = -1; // no obj_index entry: virt 0, SpecProc "None" branch
    object.in_room = NOWHERE;
    do_stat_object(&context.character, &object);
    EXPECT_TRUE(strstr(context.descriptor.small_outbuf, "L-Des: None\n\r") != nullptr)
        << context.descriptor.small_outbuf;
}

TEST(ActWizInspection, DoStatObjectFormatsLDesWhenPresent)
{
    SoloCharacterContext context;
    obj_data object { };
    object.name = const_cast<char*>("widget");
    object.item_number = -1;
    object.in_room = NOWHERE;
    object.description = const_cast<char*>("A finely wrought widget lies here.");
    do_stat_object(&context.character, &object);
    EXPECT_NE(std::string(context.descriptor.output)
                  .find("L-Des: A finely wrought widget lies here.\n\r"),
        std::string::npos)
        << context.descriptor.output;
}

TEST(ActWizInspection, DoStatObjectFormatsExtraDescsLineWithNoneKeywordFallback)
{
    SoloCharacterContext context;
    obj_data object { };
    object.name = const_cast<char*>("widget");
    object.item_number = -1;
    object.in_room = NOWHERE;
    extra_descr_data desc { };
    desc.keyword = nullptr;
    desc.next = nullptr;
    object.ex_description = &desc;
    do_stat_object(&context.character, &object);
    EXPECT_NE(std::string(context.descriptor.output).find("Extra descs: <None>\n\r"),
        std::string::npos)
        << context.descriptor.output;
}

TEST(ActWizInspection, DoStatObjectFormatsWearSetCharExtraFlagLinesWithNoFlags)
{
    SoloCharacterContext context;
    obj_data object { };
    object.name = const_cast<char*>("widget");
    object.item_number = -1;
    object.in_room = NOWHERE;
    object.obj_flags.wear_flags = 0;
    object.obj_flags.bitvector = 0;
    object.obj_flags.extra_flags = 0;
    do_stat_object(&context.character, &object);
    const std::string output = context.descriptor.output;
    EXPECT_NE(output.find("Can be worn on: <NONE>\n\r"), std::string::npos) << output;
    EXPECT_NE(output.find("Set char bits : <NONE>\n\r"), std::string::npos) << output;
    EXPECT_NE(output.find("Extra flags   : <NONE>\n\r"), std::string::npos) << output;
}

TEST(ActWizInspection, DoStatObjectFormatsMaterialUnknownFallback)
{
    SoloCharacterContext context;
    obj_data object { };
    object.name = const_cast<char*>("widget");
    object.item_number = -1;
    object.in_room = NOWHERE;
    object.obj_flags.material = -5;
    do_stat_object(&context.character, &object);
    EXPECT_NE(std::string(context.descriptor.output).find("Material: Unknown\n\r"),
        std::string::npos)
        << context.descriptor.output;
}

TEST(ActWizInspection, DoStatObjectFormatsWeightValueCostLevelTimerLine)
{
    SoloCharacterContext context;
    obj_data object { };
    object.name = const_cast<char*>("widget");
    object.item_number = -1;
    object.in_room = NOWHERE;
    object.obj_flags.weight = 7;
    object.obj_flags.cost = 250;
    object.obj_flags.cost_per_day = 3;
    object.obj_flags.level = 12;
    object.obj_flags.timer = -1;
    do_stat_object(&context.character, &object);
    EXPECT_NE(std::string(context.descriptor.output)
                  .find("Weight: 7, Value: 250, Cost/day: 0 (set to 3), Level 12, Timer: -1\n\r"),
        std::string::npos)
        << context.descriptor.output;
}

TEST(ActWizInspection, DoStatObjectFormatsScriptNumberLine)
{
    SoloCharacterContext context;
    obj_data object { };
    object.name = const_cast<char*>("widget");
    object.item_number = -1;
    object.in_room = NOWHERE;
    object.obj_flags.script_number = 77;
    do_stat_object(&context.character, &object);
    EXPECT_NE(std::string(context.descriptor.output).find("Script number: 77\n\r"),
        std::string::npos)
        << context.descriptor.output;
}

TEST(ActWizInspection, DoStatObjectFormatsLocationLineWithNowhereNoneNobodyFallbacks)
{
    SoloCharacterContext context;
    obj_data object { };
    object.name = const_cast<char*>("widget");
    object.item_number = -1;
    object.in_room = NOWHERE;
    object.in_obj = nullptr;
    object.carried_by = nullptr;
    object.loaded_by = 0;
    do_stat_object(&context.character, &object);
    EXPECT_NE(std::string(context.descriptor.output)
                  .find("In room: Nowhere, In object: None, Carried by: Nobody\n\r"),
        std::string::npos)
        << context.descriptor.output;
}

TEST(ActWizInspection, DoStatObjectFormatsLocationLineWithRealRoomObjectAndCarrier)
{
    SoloCharacterContext context;
    ScopedTestWorld test_world;
    // ScopedTestWorld's single room comes out of room_data::create_bulk()
    // stamped with EXTENSION_ROOM_HEAD as its .number (see RoomStatContext's
    // fixture comment); pin it to 0 to match the "In room: 0" expectation
    // below.
    test_world.room().number = 0;
    obj_data container { };
    container.short_description = const_cast<char*>("a leather pouch");
    obj_data object { };
    object.name = const_cast<char*>("widget");
    object.item_number = -1;
    object.in_room = 0;
    object.in_obj = &container;
    object.carried_by = &context.character;
    context.character.player.name = const_cast<char*>("Frodo");
    do_stat_object(&context.character, &object);
    EXPECT_NE(std::string(context.descriptor.output)
                  .find("In room: 0, In object: a leather pouch, Carried by: Frodo\n\r"),
        std::string::npos)
        << context.descriptor.output;
}

TEST(ActWizInspection, DoStatObjectFormatsWeaponValuesLine)
{
    SoloCharacterContext context;
    obj_data object { };
    object.name = const_cast<char*>("widget");
    object.item_number = -1;
    object.in_room = NOWHERE;
    object.obj_flags.type_flag = ITEM_WEAPON;
    object.obj_flags.value[0] = 10;
    object.obj_flags.value[1] = 5;
    object.obj_flags.value[2] = 3;
    object.obj_flags.value[3] = 1;
    object.obj_flags.value[4] = 20;
    do_stat_object(&context.character, &object);
    const std::string output = context.descriptor.output;
    EXPECT_NE(output.find("OB: 10, Parry: 5, Bulk: 3,  Type: 1, Damage: "), std::string::npos)
        << output;
    EXPECT_NE(output.find("(set to 20)"), std::string::npos) << output;
}

TEST(ActWizInspection, DoStatObjectFormatsDefaultValuesLine)
{
    SoloCharacterContext context;
    obj_data object { };
    object.name = const_cast<char*>("widget");
    object.item_number = -1;
    object.in_room = NOWHERE;
    object.obj_flags.type_flag = 0;
    object.obj_flags.value[0] = 1;
    object.obj_flags.value[1] = 2;
    object.obj_flags.value[2] = 3;
    object.obj_flags.value[3] = 4;
    object.obj_flags.value[4] = 5;
    do_stat_object(&context.character, &object);
    EXPECT_NE(std::string(context.descriptor.output).find("Values 0-4: [1] [2] [3] [4] [5]"),
        std::string::npos)
        << context.descriptor.output;
}

TEST(ActWizInspection, DoStatObjectFormatsContentsLine)
{
    SoloCharacterContext context;
    obj_data nested { };
    nested.short_description = const_cast<char*>("a small key");
    nested.next_content = nullptr;
    obj_data object { };
    object.name = const_cast<char*>("widget");
    object.item_number = -1;
    object.in_room = NOWHERE;
    object.contains = &nested;
    do_stat_object(&context.character, &object);
    EXPECT_NE(std::string(context.descriptor.output).find("Contents: a small key"),
        std::string::npos)
        << context.descriptor.output;
}

TEST(ActWizInspection, DoStatObjectFormatsAffectionsNoneLine)
{
    SoloCharacterContext context;
    obj_data object { };
    object.name = const_cast<char*>("widget");
    object.item_number = -1;
    object.in_room = NOWHERE;
    do_stat_object(&context.character, &object);
    EXPECT_NE(std::string(context.descriptor.output).find("\n\rAffections: None\n\r"),
        std::string::npos)
        << context.descriptor.output;
}

TEST(ActWizInspection, DoStatObjectFormatsAffectionsWithModifierLine)
{
    SoloCharacterContext context;
    obj_data object { };
    object.name = const_cast<char*>("widget");
    object.item_number = -1;
    object.in_room = NOWHERE;
    object.affected[0].modifier = 3;
    object.affected[0].location = 0;
    do_stat_object(&context.character, &object);
    const std::string output = context.descriptor.output;
    EXPECT_NE(output.find("\n\rAffections: +3 to "), std::string::npos) << output;
}

// ---------------------------------------------------------------------------
// do_stat_character (act_wiz.cpp:736)
// ---------------------------------------------------------------------------

TEST(ActWizInspection, StatCharacterDeniesLowLevelViewerAgainstPc)
{
    SoloCharacterContext viewer;
    viewer.character.player.level = 1;
    PcTargetContext target;
    do_stat_character(&viewer.character, &target.character);
    EXPECT_EQ(std::string(viewer.descriptor.output), "You can't do this.\n\r");
}

TEST(ActWizInspection, StatCharacterFormatsMaleSexLine)
{
    SoloCharacterContext viewer;
    PcTargetContext target;
    target.character.player.sex = SEX_MALE;
    do_stat_character(&viewer.character, &target.character);
    EXPECT_NE(std::string(viewer.descriptor.output).find("MALE PC '"), std::string::npos)
        << viewer.descriptor.output;
}

TEST(ActWizInspection, StatCharacterFormatsIllegalSexDefaultBranch)
{
    SoloCharacterContext viewer;
    PcTargetContext target;
    target.character.player.sex = 99;
    do_stat_character(&viewer.character, &target.character);
    EXPECT_NE(std::string(viewer.descriptor.output).find("ILLEGAL-SEX!! PC '"), std::string::npos)
        << viewer.descriptor.output;
}

TEST(ActWizInspection, StatCharacterFormatsHeaderLineForPc)
{
    SoloCharacterContext viewer;
    PcTargetContext target;
    target.character.player.name = const_cast<char*>("Aragorn");
    target.character.specials2.idnum = 4242;
    target.character.in_room = NOWHERE;
    do_stat_character(&viewer.character, &target.character);
    const std::string output = viewer.descriptor.output;
    EXPECT_NE(output.find(" PC 'Aragorn'  IDNum: [ 4242]"), std::string::npos) << output;
    EXPECT_NE(output.find("In room [   -1]"), std::string::npos) << output;
}

TEST(ActWizInspection, StatCharacterFormatsHeaderAndAliasLinesForMob)
{
    SoloCharacterContext viewer;
    ScopedMobIndexEntry mob_index_entry(3000, false);
    NpcTargetContext target;
    target.character.player.name = const_cast<char*>("orc alias");
    target.character.player.short_descr = const_cast<char*>("a snarling orc");
    do_stat_character(&viewer.character, &target.character);
    const std::string output = viewer.descriptor.output;
    EXPECT_NE(output.find(" MOB 'a snarling orc'"), std::string::npos) << output;
    EXPECT_NE(output.find("Alias: orc alias, VNum: [ 3000], RNum: [    0]\n\r"), std::string::npos)
        << output;
}

TEST(ActWizInspection, StatCharacterFormatsTitleNoneFallback)
{
    SoloCharacterContext viewer;
    PcTargetContext target;
    target.character.player.title = nullptr;
    do_stat_character(&viewer.character, &target.character);
    EXPECT_NE(std::string(viewer.descriptor.output).find("Title: <None>\n\r"), std::string::npos)
        << viewer.descriptor.output;
}

TEST(ActWizInspection, StatCharacterFormatsTitleWhenPresent)
{
    SoloCharacterContext viewer;
    PcTargetContext target;
    target.character.player.title = const_cast<char*>("the Brave");
    do_stat_character(&viewer.character, &target.character);
    EXPECT_NE(std::string(viewer.descriptor.output).find("Title: the Brave\n\r"), std::string::npos)
        << viewer.descriptor.output;
}

TEST(ActWizInspection, StatCharacterFormatsRaceLine)
{
    SoloCharacterContext viewer;
    PcTargetContext target;
    target.character.player.race = RACE_HUMAN;
    do_stat_character(&viewer.character, &target.character);
    EXPECT_NE(std::string(viewer.descriptor.output)
                  .find(std::format("which is number {}\n\r", static_cast<int>(RACE_HUMAN))),
        std::string::npos)
        << viewer.descriptor.output;
}

TEST(ActWizInspection, StatCharacterFormatsLDesNoneFallback)
{
    SoloCharacterContext viewer;
    PcTargetContext target;
    target.character.player.long_descr = nullptr;
    do_stat_character(&viewer.character, &target.character);
    EXPECT_NE(std::string(viewer.descriptor.output).find("L-Des: <None>\n\r"), std::string::npos)
        << viewer.descriptor.output;
}

TEST(ActWizInspection, StatCharacterFormatsLDesWhenPresent)
{
    SoloCharacterContext viewer;
    PcTargetContext target;
    target.character.player.long_descr = const_cast<char*>("A tall ranger stands here.\n\r");
    do_stat_character(&viewer.character, &target.character);
    EXPECT_NE(std::string(viewer.descriptor.output).find("L-Des: A tall ranger stands here.\n\r"),
        std::string::npos)
        << viewer.descriptor.output;
}

TEST(ActWizInspection, StatCharacterFormatsClassCoofsAndLevelsForPc)
{
    SoloCharacterContext viewer;
    PcTargetContext target;
    do_stat_character(&viewer.character, &target.character);
    const std::string output = viewer.descriptor.output;
    EXPECT_NE(output.find("Class coofs : Mag:"), std::string::npos) << output;
    EXPECT_NE(output.find("Class levels: Mag:"), std::string::npos) << output;
}

TEST(ActWizInspection, StatCharacterFormatsClassCoofsNotDefinedForMobile)
{
    SoloCharacterContext viewer;
    ScopedMobIndexEntry mob_index_entry(1, false);
    NpcTargetContext target;
    do_stat_character(&viewer.character, &target.character);
    EXPECT_NE(
        std::string(viewer.descriptor.output).find("Class coofs are not defined for mobiles.\n\r"),
        std::string::npos)
        << viewer.descriptor.output;
}

TEST(ActWizInspection, StatCharacterFormatsSpecLineForPc)
{
    SoloCharacterContext viewer;
    PcTargetContext target;
    do_stat_character(&viewer.character, &target.character);
    EXPECT_NE(std::string(viewer.descriptor.output).find("Spec:(0) "), std::string::npos)
        << viewer.descriptor.output;
}

TEST(ActWizInspection, StatCharacterFormatsLevXpAlignLine)
{
    SoloCharacterContext viewer;
    PcTargetContext target;
    target.character.player.level = 42;
    target.character.points.exp = 12345;
    target.character.specials2.alignment = -50;
    do_stat_character(&viewer.character, &target.character);
    const std::string output = viewer.descriptor.output;
    EXPECT_NE(output.find("Lev: [42], XP: [  12345], Align: [ -50]\n\r"), std::string::npos)
        << output;
}

TEST(ActWizInspection, StatCharacterFormatsExistsForTicksLineForMobile)
{
    SoloCharacterContext viewer;
    ScopedMobIndexEntry mob_index_entry(1, false);
    NpcTargetContext target;
    target.character.specials.prompt_number = 7;
    do_stat_character(&viewer.character, &target.character);
    const std::string output = viewer.descriptor.output;
    EXPECT_NE(output.find("Exists for "), std::string::npos) << output;
    EXPECT_NE(output.find(" ticks, Difficulty 7.\n\r"), std::string::npos) << output;
}

TEST(ActWizInspection, StatCharacterFormatsHometownSpeaksLineForPc)
{
    SoloCharacterContext viewer;
    PcTargetContext target;
    target.character.player.hometown = 5;
    target.character.player.talks[0] = 1;
    target.character.player.talks[1] = 0;
    target.character.player.talks[2] = 1;
    target.character.specials2.spells_to_learn = 3;
    do_stat_character(&viewer.character, &target.character);
    EXPECT_NE(std::string(viewer.descriptor.output)
                  .find("Hometown: [5], Speaks: [1/0/1], (pracs left:[3])\n\r"),
        std::string::npos)
        << viewer.descriptor.output;
}

TEST(ActWizInspection, StatCharacterFormatsAbilitiesLine)
{
    SoloCharacterContext viewer;
    PcTargetContext target;
    target.character.tmpabilities.str = 90;
    target.character.abilities.str = 95;
    target.character.constabilities.str = 5;
    do_stat_character(&viewer.character, &target.character);
    EXPECT_NE(std::string(viewer.descriptor.output).find("Str:[90/95/5] "), std::string::npos)
        << viewer.descriptor.output;
}

TEST(ActWizInspection, StatCharacterFormatsCoinsLine)
{
    SoloCharacterContext viewer;
    PcTargetContext target;
    target.character.points.gold = 500;
    do_stat_character(&viewer.character, &target.character);
    EXPECT_NE(std::string(viewer.descriptor.output).find("Coins: [      500]\n\r"),
        std::string::npos)
        << viewer.descriptor.output;
}

TEST(ActWizInspection, StatCharacterFormatsEnergyLine)
{
    SoloCharacterContext viewer;
    PcTargetContext target;
    target.character.specials.ENERGY = 12;
    target.character.points.damage = 3;
    target.character.specials.null_speed = 1;
    target.character.specials.str_speed = 2;
    do_stat_character(&viewer.character, &target.character);
    const std::string output = viewer.descriptor.output;
    EXPECT_NE(output.find("ENERGY: 12, ENE_regen: "), std::string::npos) << output;
    EXPECT_NE(output.find("damage: 3, null_speed: 1, str_speed 2\n\r"), std::string::npos)
        << output;
}

TEST(ActWizInspection, StatCharacterFormatsSpellPenLineForPcOnly)
{
    SoloCharacterContext viewer;
    PcTargetContext target;
    do_stat_character(&viewer.character, &target.character);
    EXPECT_NE(std::string(viewer.descriptor.output).find("Spell_Pen: "), std::string::npos)
        << viewer.descriptor.output;
}

TEST(ActWizInspection, StatCharacterFormatsSpellPenLineAbsentForMobile)
{
    SoloCharacterContext viewer;
    ScopedMobIndexEntry mob_index_entry(1, false);
    NpcTargetContext target;
    do_stat_character(&viewer.character, &target.character);
    EXPECT_EQ(std::string(viewer.descriptor.output).find("Spell_Pen: "), std::string::npos)
        << viewer.descriptor.output;
}

TEST(ActWizInspection, StatCharacterFormatsPosFightingLineWithNobody)
{
    SoloCharacterContext viewer;
    PcTargetContext target;
    target.character.specials.position = POSITION_STANDING;
    target.character.specials.fighting = nullptr;
    do_stat_character(&viewer.character, &target.character);
    EXPECT_NE(std::string(viewer.descriptor.output).find("Fighting: Nobody"), std::string::npos)
        << viewer.descriptor.output;
}

TEST(ActWizInspection, StatCharacterFormatsDefaultPositionIdleTimerLine)
{
    SoloCharacterContext viewer;
    PcTargetContext target;
    target.character.specials.default_pos = POSITION_STANDING;
    target.character.specials.timer = 4;
    do_stat_character(&viewer.character, &target.character);
    EXPECT_NE(std::string(viewer.descriptor.output).find(", Idle Timer (in tics) [4]\n\r"),
        std::string::npos)
        << viewer.descriptor.output;
}

TEST(ActWizInspection, StatCharacterFormatsPlrPrfRpFlagLinesForPc)
{
    SoloCharacterContext viewer;
    PcTargetContext target;
    target.character.specials2.act = 0; // not NPC
    target.character.specials2.pref = 0;
    target.character.specials2.rp_flag = 2;
    do_stat_character(&viewer.character, &target.character);
    const std::string output = viewer.descriptor.output;
    EXPECT_NE(output.find("PLR: <NONE>\n\r"), std::string::npos) << output;
    EXPECT_NE(output.find("PRF: <NONE>\n\r"), std::string::npos) << output;
    EXPECT_NE(output.find("rp_flag: 2\n\r"), std::string::npos) << output;
}

TEST(ActWizInspection, StatCharacterFormatsNpcFlagsLineForMobile)
{
    SoloCharacterContext viewer;
    ScopedMobIndexEntry mob_index_entry(1, false);
    NpcTargetContext target;
    do_stat_character(&viewer.character, &target.character);
    const std::string output = viewer.descriptor.output;
    EXPECT_NE(output.find("NPC flags: (8)  ISNPC., agg flag: 0, will_teach: 0\n\r"),
        std::string::npos)
        << output;
}

TEST(ActWizInspection, StatCharacterFormatsMobSpecProcLineForMobile)
{
    SoloCharacterContext viewer;
    ScopedMobIndexEntry mob_index_entry(1, true);
    NpcTargetContext target;
    do_stat_character(&viewer.character, &target.character);
    EXPECT_NE(std::string(viewer.descriptor.output).find("Mob Spec-Proc: Exists,"),
        std::string::npos)
        << viewer.descriptor.output;
}

TEST(ActWizInspection, StatCharacterFormatsSpecialProgNumberCarriedLine)
{
    SoloCharacterContext viewer;
    PcTargetContext target;
    target.character.specials.store_prog_number = 9;
    do_stat_character(&viewer.character, &target.character);
    const std::string output = viewer.descriptor.output;
    EXPECT_NE(output.find("Special prog_number: 9, Callmask: -1\n\rCarried: weight: "),
        std::string::npos)
        << output;
    EXPECT_NE(output.find("Items in: inventory: 0, eq: 0\n\r"), std::string::npos) << output;
}

TEST(ActWizInspection, StatCharacterFormatsHungerThirstDrunkAttLevelLine)
{
    SoloCharacterContext viewer;
    PcTargetContext target;
    target.character.specials2.conditions[FULL] = 10;
    target.character.specials2.conditions[THIRST] = 20;
    target.character.specials2.conditions[DRUNK] = 0;
    target.character.specials.attacked_level = 5;
    do_stat_character(&viewer.character, &target.character);
    EXPECT_NE(std::string(viewer.descriptor.output)
                  .find("Hunger: 10, Thirst: 20, Drunk: 0, Att.Level: 5\n\r"),
        std::string::npos)
        << viewer.descriptor.output;
}

TEST(ActWizInspection, StatCharacterFormatsMasterFollowersNoneLine)
{
    SoloCharacterContext viewer;
    PcTargetContext target;
    target.character.master = nullptr;
    target.character.followers = nullptr;
    do_stat_character(&viewer.character, &target.character);
    EXPECT_NE(std::string(viewer.descriptor.output).find("Master is: <none>, Followers are:"),
        std::string::npos)
        << viewer.descriptor.output;
}

TEST(ActWizInspection, StatCharacterFormatsMasterFollowersWithEntriesLine)
{
    SoloCharacterContext viewer;
    PcTargetContext target;
    char_data leader { };
    clear_char(&leader, MOB_VOID);
    // Releases leader.profs/skills/knowledge (clear_char() heap
    // allocations) at scope exit (Phase 5 T6 leak sweep).
    ScopedClearCharFields leader_cleanup { leader };
    leader.player.name = const_cast<char*>("Gandalf");
    target.character.master = &leader;

    char_data follower_char { };
    clear_char(&follower_char, MOB_VOID);
    // Releases follower_char.profs/skills/knowledge (clear_char() heap
    // allocations) at scope exit (Phase 5 T6 leak sweep).
    ScopedClearCharFields follower_char_cleanup { follower_char };
    follower_char.player.name = const_cast<char*>("Sam");
    follow_type follower { };
    follower.follower = &follower_char;
    follower.next = nullptr;
    target.character.followers = &follower;

    do_stat_character(&viewer.character, &target.character);
    const std::string output = viewer.descriptor.output;
    EXPECT_NE(output.find("Master is: Gandalf, Followers are: Sam"), std::string::npos) << output;
}

TEST(ActWizInspection, StatCharacterFormatsDelayCommandLine)
{
    SoloCharacterContext viewer;
    PcTargetContext target;
    target.character.delay.cmd = 3;
    target.character.delay.wait_value = 7;
    do_stat_character(&viewer.character, &target.character);
    EXPECT_NE(std::string(viewer.descriptor.output).find("Delay command:3 delay_value:7\n\r"),
        std::string::npos)
        << viewer.descriptor.output;
}

TEST(ActWizInspection, StatCharacterFormatsNoSpecialMemoriesLineForPc)
{
    SoloCharacterContext viewer;
    PcTargetContext target;
    do_stat_character(&viewer.character, &target.character);
    EXPECT_NE(std::string(viewer.descriptor.output).find("No special memories.\n\r"),
        std::string::npos)
        << viewer.descriptor.output;
}

TEST(ActWizInspection, StatCharacterFormatsMemoriesWithEntriesForMobile)
{
    SoloCharacterContext viewer;
    ScopedMobIndexEntry mob_index_entry(1, false);
    NpcTargetContext target;
    memory_rec first_memory { };
    first_memory.id = 11;
    first_memory.next_on_mob = nullptr;
    target.character.specials.memory = &first_memory;
    do_stat_character(&viewer.character, &target.character);
    EXPECT_NE(std::string(viewer.descriptor.output).find("Memories:  11\n\r"), std::string::npos)
        << viewer.descriptor.output;
}

TEST(ActWizInspection, StatCharacterFormatsAffResVulLines)
{
    SoloCharacterContext viewer;
    PcTargetContext target;
    target.character.specials.affected_by = 0;
    target.character.specials.resistance = 0;
    target.character.specials.vulnerability = 0;
    do_stat_character(&viewer.character, &target.character);
    const std::string output = viewer.descriptor.output;
    EXPECT_NE(output.find("AFF: "), std::string::npos) << output;
    EXPECT_NE(output.find("RES: "), std::string::npos) << output;
    EXPECT_NE(output.find("VUL: "), std::string::npos) << output;
    EXPECT_NE(output.find("<NONE>"), std::string::npos) << output;
}

TEST(ActWizInspection, StatCharacterFormatsSplAffectedLineWithModifierAndBitvector)
{
    SoloCharacterContext viewer;
    PcTargetContext target;
    affected_type affect { };
    affect.type = 0;
    affect.duration = 5;
    affect.modifier = 4;
    affect.location = 0;
    affect.bitvector = 1;
    affect.next = nullptr;
    target.character.affected = &affect;
    do_stat_character(&viewer.character, &target.character);
    const std::string output = viewer.descriptor.output;
    EXPECT_NE(output.find("SPL: (  6hr) "), std::string::npos) << output;
    EXPECT_NE(output.find("+4 to "), std::string::npos) << output;
    EXPECT_NE(output.find(", sets "), std::string::npos) << output;
}

// ---------------------------------------------------------------------------
// do_date / do_uptime (act_wiz.cpp:1811, :1831)
// ---------------------------------------------------------------------------

TEST(ActWizInspection, DoDateFormatsCurrentAndLastRebootLines)
{
    SoloCharacterContext viewer;
    const time_t previous_boot_time = boot_time;
    boot_time = 1'700'000'000; // fixed instant, not wall-clock-dependent
    char argument[] = "";
    do_date(&viewer.character, argument, nullptr, 0, 0);
    boot_time = previous_boot_time;

    const std::string output = viewer.descriptor.output;
    EXPECT_NE(output.find("Current machine time: "), std::string::npos) << output;
    const std::string expected_reboot_line = std::format("Last reboot on: {}",
        strip_trailing_newline(asctime(localtime(&(boot_time = 1'700'000'000)))));
    // asctime() itself already appended '\n'; act_wiz.cpp's do_date doesn't
    // strip it for the reboot line (unlike the "current time" line above),
    // so only pin the literal prefix here rather than the full trailing
    // newline shape.
    EXPECT_NE(output.find("Last reboot on: "), std::string::npos) << output;
}

TEST(ActWizInspection, DoUptimeFormatsUpSinceLineAndLastdeathBody)
{
    SoloCharacterContext viewer;
    const time_t previous_boot_time = boot_time;
    char* previous_lastdeath = lastdeath;
    // One day, one hour, one minute (plus a few seconds of slack against the
    // sub-second gap between stamping boot_time and do_uptime's own time(0)
    // call) in the past -- deterministic day/hour/minute fields without
    // depending on wall-clock value itself.
    boot_time = std::time(nullptr) - (1 * 86400 + 1 * 3600 + 1 * 60 + 5);
    lastdeath = const_cast<char*>("Slain by a troll.\n\r");

    char argument[] = "";
    do_uptime(&viewer.character, argument, nullptr, 0, 0);

    boot_time = previous_boot_time;
    lastdeath = previous_lastdeath;

    const std::string output = viewer.descriptor.output;
    EXPECT_NE(output.find("Up since "), std::string::npos) << output;
    EXPECT_NE(output.find(": 1 day, 1:01\n\r"), std::string::npos) << output;
    EXPECT_NE(output.find("The last minutes of the previous run:\n\r"), std::string::npos)
        << output;
    EXPECT_NE(output.find("Slain by a troll.\n\r"), std::string::npos) << output;
}

// ---------------------------------------------------------------------------
// do_vnum (act_wiz.cpp:397) -- no format site of its own; see file-header
// exclusion note. Exercised lightly for completeness since it's in range.
// ---------------------------------------------------------------------------

TEST(ActWizInspection, LightlyExercisesUsageMessage)
{
    SoloCharacterContext viewer;
    char argument[] = "";
    do_vnum(&viewer.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(viewer.descriptor.output), "Usage: vnum { obj | mob } <name>\n\r");
}

// ---------------------------------------------------------------------------
// do_zone (act_wiz.cpp:1022)
// ---------------------------------------------------------------------------

TEST(ActWizInspection, DoZoneReportsNoSuchZone)
{
    RoomStatContext context;
    char argument[] = "999";
    do_zone(&context.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.descriptor.output), "No such zone in the world.\n\r");
}

TEST(ActWizInspection, DoZoneFormatsHeaderOwnersAllAndCoordinatesLines)
{
    RoomStatContext context;
    context.zone_table_scope.zone().name = const_cast<char*>("The Shire");
    context.zone_table_scope.zone().description = const_cast<char*>("Rolling green hills.\n\r");
    context.zone_table_scope.zone().map = const_cast<char*>("");
    char argument[] = "0";
    do_zone(&context.character, argument, nullptr, 0, 0);
    const std::string output = context.descriptor.output;
    EXPECT_NE(output.find("Zone #0: The Shire\n\r"), std::string::npos) << output;
    EXPECT_NE(output.find("Owners: All.\n\r"), std::string::npos) << output;
    EXPECT_NE(output.find("Coordinates: (3, 7), symbol 'Z' Level 0\n\r"), std::string::npos)
        << output;
}

TEST(ActWizInspection, DoZoneFormatsOwnersNamedLine)
{
    RoomStatContext context;
    context.zone_table_scope.zone().name = const_cast<char*>("The Shire");
    context.zone_table_scope.zone().description = const_cast<char*>("");
    context.zone_table_scope.zone().map = const_cast<char*>("");

    player_index_element* previous_player_table = player_table;
    const int previous_top = top_of_p_table;
    player_table = new player_index_element[1] { };
    player_table[0].name = strdup("bilbo");
    player_table[0].idnum = 777;
    top_of_p_table = 0;

    // named_owner.next keeps pointing at the pre-existing owner==0 sentinel
    // so the do_zone owner-name walk terminates after this one match.
    owner_list named_owner { };
    named_owner.owner = 777; // matches player_table[0].idnum via find_player_in_table
    named_owner.next = context.zone_table_scope.zone().owners;
    context.zone_table_scope.zone().owners = &named_owner;

    char argument[] = "0";
    do_zone(&context.character, argument, nullptr, 0, 0);

    free(player_table[0].name);
    delete[] player_table;
    player_table = previous_player_table;
    top_of_p_table = previous_top;

    const std::string output = context.descriptor.output;
    EXPECT_NE(output.find("Owners:  Bilbo.\n\r"), std::string::npos) << output;
}

// ---------------------------------------------------------------------------
// do_findzone (act_wiz.cpp:3788)
// ---------------------------------------------------------------------------

TEST(ActWizInspection, DoFindzoneListsMatchingZoneLine)
{
    SoloCharacterContext viewer;
    ScopedZoneTable zone_table_scope(0);
    zone_table_scope.zone().x = 3;
    zone_table_scope.zone().y = 7;
    zone_table_scope.zone().symbol = 'Z';
    zone_table_scope.zone().name = const_cast<char*>("The Shire");
    char argument[] = "3 7";
    do_findzone(&viewer.character, argument, nullptr, 0, 0);
    const std::string output = viewer.descriptor.output;
    EXPECT_NE(output.find("Zones loaded are:"), std::string::npos) << output;
    EXPECT_NE(output.find("\n\rZone   0 ( 3,  7, 'Z')The Shire"), std::string::npos) << output;
    EXPECT_NE(output.find("\n\rEnd of list.\n\r"), std::string::npos) << output;
}

TEST(ActWizInspection, DoFindzoneReportsNoMatchesWhenCoordinatesMiss)
{
    SoloCharacterContext viewer;
    ScopedZoneTable zone_table_scope(0);
    zone_table_scope.zone().x = 3;
    zone_table_scope.zone().y = 7;
    char argument[] = "99 99";
    do_findzone(&viewer.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(viewer.descriptor.output), "Zones loaded are:\n\rEnd of list.\n\r");
}

// ---------------------------------------------------------------------------
// do_top (act_wiz.cpp:3942)
// ---------------------------------------------------------------------------

TEST(ActWizInspection, DoTopReportsUsageWhenNoCountGiven)
{
    SoloCharacterContext viewer;
    char argument[] = "";
    do_top(&viewer.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(viewer.descriptor.output), "Usage: top ## [[oldest] race].\n\r");
}

TEST(ActWizInspection, DoTopFormatsHeaderWithoutRace)
{
    SoloCharacterContext viewer;
    player_index_element* previous_player_table = player_table;
    const int previous_top = top_of_p_table;
    player_table = new player_index_element[1] { };
    player_table[0].name = strdup("bilbo");
    player_table[0].level = 50;
    top_of_p_table = 0;

    char argument[] = "3";
    do_top(&viewer.character, argument, nullptr, 0, 0);

    free(player_table[0].name);
    delete[] player_table;
    player_table = previous_player_table;
    top_of_p_table = previous_top;

    EXPECT_NE(std::string(viewer.descriptor.output).find("Top  3 Characters\n\r"),
        std::string::npos)
        << viewer.descriptor.output;
}

TEST(ActWizInspection, DoTopFormatsHeaderWithRace)
{
    SoloCharacterContext viewer;
    player_index_element* previous_player_table = player_table;
    const int previous_top = top_of_p_table;
    player_table = new player_index_element[1] { };
    player_table[0].name = strdup("bilbo");
    player_table[0].level = 50;
    player_table[0].race = 1; // human
    top_of_p_table = 0;

    char argument[] = "3 human";
    do_top(&viewer.character, argument, nullptr, 0, 0);

    free(player_table[0].name);
    delete[] player_table;
    player_table = previous_player_table;
    top_of_p_table = previous_top;

    EXPECT_NE(std::string(viewer.descriptor.output).find("Top  3 Human Characters\n\r"),
        std::string::npos)
        << viewer.descriptor.output;
}

TEST(ActWizInspection, DoTopFormatsPlayerListingLine)
{
    SoloCharacterContext viewer;
    player_index_element* previous_player_table = player_table;
    const int previous_top = top_of_p_table;
    player_table = new player_index_element[2] { };
    player_table[0].name = strdup("bilbo");
    player_table[0].level = 50;
    player_table[1].name = strdup("frodo");
    player_table[1].level = 45;
    top_of_p_table = 1;

    char argument[] = "5";
    do_top(&viewer.character, argument, nullptr, 0, 0);

    free(player_table[0].name);
    free(player_table[1].name);
    delete[] player_table;
    player_table = previous_player_table;
    top_of_p_table = previous_top;

    const std::string output = viewer.descriptor.output;
    EXPECT_NE(output.find("50 - bilbo\n\r"), std::string::npos) << output;
}

// ---------------------------------------------------------------------------
// do_last (act_wiz.cpp:1859)
// ---------------------------------------------------------------------------

namespace {

// Mirrors act_wiz_tests.cpp's TemporaryDirectory/ScopedWorkingDirectory
// (Phase 3 Task 5/6 convention), plus a lighter-weight sibling of that same
// file's write_valid_legacy_player_file(): rather than copying the generated
// file to a separate "legacy path" (that helper's job is exercising the
// migration converter), do_last only needs load_char() to succeed via the
// SAME player_table[0].ch_file save_player() already points at, so the
// generated file is left in place and used directly.
class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        char directory_template[] = "/tmp/rots-act-wiz-format-tests-XXXXXX";
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

class ScopedWorkingDirectory {
public:
    explicit ScopedWorkingDirectory(const std::string& path)
    {
        std::error_code ec;
        m_original_path = std::filesystem::current_path(ec);
        EXPECT_FALSE(ec);
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

TEST(ActWizInspection, DoLastFormatsPlayerSummaryLine)
{
    SoloCharacterContext viewer;
    viewer.character.player.level = LEVEL_IMPL;
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    std::filesystem::create_directories("players/A-E");

    player_index_element* previous_player_table = player_table;
    const int previous_top = top_of_p_table;
    player_table = new player_index_element[1] { };
    top_of_p_table = 0;
    player_table[0].name = strdup("aragorn");
    std::snprintf(player_table[0].ch_file, sizeof(player_table[0].ch_file), "%s", "players/A-E/aragorn");

    // Hand-written minimal load_player_from_text() (db.cpp) player file --
    // deliberately NOT produced via save_player()/write_player_text(): that
    // path's "password" field is always encrypt_line()-transformed bytes in
    // [32, 159], which never contains a null terminator inside
    // MAX_PWD_LENGTH, so its own fprintf("%s", pwdcrypt) is an
    // always-triggering global-buffer-overflow under ASan (db.cpp, out of
    // this task's file list) whenever save_player() runs at all -- not a
    // bug this test introduces, but one this test must route around rather
    // than trip over. load_player_from_text() is a flexible key/value
    // parser (db.cpp's KEY_INT/KEY_STR macros): only "end" is mandatory,
    // everything else defaults to the char_file_u{} zero-fill, and each
    // recognized line's value must start at column 12 (the field name
    // padded with spaces), matching write_player_text()'s own field
    // layout. Only the fields do_last actually reads are supplied.
    {
        std::ofstream player_file("players/A-E/aragorn", std::ios::binary);
        ASSERT_TRUE(player_file.good());
        player_file << "name        aragorn\n";
        player_file << "level       50\n";
        player_file << "race        " << static_cast<int>(RACE_HUMAN) << "\n";
        player_file << "idnum       4242\n";
        player_file << "last_logon  1700000000\n";
        player_file << "host        test-host\n";
        player_file << "end\n";
    }

    char argument[] = "aragorn";
    do_last(&viewer.character, argument, nullptr, 0, 0);

    free(player_table[0].name);
    delete[] player_table;
    player_table = previous_player_table;
    top_of_p_table = previous_top;

    const std::string output = viewer.descriptor.output;
    EXPECT_NE(output.find("[ 4242] [50 "), std::string::npos) << output;
    EXPECT_NE(output.find("aragorn"), std::string::npos) << output;
    EXPECT_NE(output.find("test-host"), std::string::npos) << output;
}

// ---------------------------------------------------------------------------
// do_show (act_wiz.cpp:2357)
// ---------------------------------------------------------------------------

TEST(ActWizInspection, DoShowFormatsOptionsListForNoArgument)
{
    SoloCharacterContext viewer;
    viewer.character.player.level = LEVEL_IMPL;
    char argument[] = "";
    do_show(&viewer.character, argument, nullptr, 0, 0);
    const std::string output = viewer.descriptor.output;
    EXPECT_NE(output.find("Show options:\n\r"), std::string::npos) << output;
    EXPECT_NE(output.find("zones"), std::string::npos) << output;
    EXPECT_NE(output.find("player"), std::string::npos) << output;
}

TEST(ActWizInspection, DoShowFormatsZoneSelfBranch)
{
    RoomStatContext context;
    context.zone_table_scope.zone().name = const_cast<char*>("The Shire");
    char argument[] = "zones .";
    do_show(&context.character, argument, nullptr, 0, 0);
    EXPECT_NE(std::string(context.descriptor.output).find("The Shire"), std::string::npos)
        << context.descriptor.output;
}

TEST(ActWizInspection, DoShowFormatsZoneMissingArgumentBranch)
{
    SoloCharacterContext viewer;
    viewer.character.player.level = LEVEL_IMPL;
    char argument[] = "zones";
    do_show(&viewer.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(viewer.descriptor.output), "Show which zone?\n\r");
}

TEST(ActWizInspection, DoShowFormatsAliasesListLine)
{
    // get_char_vis()'s CAN_SEE() check reads room light state via world[] --
    // needs a real room under both characters rather than the world-less
    // SoloCharacterContext.
    ScopedTestWorld test_world;
    SoloCharacterContext viewer;
    viewer.character.player.level = LEVEL_IMPL;
    viewer.character.in_room = 0;
    char_data target { };
    clear_char(&target, MOB_VOID);
    // Releases target.profs/skills/knowledge (clear_char() heap
    // allocations) at scope exit (Phase 5 T6 leak sweep).
    ScopedClearCharFields target_cleanup { target };
    target.player.name = const_cast<char*>("Sam");
    target.in_room = 0;
    target.next = character_list;
    character_list = &target;

    alias_list first_alias { };
    std::snprintf(first_alias.keyword, sizeof(first_alias.keyword), "%s", "gr");
    first_alias.command = const_cast<char*>("greet");
    first_alias.next = nullptr;
    target.specials.alias = &first_alias;

    char argument[] = "aliases Sam";
    do_show(&viewer.character, argument, nullptr, 0, 0);

    character_list = target.next;

    const std::string output = viewer.descriptor.output;
    EXPECT_NE(output.find("Sam has the following aliases defined:\n\r"), std::string::npos)
        << output;
    EXPECT_NE(output.find("gr                  : greet\n\r"), std::string::npos) << output;

    // first_alias is a STACK object, not a CREATE1()'d heap node -- since RAII
    // T4, target.specials.alias is an owning wrapper whose destructor calls
    // free_alias_list() (db.cpp) when `target` (a genuine C++ local, not a
    // calloc'd/placement-new'd stand-in) goes out of scope below. Reseating to
    // nullptr here (a plain pointer overwrite -- see owned_alias_list's class
    // comment in structs.h) avoids that destructor attempting to free() a
    // stack address.
    target.specials.alias = nullptr;
}

TEST(ActWizInspection, DoShowFormatsAliasesNoneDefinedLine)
{
    // See DoShowFormatsAliasesListLine: get_char_vis() needs a real room.
    ScopedTestWorld test_world;
    SoloCharacterContext viewer;
    viewer.character.player.level = LEVEL_IMPL;
    viewer.character.in_room = 0;
    char_data target { };
    clear_char(&target, MOB_VOID);
    // Releases target.profs/skills/knowledge (clear_char() heap
    // allocations) at scope exit (Phase 5 T6 leak sweep).
    ScopedClearCharFields target_cleanup { target };
    target.player.name = const_cast<char*>("Sam");
    target.in_room = 0;
    target.next = character_list;
    character_list = &target;
    target.specials.alias = nullptr;

    char argument[] = "aliases Sam";
    do_show(&viewer.character, argument, nullptr, 0, 0);

    character_list = target.next;

    EXPECT_NE(std::string(viewer.descriptor.output).find("Sam has no aliases defined.\n\r"),
        std::string::npos)
        << viewer.descriptor.output;
}

// ---------------------------------------------------------------------------
// ActWizWorldManip -- Phase 4 Wave 3 Task 6 (Chunk W2 -- act_wiz.cpp
// world-manipulation family: do_at, do_goto, do_trans, do_teleport, do_load,
// do_purge, do_zreset, do_switch, do_return, do_snoop, do_force). Same
// binding pattern as every other chunk this wave: these pin the CURRENT
// byte-for-byte output of every fixture-reachable error/usage line --
// confirmed passing against the pre-conversion source -- before this
// chunk's sprintf sites convert to std::format, and green again after.
//
// Deliberately NOT unit-tested here (documented exclusions, not oversights):
//  - do_trans (act_wiz.cpp:321), do_teleport (act_wiz.cpp:369), do_switch
//    (act_wiz.cpp:1257), do_return (act_wiz.cpp:1297), do_snoop
//    (act_wiz.cpp:1204): every branch in all five is a literal
//    send_to_char()/act() call -- zero sprintf/strcat/strcpy composition
//    anywhere in any of them (verified by inspection in Step 1) -- so there
//    is nothing for this task's std::format transform to touch. Covered by
//    the boot golden smoke test for compile-level regressions only.
//  - do_goto's two `sprintf(buf, "%s", ...)` poofin/poofout lines DO convert
//    (see the transform commit) but are only reachable through do_goto's
//    success path (an actual room transfer, `act()` to two rooms, a
//    `do_look()`); that mutation itself is out of scope per the SCOPE NOTE
//    below, so only the shared `find_target_room()` failure path (below) is
//    pinned directly for do_goto.
//  - do_load's mob/obj creation success paths (act_wiz.cpp:1336-1365) hand a
//    freshly-`read_mobile()`/`read_object()`-allocated pointer into the live
//    world graph via char_to_room()/obj_to_char() -- ownership leaves the
//    function, so per the transform catalog's RAII item this pointer is NOT
//    wrapped in RAII (recorded in the transform commit body). do_load has no
//    sprintf/strcat of its own on ANY path (every message is a literal
//    send_to_char()), so there is no format conversion here either -- only
//    its four fixture-reachable usage/error lines are pinned below.
//  - do_load's `if ((number = atoi(buf2)) < 0)` "A NEGATIVE number??"
//    branch (act_wiz.cpp:1332-1335) is unreachable dead code: the preceding
//    guard already requires `isdigit(*buf2)` to proceed past the usage
//    check, and atoi() of a string starting with a digit character can
//    never yield a negative value. Not tested (nothing exercises it in
//    production either).
//  - do_purge's "zone" branch (act_wiz.cpp:1424-1441), single-target success
//    branch (extract_char/extract_obj, act_wiz.cpp:1443-1485), and
//    no-argument whole-room branch (act_wiz.cpp:1486-1512) all mutate the
//    world graph (extract_char/extract_obj/Crash_idlesave/close_socket) --
//    out of scope per the SCOPE NOTE below. Their sprintf-to-mudlog() lines
//    (act_wiz.cpp:1438, 1469) still convert (transform commit), pinned only
//    by line-by-line diff review against catalog item 2, not by a unit
//    test. Only the side-effect-free early-return branches (silent
//    NOWHERE-room guard, "I don't know anyone or anything by that name")
//    are pinned below.
//  - do_zreset's `arg == '*'` (reset the whole world) and successful
//    single-zone reset branches (act_wiz.cpp:2103-2121) call reset_zone(),
//    a world mutation -- out of scope. Its sprintf-to-send_to_char and
//    sprintf-to-mudlog lines (act_wiz.cpp:2118, 2120) still convert
//    (transform commit: catalog items 1 and 2 respectively), pinned only by
//    diff review. Only the side-effect-free NPC/empty-argument/
//    unmatched-zone-number branches are pinned below.
//  - do_force's "all"/"room" broadcast branches and the found-victim branch
//    of its name-target path (act_wiz.cpp:1913-1961) all call
//    command_interpreter() on a live character and/or walk descriptor_list
//    to message other connected players -- world/session mutation, out of
//    scope. Its unconditional buf1/buf2 sprintf lines (act_wiz.cpp:1904-1905)
//    and the three duplicated "(GC) ... forced ... to ..." log lines
//    (act_wiz.cpp:1921, 1931, 1948) still convert (transform commit), pinned
//    by diff review; only the missing-argument and victim-not-found early
//    returns (which still exercise the unconditional buf1/buf2 composition
//    every call performs) are pinned below.
//
// SCOPE NOTE (task brief): do_load's and do_purge's object/mob creation
// hands ownership into the world graph -- the RAII catalog item does NOT
// apply to those pointers; only formatting composition converts in this
// chunk, and even that formatting conversion is absent from do_load (no
// sprintf at all). Success paths with world side effects (teleport/load/
// purge/reset/force-broadcast) are NOT unit-tested here; the boot golden +
// dual local gate cover compile-level regressions on those paths, and the
// formatting conversions living on them are pinned by reviewing the diff
// line-by-line against transform catalog items 1/2 equivalence.

TEST(ActWizWorldManip, DoAtRejectsEmptyRoomArgument)
{
    SoloCharacterContext context;
    char argument[] = "";
    do_at(&context.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.descriptor.output), "You must supply a room number or a name.\n\r");
}

TEST(ActWizWorldManip, DoAtRejectsUnknownRoomNumber)
{
    // find_target_room()'s real_room() lookup dereferences world[] --
    // BASE_WORLD must be allocated (room_data::operator[] aborts otherwise)
    // even though the requested room number (999999) never matches any
    // room in this 1-room test world.
    ScopedTestWorld test_world;
    SoloCharacterContext context;
    char argument[] = "999999 look";
    do_at(&context.character, argument, nullptr, 0, 0);
    EXPECT_NE(std::string(context.descriptor.output).find("No room exists with that number."),
        std::string::npos)
        << context.descriptor.output;
}

// do_goto: pins the invalid-target error line (no world mutation). Shares
// find_target_room() with do_at above.
TEST(ActWizWorldManip, DoGotoRejectsUnknownRoomWithErrorLine)
{
    ScopedTestWorld test_world;
    SoloCharacterContext context;
    char argument[] = " 999999";
    do_goto(&context.character, argument, nullptr, 0, 0);
    EXPECT_NE(std::string(context.descriptor.output).find("No room exists with that number."),
        std::string::npos)
        << context.descriptor.output;
}

TEST(ActWizWorldManip, DoLoadRejectsUsageWithMissingArguments)
{
    SoloCharacterContext context;
    char argument[] = "";
    do_load(&context.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.descriptor.output), "Usage: load { obj | mob } <number>\n\r");
}

TEST(ActWizWorldManip, DoLoadRejectsUnknownMobileNumber)
{
    // real_mobile()'s binary search dereferences mob_index[] -- give it one
    // fabricated entry (virt 1) so the "not found" path (requested number
    // 42 never matches) exercises real_mobile() safely instead of walking a
    // null mob_index.
    ScopedMobIndexEntry mob_index_entry(1, false);
    SoloCharacterContext context;
    char argument[] = "mob 42";
    do_load(&context.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.descriptor.output), "There is no monster with that number.\n\r");
}

TEST(ActWizWorldManip, DoLoadRejectsUnknownObjectNumber)
{
    ScopedObjIndexEntry obj_index_entry(1, false);
    SoloCharacterContext context;
    char argument[] = "obj 42";
    do_load(&context.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.descriptor.output), "There is no object with that number.\n\r");
}

TEST(ActWizWorldManip, DoLoadRejectsUnrecognizedType)
{
    SoloCharacterContext context;
    char argument[] = "foo 5";
    do_load(&context.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.descriptor.output), "That'll have to be either 'obj' or 'mob'.\n\r");
}

TEST(ActWizWorldManip, DoPurgeSilentlyIgnoresCallerWithNoRoom)
{
    // clear_char() leaves in_room == NOWHERE; do_purge()'s very first guard
    // returns immediately without emitting anything.
    SoloCharacterContext context;
    char argument[] = "";
    do_purge(&context.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.descriptor.output), "");
}

TEST(ActWizWorldManip, DoPurgeReportsUnknownTarget)
{
    ScopedTestWorld test_world;
    SoloCharacterContext context;
    context.character.in_room = 0;
    char argument[] = "NoSuchCreatureOrObject";
    do_purge(&context.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.descriptor.output),
        "I don't know anyone or anything by that name.\n\r");
}

TEST(ActWizWorldManip, DoZresetRejectsNpcCaller)
{
    char_data npc { };
    descriptor_data npc_descriptor { };
    clear_char(&npc, MOB_ISNPC);
    // Releases npc.profs (clear_char()'s only heap allocation for
    // MOB_ISNPC) at scope exit (Phase 5 T6 leak sweep).
    ScopedClearCharFields npc_cleanup { npc };
    npc.specials2.act = MOB_ISNPC;
    reset_capturing_descriptor(npc_descriptor, &npc);
    npc.desc = &npc_descriptor;

    char argument[] = "1";
    do_zreset(&npc, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(npc_descriptor.output), "Homie don't play that!\n\r");
}

TEST(ActWizWorldManip, DoZresetRejectsEmptyArgument)
{
    SoloCharacterContext context;
    char argument[] = "";
    do_zreset(&context.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.descriptor.output), "You must specify a zone.\n\r");
}

TEST(ActWizWorldManip, DoZresetReportsInvalidZoneNumber)
{
    // ScopedZoneTable seeds a single zone numbered 0; requesting zone 999
    // (not '*' and not '.') walks the whole table without a match and falls
    // through to the "invalid" branch without ever calling reset_zone().
    ScopedZoneTable zone_table_scope(0);
    SoloCharacterContext context;
    char argument[] = "999";
    do_zreset(&context.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.descriptor.output), "Invalid zone number.\n\r");
}

TEST(ActWizWorldManip, DoForceRejectsMissingNameOrCommand)
{
    // do_force() unconditionally composes buf1/buf2 (GET_NAME(ch) + to_force)
    // before checking whether name/to_force were even supplied, so
    // player.name must be set even for this early-return branch.
    SoloCharacterContext context;
    context.character.player.name = const_cast<char*>("Gandalf");
    char argument[] = "";
    do_force(&context.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.descriptor.output), "Whom do you wish to force do what?\n\r");
}

TEST(ActWizWorldManip, DoForceReportsNoSuchVictim)
{
    // get_char_vis() -> get_char_room_vis() dereferences world[] via
    // ch->in_room, so BASE_WORLD must be allocated even though the named
    // victim is never linked into it (or into character_list).
    ScopedTestWorld test_world;
    SoloCharacterContext context;
    context.character.player.name = const_cast<char*>("Gandalf");
    context.character.in_room = 0;
    char argument[] = "NoSuchVictim quit";
    do_force(&context.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.descriptor.output), "No-one by that name here...\n\r");
}

// ---------------------------------------------------------------------------
// ActWizPlayerAdmin -- Phase 4 Wave 3 Task 7 (Chunk W3 -- act_wiz.cpp
// player-administration family: do_advance, do_restore, do_invis, do_dc,
// do_wizlock (+ the wizlock_msg RAII conversion), do_wizutil, do_wizset,
// do_delete, do_register, do_setfree; do_account/do_whoacct live in the
// separate act_wiz_tests.cpp file and are NOT re-declared/re-tested here --
// see below). Same binding pattern as every other chunk this wave: these pin
// the CURRENT byte-for-byte output of every fixture-reachable error/usage
// line -- confirmed passing against the pre-conversion source -- before this
// chunk's sprintf/strcat sites (and the wizlock_msg char*->std::string RAII
// conversion) convert, and green again after.
//
// wizlock_msg consumer-guard trace (task brief Step 1): interpre.cpp's two
// SEND_TO_Q(wizlock_msg, d) sites (complete_existing_character_login's
// restrict-gate at interpre.cpp:2751, and the CON_NMECNF new-character
// wizlock gate at interpre.cpp:3012) are both reached only when `restrict`
// is non-zero. `restrict` is set non-zero in exactly two places in the whole
// codebase: (1) do_wizlock, which unconditionally reassigns wizlock_msg in
// the very same call before returning -- so by the time THAT path can raise
// restrict, wizlock_msg is always freshly set; and (2) comm.cpp:476
// (`restrict = startup_options.restrict_game ? 1 : 0`), driven by the `-r`
// command-line flag parsed at comm.cpp:285-287, which sets restrict=1 at
// boot WITHOUT ever calling do_wizlock. In that second case wizlock_msg is
// still its static initializer (`char* wizlock_msg = 0`, act_wiz.cpp) --
// nullptr -- so a level-0 (or wizlock-blocked) login/new-character attempt
// under `-r` reaches SEND_TO_Q(nullptr, d) -> write_to_output() ->
// `strlen(txt)` on a null pointer: a genuine, currently-reachable crash, not
// a hypothetical one. Converting wizlock_msg to `std::string wizlock_msg;`
// (default-constructed to "") fixes this as a side effect: `.c_str()` on an
// empty string is a valid pointer to "", so `strlen()` returns 0 and
// SEND_TO_Q sends nothing instead of crashing (both consumer sites then send
// an empty payload followed by "\n\r" in place of what would have been the
// crash). Per the task brief this is explicitly a sanctioned "note it in the
// commit" latent-crash fix, not a silent behavior change on the SET path
// (which stays byte-for-byte identical) -- no `if (!wizlock_msg.empty())`
// guard was added because the OLD code had none on the set path either, and
// none is needed to preserve set-path parity.
//
// Deliberately NOT unit-tested here (documented exclusions, not oversights):
//  - do_account (act_wiz.cpp:3173) and do_whoacct (act_wiz.cpp:3442): both
//    are exhaustively characterized already by the 13 pre-existing ActWiz.*
//    tests in act_wiz_tests.cpp (the characterization BASE this task must
//    keep green, per the brief's caution). do_account has no sprintf/strcat
//    composition of its own left to convert -- it was already rewritten onto
//    std::string/std::format by the account-management merge. do_whoacct's
//    remaining char line[256]/strcpy/strcat header composition and its
//    snprintf(buf, sizeof(buf), "%3d %-26.26s %-12.12s %-16.16s %s\n\r", ...)
//    row DO convert in the transform commit (catalog items 1 and 8), but
//    every byte of that row format (including the "%-26.26"/"%-12.12"/
//    "%-16.16" truncation widths, the sanitized-field substitutions, and the
//    singular/plural session-count line) is already pinned exactly by
//    WhoAcctShowsAuthenticatedAccountsAndCurrentCharacterOrMenuState,
//    WhoAcctListsDuplicateAuthenticatedSessionsSeparatelyAndSkipsClosingDescriptors,
//    WhoAcctShowsCharacterSelectStateAndSkipsPendingVerificationSessions,
//    WhoAcctSanitizesDisplayedAccountAndHostFields, and
//    WhoAcctFormatsLongFieldsIntoStableColumns -- so no additional pins are
//    needed here; the transform is verified by keeping those 13 tests green.
//  - do_wizset (act_wiz.cpp:2683), the file's single biggest function: only
//    its side-effect-free early-return branches (usage, NPC-caller, and the
//    is_player/default "not found" lookups) are pinned below. The ~60
//    field-table switch cases and their final BINARY/NUMBER/MISC composition
//    lines (act_wiz.cpp:3118-3126) all require a fully-permission-cleared,
//    resolved `vict` (and, for non-NPC victims, a matching player_table
//    entry via find_name()) to reach -- reproducing that for every case is
//    combinatorially expensive for a single sub-pass. Those lines still
//    convert (transform commit: catalog items 1/6), verified by direct diff
//    review against the pre-conversion sprintf format strings plus the dual
//    local gate / boot golden, not by a per-case unit test here.
//  - do_wizutil (act_wiz.cpp:2152)'s per-subcommand branches past victim
//    resolution (freeze/thaw/reroll/retire/... success paths, act_wiz.cpp:
//    2209-2341) mutate the victim (SET_BIT/affect_remove/roll_abilities/
//    retire/...) and log via mudlog -- out of scope per the world-mutation
//    convention established in ActWizWorldManip above. Only the four
//    side-effect-free early-return branches (NPC caller, malformed general
//    call, missing name, victim not found) are pinned below.
//  - do_delete (act_wiz.cpp:3140)'s success path (close_socket/extract_char/
//    Crash_delete_file/move_char_deleted, act_wiz.cpp:3159-3170) mutates the
//    world/player table and touches disk -- out of scope. Only its two
//    side-effect-free early returns (wrong password, player not found) are
//    pinned below.
//  - do_register (act_wiz.cpp:3500) is a long flat dispatcher over
//    mobile/object/room/player/top/script listings, each walking a
//    process-global index table (mob_index/obj_index/world/player_table/
//    script_table) that would need substantial fixture reconstruction to
//    exercise deterministically -- the same class of exclusion do_show's
//    "stats"/"death"/"godrooms" branches already document above. Only the
//    two branches reachable without any of those tables populated (the
//    top-level usage message, and the final "mobile, object, player, script
//    or room only" fallback for an unrecognized type token) are pinned
//    below.
//  - do_rehash (act_wiz.cpp:3843) sends its only composed line
//    ("(GC) ... rehashed affection, was %d, now %d.") to mudlog() only, never
//    to send_to_char -- there is no descriptor-captured text for a unit test
//    to assert on, and exercising it meaningfully needs a populated world/
//    character_list to produce a non-trivial count. Its sprintf converts in
//    the transform commit (catalog item 2: buf reused by mudlog), verified
//    by diff review and the dual local gate / boot golden only.

namespace {

// Saves/restores the `restrict` global (act_wiz.cpp/interpre.cpp/comm.cpp)
// around do_wizlock tests, which unconditionally assign it whenever the
// caller supplies a numeric first argument.
class ScopedRestrictLevel {
public:
    ScopedRestrictLevel()
        : m_previous(restrict)
    {
    }

    ~ScopedRestrictLevel() { restrict = m_previous; }

private:
    int m_previous;
};

// Saves/restores global_release_flag (structs.h, default 1) around
// do_setfree tests, which read and toggle it directly.
class ScopedGlobalReleaseFlag {
public:
    ScopedGlobalReleaseFlag()
        : m_previous(global_release_flag)
    {
    }

    ~ScopedGlobalReleaseFlag() { global_release_flag = m_previous; }

private:
    int m_previous;
};

// Forces find_player_in_table() (utility.cpp) to report "not found" for any
// name/idnum without needing a real player_table allocation: its very first
// comparison is `i > top_of_p_table`, short-circuiting before it ever
// dereferences player_table[i], so top_of_p_table alone is sufficient here.
class ScopedEmptyPlayerTable {
public:
    ScopedEmptyPlayerTable()
        : m_previous(top_of_p_table)
    {
        top_of_p_table = -1;
    }

    ~ScopedEmptyPlayerTable() { top_of_p_table = m_previous; }

private:
    int m_previous;
};

// Mirrors act_wiz_tests.cpp's ScopedDescriptorList (mirrored, not included,
// per this file's per-file-fixture convention) for do_dc's connection-list
// walk.
class ScopedDescriptorListForDc {
public:
    ScopedDescriptorListForDc()
        : m_previous(descriptor_list)
    {
        descriptor_list = nullptr;
    }

    ~ScopedDescriptorListForDc() { descriptor_list = m_previous; }

private:
    descriptor_data* m_previous;
};

} // namespace

// ---------------------------------------------------------------------------
// do_wizlock (act_wiz.cpp:1771)
// ---------------------------------------------------------------------------

// Pins the level-gate status line, "now" branch (argument given).
TEST(ActWizPlayerAdmin, DoWizlockFormatsLevelGateStatusLineNow)
{
    ScopedRestrictLevel restrict_scope;
    SoloCharacterContext context; // PC at LEVEL_IMPL so do_wizlock's caller passes freely
    char argument[] = " 20";
    do_wizlock(&context.character, argument, nullptr, 0, 0);
    EXPECT_TRUE(strstr(context.descriptor.output,
                    "Only level 20 and above may enter the game now.\n")
        != nullptr)
        << context.descriptor.output;
}

TEST(ActWizPlayerAdmin, DoWizlockFormatsFullyOpenStatusLineCurrently)
{
    ScopedRestrictLevel restrict_scope;
    restrict = 0;
    SoloCharacterContext context;
    char argument[] = "";
    do_wizlock(&context.character, argument, nullptr, 0, 0);
    // restrict stays 0 ("currently" branch, no numeric argument), so the
    // `sprintf(buf, "Message set to:  %s", wizlock_msg)` reassignment is
    // skipped (guarded by `if (restrict != 0)`) -- but the send_to_char(buf,
    // ch) two lines below that sprintf is NOT itself inside that guard, so
    // `buf` still holds the untouched status line and gets sent a SECOND
    // time. This is a pre-existing quirk of the original code (verified
    // against unmodified act_wiz.cpp), not a bug this task introduces or
    // fixes -- pinned verbatim.
    EXPECT_EQ(std::string(context.descriptor.output),
        "The game is currently completely open.\n"
        "The game is currently completely open.\n");
}

// Pins both the "closed to new players" status line AND the "Message set
// to:" echo of the DEFAULT wizlock message (no text argument supplied).
TEST(ActWizPlayerAdmin, DoWizlockFormatsClosedToNewPlayersStatusLineWithDefaultMessage)
{
    ScopedRestrictLevel restrict_scope;
    SoloCharacterContext context;
    char argument[] = "1";
    do_wizlock(&context.character, argument, nullptr, 0, 0);
    const std::string expected = std::string("The game is now closed to new players.\n") + "Message set to:  " + wizlock_default;
    EXPECT_EQ(std::string(context.descriptor.output), expected);
}

// Pins the "Message set to:" echo of a CUSTOM wizlock message (text
// argument supplied), appended to the default's "\n\r" the same way the
// default branch appends wizlock_default verbatim.
TEST(ActWizPlayerAdmin, DoWizlockFormatsMessageSetToLineWithCustomMessage)
{
    ScopedRestrictLevel restrict_scope;
    SoloCharacterContext context;
    char argument[] = "5 Server going down for maintenance";
    do_wizlock(&context.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.descriptor.output),
        "Only level 5 and above may enter the game now.\n"
        "Message set to:  Server going down for maintenance\n\r");
}

TEST(ActWizPlayerAdmin, DoWizlockRejectsInvalidValue)
{
    ScopedRestrictLevel restrict_scope;
    SoloCharacterContext context;
    char argument[] = "-1";
    do_wizlock(&context.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.descriptor.output), "Invalid wizlock value.\n\r");
}

// ---------------------------------------------------------------------------
// do_advance (act_wiz.cpp:1515)
// ---------------------------------------------------------------------------

TEST(ActWizPlayerAdmin, DoAdvanceRejectsEmptyName)
{
    SoloCharacterContext context;
    char argument[] = "";
    do_advance(&context.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.descriptor.output), "Advance who?\n\r");
}

TEST(ActWizPlayerAdmin, DoAdvanceReportsTargetNotFound)
{
    ScopedTestWorld test_world;
    SoloCharacterContext context;
    context.character.in_room = 0;
    char argument[] = "NoSuchVictim";
    do_advance(&context.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.descriptor.output), "That player is not here.\n\r");
}

// ---------------------------------------------------------------------------
// do_restore (act_wiz.cpp:1612)
// ---------------------------------------------------------------------------

TEST(ActWizPlayerAdmin, DoRestoreRequestsTargetWhenArgumentEmpty)
{
    SoloCharacterContext context;
    char argument[] = "";
    do_restore(&context.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.descriptor.output), "Whom do you wish to restore?\n\r");
}

// get_char() (handler.cpp) only walks character_list -- no world[]
// dependency, unlike get_char_vis() -- so no ScopedTestWorld is needed here.
TEST(ActWizPlayerAdmin, DoRestoreReportsTargetNotFound)
{
    SoloCharacterContext context;
    char argument[] = "NoSuchVictim";
    do_restore(&context.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.descriptor.output), "No-one by that name in the world.\n\r");
}

// ---------------------------------------------------------------------------
// do_invis (act_wiz.cpp:1643)
// ---------------------------------------------------------------------------

TEST(ActWizPlayerAdmin, DoInvisRejectsNpcCaller)
{
    char_data npc { };
    descriptor_data npc_descriptor { };
    clear_char(&npc, MOB_ISNPC);
    // Releases npc.profs (clear_char()'s only heap allocation for
    // MOB_ISNPC) at scope exit (Phase 5 T6 leak sweep).
    ScopedClearCharFields npc_cleanup { npc };
    npc.specials2.act = MOB_ISNPC;
    reset_capturing_descriptor(npc_descriptor, &npc);
    npc.desc = &npc_descriptor;

    char argument[] = "";
    do_invis(&npc, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(npc_descriptor.output), "Yeah.. like a mob knows how to bend light.\n\r");
}

TEST(ActWizPlayerAdmin, DoInvisTogglesOffWhenAlreadyInvisibleAndNoArgument)
{
    SoloCharacterContext context;
    GET_INVIS_LEV(&context.character) = GET_LEVEL(&context.character);
    char argument[] = "";
    do_invis(&context.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.descriptor.output), "You are now fully visible.\n\r");
}

TEST(ActWizPlayerAdmin, DoInvisTogglesOnAtOwnLevelWhenNoArgument)
{
    SoloCharacterContext context;
    GET_INVIS_LEV(&context.character) = 0;
    char argument[] = "";
    do_invis(&context.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.descriptor.output),
        std::format("Your invisibility level is {}.\n\r", GET_LEVEL(&context.character)));
}

TEST(ActWizPlayerAdmin, DoInvisRejectsLevelAboveOwnLevel)
{
    SoloCharacterContext context;
    char argument[] = "200";
    do_invis(&context.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.descriptor.output), "You can't go invisible above your own level.\n\r");
}

TEST(ActWizPlayerAdmin, DoInvisSetsSpecificLevelBelowOwnLevel)
{
    SoloCharacterContext context;
    char argument[] = "50";
    do_invis(&context.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.descriptor.output), "Your invisibility level is now 50.\n\r");
}

TEST(ActWizPlayerAdmin, DoInvisTogglesOffViaExplicitZeroArgument)
{
    SoloCharacterContext context;
    char argument[] = "0";
    do_invis(&context.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.descriptor.output), "You are now fully visible.\n\r");
}

// ---------------------------------------------------------------------------
// do_dc (act_wiz.cpp:1734)
// ---------------------------------------------------------------------------

TEST(ActWizPlayerAdmin, DoDcRejectsNpcCaller)
{
    char_data npc { };
    descriptor_data npc_descriptor { };
    clear_char(&npc, MOB_ISNPC);
    // Releases npc.profs (clear_char()'s only heap allocation for
    // MOB_ISNPC) at scope exit (Phase 5 T6 leak sweep).
    ScopedClearCharFields npc_cleanup { npc };
    npc.specials2.act = MOB_ISNPC;
    reset_capturing_descriptor(npc_descriptor, &npc);
    npc.desc = &npc_descriptor;

    char argument[] = "1";
    do_dc(&npc, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(npc_descriptor.output), "Monsters can't cut connections... leave me alone.\n\r");
}

TEST(ActWizPlayerAdmin, DoDcReportsUsageWhenArgumentIsNotNumeric)
{
    SoloCharacterContext context;
    char argument[] = "notanumber";
    do_dc(&context.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.descriptor.output), "Usage: DC <connection number> (type USERS for a list)\n\r");
}

TEST(ActWizPlayerAdmin, DoDcReportsNoSuchConnection)
{
    ScopedDescriptorListForDc descriptor_list_scope;
    SoloCharacterContext context;
    char argument[] = "42";
    do_dc(&context.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.descriptor.output), "No such connection.\n\r");
}

// ---------------------------------------------------------------------------
// do_wizutil (act_wiz.cpp:2152)
// ---------------------------------------------------------------------------

TEST(ActWizPlayerAdmin, DoWizutilRejectsNpcCaller)
{
    char_data npc { };
    descriptor_data npc_descriptor { };
    clear_char(&npc, MOB_ISNPC);
    // Releases npc.profs (clear_char()'s only heap allocation for
    // MOB_ISNPC) at scope exit (Phase 5 T6 leak sweep).
    ScopedClearCharFields npc_cleanup { npc };
    npc.specials2.act = MOB_ISNPC;
    reset_capturing_descriptor(npc_descriptor, &npc);
    npc.desc = &npc_descriptor;

    char argument[] = "";
    do_wizutil(&npc, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(npc_descriptor.output), "You're just an unfrozen caveman NPC.\n\r");
}

TEST(ActWizPlayerAdmin, DoWizutilReportsMalformedGeneralCallWhenWtlIsNull)
{
    SoloCharacterContext context;
    char argument[] = "";
    do_wizutil(&context.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.descriptor.output), "Wrong call to wizutils. Consult implementors, please.\n\r");
}

TEST(ActWizPlayerAdmin, DoWizutilRequestsNameWhenMissing)
{
    SoloCharacterContext context;
    char argument[] = "";
    do_wizutil(&context.character, argument, nullptr, 0, SCMD_FREEZE);
    EXPECT_EQ(std::string(context.descriptor.output), "Yes, but for whom?!?\n\r");
}

TEST(ActWizPlayerAdmin, DoWizutilReportsTargetNotFound)
{
    ScopedTestWorld test_world;
    SoloCharacterContext context;
    context.character.in_room = 0;
    char argument[] = "NoSuchVictim";
    do_wizutil(&context.character, argument, nullptr, 0, SCMD_FREEZE);
    EXPECT_EQ(std::string(context.descriptor.output), "There is no such player.\n\r");
}

// ---------------------------------------------------------------------------
// do_wizset (act_wiz.cpp:2683)
// ---------------------------------------------------------------------------

TEST(ActWizPlayerAdmin, DoWizsetReportsUsageWhenNameOrFieldMissing)
{
    SoloCharacterContext context;
    char argument[] = "";
    do_wizset(&context.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.descriptor.output), "Usage: wizset <victim> <field> <value>\n\r");
}

TEST(ActWizPlayerAdmin, DoWizsetRejectsNpcCaller)
{
    char_data npc { };
    descriptor_data npc_descriptor { };
    clear_char(&npc, MOB_ISNPC);
    // Releases npc.profs (clear_char()'s only heap allocation for
    // MOB_ISNPC) at scope exit (Phase 5 T6 leak sweep).
    ScopedClearCharFields npc_cleanup { npc };
    npc.specials2.act = MOB_ISNPC;
    reset_capturing_descriptor(npc_descriptor, &npc);
    npc.desc = &npc_descriptor;

    // Both name and field must be non-empty to get past the usage check
    // above and reach the IS_NPC(ch) gate; the target name itself ("orc")
    // is never looked up since this branch returns first.
    char argument[] = "orc field on";
    do_wizset(&npc, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(npc_descriptor.output), "None of that!\n\r");
}

// get_player_vis() (handler.cpp) only walks character_list -- no world[]
// dependency -- so no ScopedTestWorld is needed for the "player" subcommand.
TEST(ActWizPlayerAdmin, DoWizsetReportsNoSuchPlayerForPlayerSubcommand)
{
    SoloCharacterContext context;
    char argument[] = "player NoSuchPlayer field on";
    do_wizset(&context.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.descriptor.output), "There is no such player.\n\r");
}

TEST(ActWizPlayerAdmin, DoWizsetReportsNoSuchCreatureForDefaultLookup)
{
    ScopedTestWorld test_world;
    SoloCharacterContext context;
    context.character.in_room = 0;
    char argument[] = "NoSuchCreature field on";
    do_wizset(&context.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.descriptor.output), "There is no such creature.\n\r");
}

// ---------------------------------------------------------------------------
// do_delete (act_wiz.cpp:3140)
// ---------------------------------------------------------------------------

// Note the exact literal (no trailing "\n\r") matches act_wiz.cpp's source.
TEST(ActWizPlayerAdmin, DoDeleteRejectsIncorrectPassword)
{
    SoloCharacterContext context;
    char argument[] = "somebody wrongpassword";
    do_delete(&context.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.descriptor.output), "Incorrect or missing password");
}

// Note the exact literal (no trailing "\n\r") matches act_wiz.cpp's source.
TEST(ActWizPlayerAdmin, DoDeleteReportsNoSuchPlayer)
{
    ScopedEmptyPlayerTable player_table_scope;
    SoloCharacterContext context;
    char argument[] = "somebody rots";
    do_delete(&context.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.descriptor.output), "No such player");
}

// ---------------------------------------------------------------------------
// do_register (act_wiz.cpp:3500)
// ---------------------------------------------------------------------------

// Even the empty-argument usage path reads world[ch->in_room].number (the
// `if (!*arg2) zonnum = world[ch->in_room].number / 100;` fallback runs
// before the usage check), so BASE_WORLD must be allocated here too.
TEST(ActWizPlayerAdmin, DoRegisterReportsUsageWhenNoTypeOrZoneGiven)
{
    ScopedTestWorld test_world;
    SoloCharacterContext context;
    context.character.in_room = 0;
    char argument[] = "";
    do_register(&context.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.descriptor.output), "Usage: register <room|mobile|object> [zone number].\n\r");
}

// Supplying a numeric zone argument skips the world[]-touching fallback
// above entirely, so this path needs no ScopedTestWorld.
TEST(ActWizPlayerAdmin, DoRegisterReportsUnknownTypeFallback)
{
    SoloCharacterContext context;
    char argument[] = "unknowntype 5";
    do_register(&context.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.descriptor.output),
        "You can see register for mobile, object, player, script or room only.\n\r");
}

// ---------------------------------------------------------------------------
// do_setfree (act_wiz.cpp:3812)
// ---------------------------------------------------------------------------
// No sprintf/strcat anywhere in do_setfree -- every branch is a literal
// send_to_char() -- so there is no format site here for this task's
// transform to touch. Exercised anyway (all five branches), same rationale
// as do_vnum's LightlyExercisesUsageMessage precedent above: a smoke check
// since it's in this chunk's read range, and because it directly reads/
// writes the process-global global_release_flag that RELEASE() (utils.h)
// gates everywhere else in the codebase.

TEST(ActWizPlayerAdmin, DoSetfreeReportsAllowedStateWhenFlagIsSet)
{
    ScopedGlobalReleaseFlag release_flag_scope;
    global_release_flag = 1;
    SoloCharacterContext context;
    char argument[] = "";
    do_setfree(&context.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.descriptor.output), "RELEASE is allowed.\n\r");
}

TEST(ActWizPlayerAdmin, DoSetfreeReportsFakedStateWhenFlagIsClear)
{
    ScopedGlobalReleaseFlag release_flag_scope;
    global_release_flag = 0;
    SoloCharacterContext context;
    char argument[] = "";
    do_setfree(&context.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.descriptor.output), "RELEASE is faked - be careful.\n\r");
    // Restore before `context` (declared above) is destroyed below: its
    // RAII cleanup members RELEASE() context's clear_char()/large_outbuf
    // allocations, which -- correctly, matching production's own RELEASE()
    // macro -- become no-ops while global_release_flag is 0 (Phase 5 T6
    // leak sweep; this test's own point is to verify that no-op behavior for
    // do_setfree's *own* effect, not to leave it disabled for teardown too).
    global_release_flag = 1;
}

TEST(ActWizPlayerAdmin, DoSetfreeEnablesReleaseOnArgumentOn)
{
    ScopedGlobalReleaseFlag release_flag_scope;
    SoloCharacterContext context;
    char argument[] = "on";
    do_setfree(&context.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.descriptor.output), "You enabled pointer release.\n\r");
    EXPECT_EQ(global_release_flag, 1);
}

TEST(ActWizPlayerAdmin, DoSetfreeDisablesReleaseOnArgumentOff)
{
    ScopedGlobalReleaseFlag release_flag_scope;
    SoloCharacterContext context;
    char argument[] = "off";
    do_setfree(&context.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.descriptor.output), "You disabled pointer release.\n\r");
    EXPECT_EQ(global_release_flag, 0);
    // See DoSetfreeReportsFakedStateWhenFlagIsClear's comment above (Phase 5
    // T6 leak sweep) -- restore before `context`'s RAII cleanup runs below.
    global_release_flag = 1;
}

TEST(ActWizPlayerAdmin, DoSetfreeReportsUsageForUnrecognizedArgument)
{
    ScopedGlobalReleaseFlag release_flag_scope;
    SoloCharacterContext context;
    char argument[] = "bogus";
    do_setfree(&context.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.descriptor.output), "Use [on|off] to switch pointer RELEASE.\n\r");
}

// ---------------------------------------------------------------------------
// Phase 4 Wave 3 Task 8 (Chunk W4 -- act_wiz.cpp Wiz communication family):
// do_emote, do_send, do_echo, do_gecho, do_poofset, do_wiznet. Suite
// ActWizComm below. Same binding pattern as Tasks 5-7: these pin the CURRENT
// byte-for-byte output -- confirmed passing against the pre-conversion
// source -- before this chunk's sprintf sites convert to std::format, and
// green again after.
//
// SCOPE NOTE (do_poofset, act_wiz.cpp ~:1725): `*msg = str_dup(argument + i)`
// stores a malloc'd string into char_data::specials.poofIn/poofOut --
// ownership leaves the function into a struct member the live game frees
// later (RELEASE() on a subsequent do_poofset call, or player/mob teardown).
// Per the transform idiom catalog (#10: "ownership that leaves the function
// ... is OUT of scope"), that assignment is NOT converted to RAII this task;
// only formatting is otherwise in play there, and do_poofset has no sprintf/
// strcpy/strcat of its own to convert.

namespace {

// Mirrors act_format_tests.cpp's RoomPairContext (Phase 4 Wave 2 Task 4),
// redefined locally per this wave's per-file-fixture convention (not
// included across test files). Two ordinary (non-NPC) PCs share room 0 of a
// fresh single-room test world so do_echo/do_emote's TO_ROOM delivery and
// do_send's get_char_vis() room lookup are all directly exercisable.
//
// Deviates from act_format_tests.cpp's version in one respect: PRF_HOLYLIGHT
// is set on BOTH characters here, not just the actor. That original fixture
// only needed the ACTOR to bypass CAN_SEE's light gate (its get_char_room_vis()
// callers always call CAN_SEE(ch=actor, victim, ...)). This chunk's
// do_emote/do_echo additionally substitute "$n" through PERS(actor, victim,
// ...) inside act(), which calls CAN_SEE(victim, actor) -- the OPPOSITE
// direction -- so the victim needs its own bypass too. Without it, whether
// $n resolves to the actor's real name or falls back to "someone" would
// depend on room_data's otherwise-untouched sector_type/room_flags/light
// fields, which (per RoomStatContext's fixture comment above) are
// indeterminate bytes from the underlying `new room_data[]` allocation --
// exactly the allocator-dependent nondeterminism macOS ASan surfaced in
// Task 5. Setting HOLYLIGHT on both sides makes every test here independent
// of that garbage.
struct RoomPairContext {
    ScopedTestWorld test_world;
    char_data actor { };
    char_data victim { };
    descriptor_data actor_descriptor { };
    descriptor_data victim_descriptor { };
    char_data* original_people = nullptr;
    // Releases actor/victim.profs/skills/knowledge (clear_char() heap
    // allocations) at scope exit (Phase 5 T6 leak sweep).
    ScopedClearCharFields actor_cleanup { actor };
    ScopedClearCharFields victim_cleanup { victim };

    RoomPairContext()
    {
        clear_char(&actor, MOB_VOID);
        clear_char(&victim, MOB_VOID);
        reset_capturing_descriptor(actor_descriptor, &actor);
        reset_capturing_descriptor(victim_descriptor, &victim);

        original_people = test_world.room().people;

        actor.in_room = 0;
        victim.in_room = 0;
        actor.next_in_room = &victim;
        victim.next_in_room = nullptr;
        test_world.room().people = &actor;

        actor.specials.position = POSITION_STANDING;
        victim.specials.position = POSITION_STANDING;
        actor.player.race = RACE_HUMAN;
        victim.player.race = RACE_HUMAN;
        actor.player.name = const_cast<char*>("Gandalf");
        victim.player.name = const_cast<char*>("Legolas");
        SET_BIT(PRF_FLAGS(&actor), PRF_HOLYLIGHT);
        SET_BIT(PRF_FLAGS(&victim), PRF_HOLYLIGHT);

        actor.desc = &actor_descriptor;
        victim.desc = &victim_descriptor;
    }

    ~RoomPairContext()
    {
        test_world.room().people = original_people;
        actor.next_in_room = nullptr;
        victim.next_in_room = nullptr;
        actor.in_room = NOWHERE;
        victim.in_room = NOWHERE;
    }
};

// Mirrors act_wiz_tests.cpp's ScopedDescriptorList (Phase 4 Wave 1) minus the
// account-character-selection-unlock reset -- this file's do_gecho/
// do_wiznet tests never touch that map (the same simplification
// act_info_format_tests.cpp's own copy already made). Per-file duplication,
// not a shared header, matches this suite's existing convention.
class ScopedDescriptorList {
public:
    ScopedDescriptorList()
        : m_previous_descriptor_list(descriptor_list)
    {
        descriptor_list = nullptr;
    }

    ~ScopedDescriptorList() { descriptor_list = m_previous_descriptor_list; }

private:
    // The process-global descriptor_list value in effect before this guard,
    // restored on scope exit.
    descriptor_data* m_previous_descriptor_list;
};

} // namespace

// ---------------------------------------------------------------------------
// do_emote (act_wiz.cpp:121)
// ---------------------------------------------------------------------------

TEST(ActWizComm, DoEmoteReportsMistakeMessageWhenArgumentBlank)
{
    SoloCharacterContext context;
    char argument[] = "   ";
    do_emote(&context.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.descriptor.output), "Yes.. But what?\n\r");
}

TEST(ActWizComm, DoEmoteDeliversToRoomAndAcksActorWithoutEcho)
{
    RoomPairContext context;
    char argument[] = "waves.";
    do_emote(&context.actor, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.victim_descriptor.output),
        "Gandalf waves.\n\r");
    EXPECT_EQ(std::string(context.actor_descriptor.output), "Ok.\n\r");
}

TEST(ActWizComm, DoEmoteDeliversToActorTooWhenEchoOn)
{
    RoomPairContext context;
    SET_BIT(PRF_FLAGS(&context.actor), PRF_ECHO);
    char argument[] = "waves.";
    do_emote(&context.actor, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.victim_descriptor.output),
        "Gandalf waves.\n\r");
    EXPECT_EQ(std::string(context.actor_descriptor.output), "Gandalf waves.\n\r");
}

// ---------------------------------------------------------------------------
// do_send (act_wiz.cpp:146)
// ---------------------------------------------------------------------------

TEST(ActWizComm, DoSendReportsSendWhatWhenNoArgument)
{
    SoloCharacterContext context;
    char argument[] = "";
    do_send(&context.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.descriptor.output), "Send what to who?\n\r");
}

TEST(ActWizComm, DoSendReportsNoSuchPersonWhenTargetNotFound)
{
    RoomPairContext context;
    char argument[] = "Nobody hello";
    do_send(&context.actor, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.actor_descriptor.output),
        "No such person around.\n\r");
}

TEST(ActWizComm, DoSendDeliversMessageAndSentAckWithoutEcho)
{
    RoomPairContext context;
    char argument[] = "Legolas Watch out!";
    do_send(&context.actor, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.victim_descriptor.output), "Watch out!\n\r");
    EXPECT_EQ(std::string(context.actor_descriptor.output), "Sent.\n\r");
}

TEST(ActWizComm, DoSendReportsEchoConfirmationToActorWhenEchoOn)
{
    RoomPairContext context;
    SET_BIT(PRF_FLAGS(&context.actor), PRF_ECHO);
    char argument[] = "Legolas Watch out!";
    do_send(&context.actor, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.victim_descriptor.output), "Watch out!\n\r");
    EXPECT_EQ(std::string(context.actor_descriptor.output),
        "You send 'Watch out!' to Legolas.\n\r");
}

// ---------------------------------------------------------------------------
// do_echo (act_wiz.cpp:172)
// ---------------------------------------------------------------------------

// Exemplar from the task brief: pins the observer's received line and the
// actor's PRF_ECHO=off "Ok." acknowledgment in one shot.
TEST(ActWizComm, DoEchoDeliversLineToRoomAndAcksActor)
{
    RoomPairContext context;
    char argument[] = "  The walls tremble.";
    do_echo(&context.actor, argument, nullptr, 0, 0);
    EXPECT_STREQ(context.victim_descriptor.small_outbuf,
        "The walls tremble.\n\r");
    EXPECT_STREQ(context.actor_descriptor.small_outbuf, "Ok.\n\r");
}

TEST(ActWizComm, DoEchoDeliversLineToActorTooWhenEchoOn)
{
    RoomPairContext context;
    SET_BIT(PRF_FLAGS(&context.actor), PRF_ECHO);
    char argument[] = "  The walls tremble.";
    do_echo(&context.actor, argument, nullptr, 0, 0);
    EXPECT_STREQ(context.victim_descriptor.small_outbuf,
        "The walls tremble.\n\r");
    EXPECT_STREQ(context.actor_descriptor.small_outbuf, "The walls tremble.\n\r");
}

TEST(ActWizComm, DoEchoReportsMistakeMessageWhenArgumentBlank)
{
    RoomPairContext context;
    char argument[] = "   ";
    do_echo(&context.actor, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(context.actor_descriptor.output),
        "That must be a mistake...\n\r");
}

// ---------------------------------------------------------------------------
// do_gecho (act_wiz.cpp:1676)
// ---------------------------------------------------------------------------

TEST(ActWizComm, DoGechoDeliversToConnectedDescriptorsAndAcksActorWithoutEcho)
{
    ScopedDescriptorList descriptor_list_scope;

    SoloCharacterContext actor_context;
    char_data receiver { };
    clear_char(&receiver, MOB_VOID);
    // Releases receiver.profs/skills/knowledge (clear_char() heap
    // allocations) at scope exit (Phase 5 T6 leak sweep).
    ScopedClearCharFields receiver_cleanup { receiver };
    descriptor_data receiver_descriptor { };
    reset_capturing_descriptor(receiver_descriptor, &receiver);
    receiver.desc = &receiver_descriptor;

    // actor's own descriptor is deliberately included in the list too --
    // do_gecho's loop gates on `pt->character != ch`, so it must be skipped
    // there even though the SAME actor separately receives an "Ok."
    // acknowledgment via the direct send_to_char() below the loop.
    actor_context.descriptor.next = &receiver_descriptor;
    receiver_descriptor.next = nullptr;
    descriptor_list = &actor_context.descriptor;

    char argument[] = "  The gods speak.";
    do_gecho(&actor_context.character, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(receiver_descriptor.output), "The gods speak.\n\r");
    EXPECT_EQ(std::string(actor_context.descriptor.output), "Ok.\n\r");
}

TEST(ActWizComm, DoGechoSkipsDescriptorsStillConnecting)
{
    ScopedDescriptorList descriptor_list_scope;

    SoloCharacterContext actor_context;
    char_data connecting_char { };
    clear_char(&connecting_char, MOB_VOID);
    // Releases connecting_char.profs/skills/knowledge (clear_char() heap
    // allocations) at scope exit (Phase 5 T6 leak sweep).
    ScopedClearCharFields connecting_char_cleanup { connecting_char };
    descriptor_data connecting_descriptor { };
    reset_capturing_descriptor(connecting_descriptor, &connecting_char);
    connecting_descriptor.connected = 1; // still at a menu, not CON_PLAYING
    connecting_descriptor.next = nullptr;
    descriptor_list = &connecting_descriptor;

    char argument[] = "  The gods speak.";
    do_gecho(&actor_context.character, argument, nullptr, 0, 0);
    EXPECT_STREQ(connecting_descriptor.small_outbuf, "");
    EXPECT_EQ(std::string(actor_context.descriptor.output), "Ok.\n\r");
}

// ---------------------------------------------------------------------------
// do_poofset (act_wiz.cpp:1703)
// ---------------------------------------------------------------------------

TEST(ActWizComm, DoPoofsetStoresPoofinMessageAndAcksActor)
{
    SoloCharacterContext context;
    char argument[] = "  Ta-da!";
    do_poofset(&context.character, argument, nullptr, 0, SCMD_POOFIN);
    ASSERT_NE(context.character.specials.poofIn, nullptr);
    EXPECT_STREQ(context.character.specials.poofIn, "Ta-da!");
    EXPECT_EQ(std::string(context.descriptor.output), "Ok.\n\r");
    // Freed directly (not via RELEASE()) so cleanup doesn't depend on the
    // process-global global_release_flag toggle (RELEASE() only calls
    // free_function() when that flag is set -- see do_setfree's "RELEASE is
    // faked" message); str_dup()'s allocation is a plain malloc-compatible
    // buffer (create_function()/CREATE()), so std::free() is always safe here.
    std::free(context.character.specials.poofIn);
    context.character.specials.poofIn = nullptr;
}

TEST(ActWizComm, DoPoofsetClearsPoofoutMessageWhenArgumentBlank)
{
    SoloCharacterContext context;
    context.character.specials.poofOut = str_dup("old message");
    char argument[] = "   ";
    do_poofset(&context.character, argument, nullptr, 0, SCMD_POOFOUT);
    EXPECT_EQ(context.character.specials.poofOut, nullptr);
    EXPECT_EQ(std::string(context.descriptor.output), "Ok.\n\r");
}

TEST(ActWizComm, DoPoofsetIgnoresUnrecognizedSubcommand)
{
    SoloCharacterContext context;
    char argument[] = "whatever";
    do_poofset(&context.character, argument, nullptr, 0, 99);
    EXPECT_EQ(std::string(context.descriptor.output), "");
}

// ---------------------------------------------------------------------------
// do_wiznet (act_wiz.cpp:1974)
// ---------------------------------------------------------------------------

// Pins the "<name> wiznets '<text>'" line prefix delivered (color-code-
// wrapped, colorless here since neither character has PRF_COLOR) to a
// wiz-flagged descriptor, alongside the actor's own "Ok." acknowledgment.
TEST(ActWizComm,
    DoWiznetDeliversPrefixedLineToWizFlaggedDescriptorAndAcksActor)
{
    ScopedTestWorld test_world;
    ScopedDescriptorList descriptor_list_scope;

    char_data actor { };
    clear_char(&actor, MOB_VOID);
    // Releases actor.profs/skills/knowledge (clear_char() heap
    // allocations) at scope exit (Phase 5 T6 leak sweep).
    ScopedClearCharFields actor_cleanup { actor };
    descriptor_data actor_descriptor { };
    reset_capturing_descriptor(actor_descriptor, &actor);
    actor.desc = &actor_descriptor;
    actor.in_room = 0;
    actor.player.name = const_cast<char*>("Gandalf");
    actor.player.level = LEVEL_IMMORT;
    actor.specials.position = POSITION_STANDING;
    SET_BIT(PRF_FLAGS(&actor), PRF_WIZ);
    SET_BIT(PRF_FLAGS(&actor), PRF_HOLYLIGHT);

    char_data receiver { };
    clear_char(&receiver, MOB_VOID);
    // Releases receiver.profs/skills/knowledge (clear_char() heap
    // allocations) at scope exit (Phase 5 T6 leak sweep).
    ScopedClearCharFields receiver_cleanup { receiver };
    descriptor_data receiver_descriptor { };
    reset_capturing_descriptor(receiver_descriptor, &receiver);
    receiver.desc = &receiver_descriptor;
    receiver.player.level = LEVEL_IMMORT;
    receiver.specials.position = POSITION_STANDING;
    SET_BIT(PRF_FLAGS(&receiver), PRF_WIZ);
    SET_BIT(PRF_FLAGS(&receiver), PRF_HOLYLIGHT);

    descriptor_list = &receiver_descriptor;
    receiver_descriptor.next = nullptr;

    char argument[] = " The council convenes.";
    do_wiznet(&actor, argument, nullptr, 0, 0);
    EXPECT_EQ(std::string(receiver_descriptor.output),
        "Gandalf wiznets 'The council convenes.'\n\r");
    EXPECT_EQ(std::string(actor_descriptor.output), "Ok.\n\r");
}
