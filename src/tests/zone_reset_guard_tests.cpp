// RR Wave R2 Task 2: reset_zone()'s GUARDED conversions (the room-resolve
// ledger's rr2-census.md finding for src/world/zone.cpp:314/:342/:448).
//
// The census flagged 5 of the `reset_zone · room_by_id_total(` row's 11
// sites as relying on load-time real_room() normalization ALONE, with no
// independent runtime guard against a malformed zone file whose room
// reference failed to resolve (real_room() returning NOWHERE). This task's
// own re-audit (see docs/superpowers/room-resolve-ledger.md's row for the
// full citation) found the true picture split differently than the
// census's 5-site list:
//
//   - zone.cpp:314/:342 ('L' sub-commands 0/1): genuinely unguarded. Their
//     local `if (ZCMD.arg2 >= 0 || ZCMD.arg3 >= 0)` gate is an OR across two
//     DIFFERENT args, so it does not exclude ZCMD.arg2 == NOWHERE when
//     ZCMD.arg3 happens to be >= 0 -- and ZCMD.arg2 is the room id used
//     UNCONDITIONALLY inside the block regardless of which half of the OR
//     fired. zone_load.cpp's own load-time full-command-disable mechanism
//     (renum_zone_one()'s `a`/`b` capture + `if (a<0||b<0) command='*'`)
//     does not cover 'L' commands' arg2 either (its real_room() call is a
//     bare, uncaptured assignment) -- so nothing closes this gap before
//     reset_zone() runs. Without a guard, room_by_id_total(NOWHERE) would
//     silently degrade to world[0] (room_data::operator[]'s negative-room
//     mudlog + BASE_WORLD fallback) and search THAT room instead of
//     skipping the command -- exactly the class of silent-wrong-room defect
//     this wave's GUARDED conversions close.
//   - zone.cpp:448 ('L' sub-command 6): a DIFFERENT root cause the census
//     did not identify -- ZCMD.arg1 here is this switch's own selector
//     value, so it is provably == 6 whenever this branch runs (not a
//     zone-file-driven room id at all; zone_load.cpp never routes 'L'
//     arg1 through real_room()). The residual risk is instead that
//     room_by_id_total(6) silently reads one of create_bulk()'s trailing
//     EXTENSION_SIZE(=50) dummy rooms whenever the world has fewer than 7
//     rooms (top_of_world < 6) -- never true for any real production world,
//     but not formally guaranteed, so this task adds the same defensive
//     upper-bound guard the 'D' command already carries (zone.cpp:697).
//   - zone.cpp:580/:602 ('O'/'P'): this task's re-audit found these were
//     ALREADY correctly guarded (`if (ZCMD.arg2 >= 0)` at zone.cpp:583,
//     and the `if (ZCMD.arg1 == NOWHERE || ...) ... else` at zone.cpp:603
//     whose else-branch requires arg1 != NOWHERE by De Morgan) -- the
//     census over-flagged these two; they land PROVEN entry-guard in the
//     ledger with no code change, correcting that premise (the same kind
//     of correction Task 1 made for get_char_room's caller).
//
// Each test below is RED-FIRST: written and run against the pre-guard code
// (confirmed failing -- see task-2-report.md for the quoted failure), then
// left in place once the corresponding guard lands.

#include "../db.h"
#include "../handler.h"
#include "../player_limits.h"
#include "../utils.h"
#include "../zone.h"
#include "rots/core/character.h"
#include "rots/core/object.h"
#include "rots/core/room.h"
#include "rots/core/types.h"
#include "test_char_cleanup.h"
#include "test_placement.h"
#include "test_world.h"

#include <gtest/gtest.h>

extern struct char_data *character_list;
extern struct zone_data *zone_table;
extern int top_of_zone_table;
extern int top_of_world;

namespace {

// Installs a single-zone zone_table (mirroring occupant_order_tests.cpp's
// ZoneResetLastMobOrdinal/ZoneResetFollowOrdinal fixture) and restores
// whatever zone_table pointed at before on destruction.
class ScopedSingleZoneTable {
  public:
    ScopedSingleZoneTable()
        : m_previous_zone_table(zone_table), m_previous_top_of_zone_table(top_of_zone_table) {
        m_owned_zone.number = 0;
        zone_table = &m_owned_zone;
        top_of_zone_table = 0;
    }

    ~ScopedSingleZoneTable() {
        zone_table = m_previous_zone_table;
        top_of_zone_table = m_previous_top_of_zone_table;
    }

    ScopedSingleZoneTable(const ScopedSingleZoneTable &) = delete;
    ScopedSingleZoneTable &operator=(const ScopedSingleZoneTable &) = delete;

    zone_data &zone() { return m_owned_zone; }

  private:
    zone_data m_owned_zone{};
    zone_data *m_previous_zone_table;
    int m_previous_top_of_zone_table;
};

} // namespace

// ---------------------------------------------------------------------------
// zone.cpp:314 -- 'L' sub-command 0 ("Sets last_mob") GUARDED conversion
// ---------------------------------------------------------------------------
//
// candidate is a real, matching occupant of room 0 (IS_NPC, nr == the
// filter). Without the guard, ZCMD.arg2 == NOWHERE falls back to
// room_by_id_total(NOWHERE) -> world[0] (this fixture's own room 0) and
// WOULD find candidate a match; with the guard, the 'L' command is skipped
// entirely, mob/last_mob stay unset, and the follow-up 'A' "Set gold"
// command's `!mob` check (zone.cpp:468) no-ops instead of touching
// candidate's gold.
TEST(ResetZoneTest, SkipsTheLCommandSettingLastMobWhenItsRoomArgFailedToResolveAtLoadTime) {
    ScopedTestWorld test_world;
    ScopedSingleZoneTable zone_fixture;

    reset_com cmds[2]{};
    cmds[0].command = 'L';
    cmds[0].arg1 = 0;       // sets last_mob
    cmds[0].arg2 = NOWHERE; // simulates a room reference that failed to
                            // resolve at load time (real_room() returned -1)
    cmds[0].arg3 = 77;      // nr filter -- candidate matches this
    cmds[0].arg4 = 1;
    cmds[1].command = 'A';
    cmds[1].arg1 = 1;   // "Set gold"
    cmds[1].arg2 = 555; // marker value
    zone_fixture.zone().cmd = cmds;
    zone_fixture.zone().cmdno = 2;

    char_data candidate{};
    ScopedClearCharFields candidate_cleanup{candidate};
    clear_char(&candidate, MOB_ISNPC);
    candidate.specials2.act = MOB_ISNPC;
    candidate.nr = 77;

    ScopedRoomOccupants occupants{room_by_id_total(0), 0, {&candidate}};

    reset_zone(0);

    EXPECT_EQ(GET_GOLD(&candidate), 0)
        << "Expected the NOWHERE-room-arg guard to skip the 'L' command "
           "entirely rather than falling back to searching room 0 (the "
           "room_data::operator[] negative-room-number fallback).";
}

// ---------------------------------------------------------------------------
// zone.cpp:342 -- 'L' sub-command 1 ("Sets last_obj from the room") GUARDED
// conversion
// ---------------------------------------------------------------------------
//
// Same shape as case 0 above, but for the object-search sub-command: an
// object with a matching item_number sits in room 0's contents. Without the
// guard, ZCMD.arg2 == NOWHERE falls back to room_by_id_total(NOWHERE) ->
// world[0] and WOULD find candidate_item a match, letting the follow-up 'A'
// "Set obj value" command mutate it; with the guard, the search is skipped
// and obj/last_obj stay unset, so the 'A' command's `last_obj && obj` check
// (zone.cpp:468) no-ops instead.
TEST(ResetZoneTest, SkipsTheLCommandSettingLastObjWhenItsRoomArgFailedToResolveAtLoadTime) {
    ScopedTestWorld test_world;
    ScopedSingleZoneTable zone_fixture;

    obj_data candidate_item{};
    candidate_item.item_number = 42;
    candidate_item.next_content = nullptr;
    candidate_item.in_room = 0; // LS1-ALLOW: obj-location
    room_by_id_total(0)->contents = &candidate_item;

    reset_com cmds[2]{};
    cmds[0].command = 'L';
    cmds[0].arg1 = 1;       // sets last_obj from the room
    cmds[0].arg2 = NOWHERE; // failed-to-resolve room reference
    cmds[0].arg3 = 42;      // item_number filter -- candidate_item matches this
    cmds[0].arg4 = 1;
    cmds[1].command = 'A';
    cmds[1].arg1 = 5;   // "Set obj_flags.value[arg2]"
    cmds[1].arg2 = 2;   // value[] index
    cmds[1].arg3 = 999; // marker value
    zone_fixture.zone().cmd = cmds;
    zone_fixture.zone().cmdno = 2;

    reset_zone(0);

    EXPECT_EQ(candidate_item.obj_flags.value[2], 0)
        << "Expected the NOWHERE-room-arg guard to skip the 'L' command "
           "entirely rather than falling back to searching room 0's "
           "contents (the room_data::operator[] negative-room-number "
           "fallback).";

    room_by_id_total(0)->contents = nullptr;
}

// ---------------------------------------------------------------------------
// zone.cpp:448 -- 'L' sub-command 6 ("Sets last_mob from the zone") GUARDED
// conversion
// ---------------------------------------------------------------------------
//
// A distinct guard shape from the two above: ZCMD.arg1 is provably == 6
// here (this switch's own case selector, never real_room()-normalized), so
// the risk isn't a malformed zone file -- it's room_by_id_total(6) reading
// one of create_bulk()'s trailing dummy extension rooms whenever the world
// has fewer than 7 rooms. This 2-room fixture (top_of_world == 1) stamps
// room 6 (safely inside create_bulk()'s EXTENSION_SIZE=50 block, though
// logically outside the "real" world) with the SAME zone number room 1
// carries; without the guard, the buggy read of room_by_id_total(6)->zone
// would match candidate (parked in room 1) and set its gold, exactly like
// a correct zone-scoped read would if room_by_id_total(ZCMD.arg1) had
// legitimately meant "the room 1 is in". With the guard
// (`ZCMD.arg1 > top_of_world`, mirroring the 'D' command's own idiom at
// zone.cpp:697), the whole command is skipped before that read happens.
TEST(ResetZoneTest, SkipsTheLCommandSettingLastMobFromZoneWhenTheWorldHasFewerThanSevenRooms) {
    ScopedTestWorld test_world{2}; // top_of_world == 1 -- fewer than 7 rooms
    ScopedSingleZoneTable zone_fixture;

    room_by_id_total(0)->zone = 0;
    room_by_id_total(1)->zone = 42;
    // Safe to touch: create_bulk(2) allocates 2 + EXTENSION_SIZE(50) rooms,
    // so index 6 is well inside the allocated (if logically "extra") block.
    room_by_id_total(6)->zone = 42;

    reset_com cmds[2]{};
    cmds[0].command = 'L';
    cmds[0].arg1 = 6;  // sets last_mob from the zone
    cmds[0].arg2 = 0;  // >= 0, so the outer `arg2 >= 0 || arg3 >= 0` gate
                       // passes regardless of the world-size guard below
    cmds[0].arg3 = 77; // nr filter -- candidate matches this
    cmds[0].arg4 = 1;
    cmds[1].command = 'A';
    cmds[1].arg1 = 1;   // "Set gold"
    cmds[1].arg2 = 555; // marker value
    zone_fixture.zone().cmd = cmds;
    zone_fixture.zone().cmdno = 2;

    char_data candidate{};
    ScopedClearCharFields candidate_cleanup{candidate};
    clear_char(&candidate, MOB_ISNPC);
    candidate.specials2.act = MOB_ISNPC;
    candidate.nr = 77;
    set_location(&candidate, 1);
    candidate.next = character_list;
    char_data *previous_character_list = character_list;
    character_list = &candidate;

    reset_zone(0);

    character_list = previous_character_list;

    EXPECT_EQ(GET_GOLD(&candidate), 0)
        << "Expected the world-too-small guard to skip case 6's zone "
           "lookup (room_by_id_total(6) would otherwise silently read one "
           "of create_bulk()'s dummy extension rooms) rather than matching "
           "against whatever zone that dummy room happens to carry.";
}
