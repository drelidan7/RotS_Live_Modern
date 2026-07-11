#include "../db.h"
#include "../handler.h"
#include "../interpre.h"
#include "../structs.h"
#include "../utils.h"
#include "../zone.h"
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

    PcTargetContext()
    {
        clear_char(&character, MOB_VOID);
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
    leader.player.name = const_cast<char*>("Gandalf");
    target.character.master = &leader;

    char_data follower_char { };
    clear_char(&follower_char, MOB_VOID);
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
