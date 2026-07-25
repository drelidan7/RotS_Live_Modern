#include "../combat_hooks.h"
#include "../handler.h"
#include "../interpre.h"
#include "../utils.h"
#include "rots/core/character.h"
#include "rots/core/object.h"
#include "rots/core/room.h"
#include "test_world.h"

#include <gtest/gtest.h>

// Coverage riders for the LS-2 Wave Task T3c conversion of act_move.cpp's
// location reads through the Stage-1 Placement API
// (.superpowers/sdd/ls2-task-3c-report.md). Before this task act_move.cpp
// had 68 of its 69 live location-read lines completely untested -- the only
// tracked site any existing test reached was act_move.cpp:656 (do_move's
// no-exit early return), via combat_hooks_tests.cpp's discriminator pair.
// This file adds direct, function-level coverage for the two commit-1
// riders the task brief names as highest priority within this commit's
// scope (M2's race-guard walk) plus a cheap M7-first-half addition
// (set_blood_trail); commit 2 adds a second block of tests below (do_pull,
// do_move, the door family, do_enter/do_leave) once that commit's edits
// land.

namespace {

// -----------------------------------------------------------------------
// check_simple_move (act_move.cpp:137) -- M2's highest-priority rider: the
// race-guard walk at act_move.cpp:273 (LS-2 wave: converted from a raw
// `for (tmpch = world[world[ch->in_room].dir_option[cmd]->to_room].people;
// tmpch; tmpch = tmpch->next_in_room)` loop to
// `for (auto* tmpch : rots::entity::occupants(room_by_id_total(
// room_of(ch)->dir_option[cmd]->to_room)))`). Reached through
// rots::combat::check_simple_move(), the registered hook wrapping this
// file's real body -- gtest_main.cpp calls register_check_simple_move_hook()
// once for the whole test process (see combat_hooks_tests.cpp's own
// ScopedCheckSimpleMoveHook precedent), so no per-test registration is
// needed here.
//
// Both tests also exercise the function's other two commit-1 resolver-trap
// conversions unconditionally on every call, before the walk itself: :168
// (`room_from = room_of(ch);`, replacing `&world[ch->in_room]`) and :177
// (`room_to = room_by_id_total(...)`, replacing `&world[...]`, whose :178
// `if (!room_to) return 1;` guard is dead today and must stay dead --
// see the task report's resolver-trap section). The mounted-only INDOORS/
// NORIDE branch (:263/:265) is deliberately NOT separately pinned here:
// IS_RIDING(ch) is false for a non-mounted mover (this fixture's `ch` never
// sets mount_data.mount), so that branch is skipped entirely by both tests
// below, exactly as it is by every real non-mounted mover -- it is the
// identical `room_by_id_total(room_of(ch)->dir_option[cmd]->to_room)`
// resolver shape the race-guard walk already proves correct twice over, not
// a fresh idiom, so a third dedicated mounted fixture was judged lower
// marginal value than the effort of standing up a working IS_RIDING(ch)
// mount graph (mount_data.mount/mount_number/char_exists()) for a
// zero-behavior-change wave -- deferred with this reasoning, matching the
// task brief's guidance that a reviewer must be able to verify each
// deferral against source.
struct CheckSimpleMoveTestContext {
    // Two rooms: room 0 is the mover's starting room, room 1 is the
    // destination the walk scans -- world[world[ch->in_room].dir_option[cmd]
    // ->to_room] in the pre-conversion source.
    ScopedTestWorld test_world{2};
    room_direction_data exit_to_room1{};
    char_data ch{};
    char_data guard{};

    CheckSimpleMoveTestContext() {
        top_of_world = 1;

        for (int room = 0; room < 2; ++room) {
            world[room].room_flags = 0;
            world[room].sector_type = 0;
            world[room].people = nullptr;
            for (int dir = 0; dir < NUM_OF_DIRS; ++dir) {
                world[room].dir_option[dir] = nullptr;
            }
        }

        exit_to_room1.exit_info = 0;
        exit_to_room1.to_room = 1;
        world[0].dir_option[NORTH] = &exit_to_room1;

        // A plain, non-mounted, non-shadow PC mover -- the race guard's
        // `!IS_NPC(ch)` half of act_move.cpp:276's condition requires a
        // non-NPC mover to ever block. Ability scores mirror
        // protocol_tests.cpp's initialize_msdp_player() baseline (nonzero
        // str/dex/con on both the current and base pair -- room_move_cost()'s
        // and has_critical_stat_damage()'s division-by-base-ability-score
        // arithmetic, both reached unconditionally before the walk, would
        // divide by zero on a default-zeroed char_data otherwise).
        ch.specials.position = POSITION_STANDING;
        ch.specials2.act = 0; // not NPC
        ch.player.race = RACE_HUMAN;
        ch.abilities.str = 16;
        ch.tmpabilities.str = 14;
        ch.abilities.dex = 17;
        ch.tmpabilities.dex = 15;
        ch.abilities.con = 18;
        ch.tmpabilities.con = 16;
        ch.abilities.hit = 100;
        ch.tmpabilities.hit = 100;
        // Large enough that GET_MOVE(ch) < need_movement (act_move.cpp's
        // "too exhausted" early return, case 3) never fires and the walk is
        // actually reached.
        ch.tmpabilities.move = 10000;
        ch.in_room = 0;

        // The race-guard occupant of the destination room, of a different
        // race than ch by default (RACE_DWARF vs. RACE_HUMAN).
        guard.specials2.act = MOB_ISNPC | MOB_RACE_GUARD;
        guard.specials.position = POSITION_STANDING;
        guard.player.race = RACE_DWARF;
        guard.in_room = 1;
        guard.next_in_room = nullptr;
        world[1].people = &guard;
    }

    ~CheckSimpleMoveTestContext() {
        world[0].people = nullptr;
        world[1].people = nullptr;
    }
};

} // namespace

TEST(CheckSimpleMoveTest, RaceGuardWalkBlocksDifferentRacedNonNpcMover) {
    CheckSimpleMoveTestContext context;
    int move_cost = 0;

    const int result = rots::combat::check_simple_move(&context.ch, NORTH, &move_cost, SCMD_MOVING);

    EXPECT_EQ(result, 8)
        << "Expected the converted race-guard walk (act_move.cpp:273) to find the "
           "MOB_RACE_GUARD occupant of the destination room and block a differently-raced "
           "non-NPC mover, exactly as the pre-conversion raw next_in_room walk did.";
}

TEST(CheckSimpleMoveTest, RaceGuardWalkAllowsMovementWhenRaceMatches) {
    CheckSimpleMoveTestContext context;
    // Same race as the mover -- act_move.cpp:276's
    // `GET_RACE(ch) != GET_RACE(tmpch)` half of the guard condition is now
    // false, so the walk must visit the guard node (proving the converted
    // rots::entity::occupants(room_by_id_total(...)) range still reaches it)
    // without blocking.
    context.guard.player.race = RACE_HUMAN;
    int move_cost = 0;

    const int result = rots::combat::check_simple_move(&context.ch, NORTH, &move_cost, SCMD_MOVING);

    EXPECT_EQ(result, 0)
        << "Expected the converted race-guard walk to visit the same-race occupant, find no "
           "race mismatch, and fall through to a successful move -- proving the walk still "
           "enumerates the destination room's occupants (not just early-exits empty).";
}

// -----------------------------------------------------------------------
// set_blood_trail (act_move.cpp:297) -- M7 first half. R6's Option 1
// ruling (ls2-global-constraints.md): the room-field WRITE stays raw, but
// its `world[ch->in_room]` navigation converts to `room_of(ch)`. Untested
// before this task. `tmp` (the written bleed_track slot) is chosen by
// number(0, NUM_OF_BLOOD_TRAILS - 1) -- rather than seed the RNG, this test
// identifies the written slot by its distinctive `data` value
// (time_info.hours * 8 + dir, using values no default-constructed
// room_bleed_data slot could already hold) and scans all NUM_OF_BLOOD_TRAILS
// slots for it, so it is independent of which slot the RNG picks.
// -----------------------------------------------------------------------

extern struct time_info_data time_info;

// set_blood_trail() is a plain, non-header-declared file-local helper in
// act_move.cpp -- forward-declared here directly, matching this test
// suite's established convention for such helpers (e.g.
// act_info_format_tests.cpp's show_mount_to_char()/list_char_to_char()
// forward declarations).
void set_blood_trail(struct char_data *ch, int dir);

TEST(SetBloodTrailTest, WritesConvertedRoomsBleedTrackSlotForNpcCaller) {
    ScopedTestWorld test_world{1};
    char_data ch{};
    ch.specials2.act = MOB_ISNPC;
    ch.player.race = RACE_HUMAN;
    ch.nr = 4242;
    ch.in_room = 0;

    const int previous_hours = time_info.hours;
    time_info.hours = 5;
    const int dir = 2;
    const int expected_data = static_cast<int>(static_cast<byte>(time_info.hours * 8 + dir));

    set_blood_trail(&ch, dir);

    time_info.hours = previous_hours;

    bool found = false;
    for (int slot = 0; slot < NUM_OF_BLOOD_TRAILS; ++slot) {
        const room_bleed_data &entry = world[0].bleed_track[slot];
        if (entry.data == expected_data) {
            found = true;
            EXPECT_EQ(entry.char_number, ch.nr)
                << "Expected the converted room_of(ch)->bleed_track[tmp] write to record the "
                   "NPC caller's mob number (act_move.cpp:303), matching the "
                   "pre-conversion world[ch->in_room].bleed_track[tmp] write byte-for-byte.";
            EXPECT_EQ(entry.condition, 0);
        }
    }

    EXPECT_TRUE(found)
        << "Expected set_blood_trail() to write into the SAME room object room_of(ch) "
           "resolves to (world[0], via ScopedTestWorld) -- no bleed_track slot carried the "
           "expected data value, meaning the converted navigation landed on the wrong room "
           "or never executed.";
}

// perform_move_mount (act_move.cpp:321) -- M7 second half, deliberately NOT
// unit-tested here (documented exclusion, not an oversight): its mount
// graph (rider chains via mount_data.rider/next_rider, stop_riding_all(),
// raw_kill(), char_to_room()'s full relocation side effects, do_look()'s
// own room-render path) is genuinely fixture-hostile for a focused
// location-read regression test, exactly as the census
// (ls2-census-a1.md's M7 entry) recommended -- the function's two walks
// (:383, :438) are both display-only (feeding show_mount_to_char(), which
// itself has no location-token content) and its four F-RULING room_track
// writes (:408/:410/:412/:413) are the identical room_of(ch)->room_track[...]
// shape SetBloodTrailTest above already proves correct for the sibling
// bleed_track array. scripts/boot-golden.sh's real mount/ride flow through
// this function at every commit is this task's witness instead, matching
// act_wiz_format_tests.cpp's/act_info_format_tests.cpp's own established
// "Deliberately NOT unit-tested here" convention for genuinely
// fixture-hostile functions.
