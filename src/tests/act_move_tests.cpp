#include "../combat_hooks.h"
#include "../comm.h"
#include "../handler.h"
#include "../interpre.h"
#include "../output_seam.h"
#include "../utils.h"
#include "rots/core/character.h"
#include "rots/core/descriptor.h"
#include "rots/core/object.h"
#include "rots/core/room.h"
#include "test_placement.h"
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

    // Room 0 is published EMPTY -- the mover is LOCATED there (set_location in
    // the constructor body) but was never linked into its occupant chain, and
    // still is not -- while room 1 holds the single race guard the converted
    // destination walk has to find. Both declared LAST so they unwind before
    // the characters they manage and before the ScopedTestWorld whose rooms
    // they point into. The room-1 helper stamps the guard's location through
    // set_location(); together they replace the ctor loop's per-room
    // `people = nullptr` and both of the destructor's own null-outs.
    ScopedRoomOccupants room0_occupants{room_by_id_total(0), 0, {}};
    ScopedRoomOccupants room1_occupants{room_by_id_total(1), 1, {&guard}};

    CheckSimpleMoveTestContext() {
        top_of_world = 1;

        for (int room = 0; room < 2; ++room) {
            room_by_id_total(room)->room_flags = 0;
            room_by_id_total(room)->sector_type = 0;
            for (int dir = 0; dir < NUM_OF_DIRS; ++dir) {
                room_by_id_total(room)->dir_option[dir] = nullptr;
            }
        }

        exit_to_room1.exit_info = 0;
        exit_to_room1.to_room = 1;
        room_by_id_total(0)->dir_option[NORTH] = &exit_to_room1;

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
        set_location(&ch, 0);

        // The race-guard occupant of the destination room, of a different
        // race than ch by default (RACE_DWARF vs. RACE_HUMAN).
        guard.specials2.act = MOB_ISNPC | MOB_RACE_GUARD;
        guard.specials.position = POSITION_STANDING;
        guard.player.race = RACE_DWARF;
    }

    ~CheckSimpleMoveTestContext() {
        // The ctor's per-direction reset loop guarantees dir_option[NORTH] was
        // nullptr before this fixture pointed it at exit_to_room1 (a fixture
        // member about to be destroyed); null it back out here so no later
        // test sharing this process's world[] can walk into a dangling
        // pointer (LS-2 whole-branch review B1, site 1).
        room_by_id_total(0)->dir_option[NORTH] = nullptr;
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
    set_location(&ch, 0);

    const int previous_hours = time_info.hours;
    time_info.hours = 5;
    const int dir = 2;
    const int expected_data = static_cast<int>(static_cast<byte>(time_info.hours * 8 + dir));

    set_blood_trail(&ch, dir);

    time_info.hours = previous_hours;

    bool found = false;
    for (int slot = 0; slot < NUM_OF_BLOOD_TRAILS; ++slot) {
        const room_bleed_data &entry = room_by_id_total(0)->bleed_track[slot];
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

// -----------------------------------------------------------------------
// Commit 2 riders (do_pull, do_open, do_enter/do_leave). do_move's own two
// walk conversions (act_move.cpp :760/:815, M4) are deliberately NOT
// separately unit-tested here (documented exclusion, not an oversight):
// they are the IDENTICAL `rots::entity::occupants(room_of(ch))` range-for
// shape CheckSimpleMoveTest's pair above already proves correct twice over
// (positive block + positive pass-through), and a do_move-specific fixture
// would additionally need do_look()'s full room-render dependencies (do_move
// unconditionally calls do_look() after every successful relocation) on top
// of check_simple_move's own already-substantial requirements -- a
// materially higher cost than CheckSimpleMoveTestContext for proving the
// same conversion pattern a third time. do_close/do_lock/do_unlock's
// reciprocal-door conversions (M5 remainder) are also deliberately NOT
// separately tested: DoOpenAnnouncesToTheOtherSideOfAReciprocalDoor below
// proves the identical `room_by_id_total(other_room)->dir_option[...]` +
// `location_of(ch)` pair those three share verbatim, matching the census's
// own explicit guidance ("one test suffices for do_open; the other three...
// can be covered by diff review + the boot golden"). Both deferrals are
// witnessed by scripts/boot-golden.sh at every commit.
// -----------------------------------------------------------------------

ACMD(do_pull);
ACMD(do_open);
ACMD(do_enter);
ACMD(do_leave);

namespace {

// Real (non-socket) descriptor output buffer -- act_wiz_tests.cpp's
// established make_descriptor()/`.output = .small_outbuf` pattern, needed
// here because do_pull's/do_open's room-broadcast messages route through
// send_to_room() (comm.cpp's send_to_room_impl walking world[room].people
// via occupant->desc), not the simpler send_to_char() seam a capturing
// sink alone could intercept.
// Initializes IN PLACE, by reference -- deliberately NOT a value-returning
// factory. descriptor_data::output points at the SAME object's small_outbuf,
// so returning such an object by value is only correct if the compiler
// applies NRVO; NRVO is OPTIONAL (C++17 guarantees copy elision only for
// prvalue temporaries, not for a named local returned by value). GCC and
// Clang elide here, which made a value-returning factory pass on macOS and
// linux-x64; MSVC copies, leaving `output` dangling into the destroyed
// temporary -- observed on CI as shredded, NUL-interleaved captures like
// "\0^A\0\0\0ever \0\0\0...ly." instead of "The lever closes slowly.".
// act_wiz_format_tests.cpp's reset_capturing_descriptor() takes a reference
// for exactly this reason; this mirrors it.
void init_output_descriptor(descriptor_data &descriptor) {
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
}

// Minimal send_to_char() capturing sink (mirrors combat_hooks_tests.cpp's
// ScopedCapturingOutputSink, redefined locally since that class is
// TU-local to its own anonymous namespace) -- used by the do_enter/
// do_leave riders below, whose pinned messages route through
// send_to_char(), not send_to_room().
class ScopedCapturingSendToCharSink {
  public:
    ScopedCapturingSendToCharSink() {
        last_message.clear();
        rots::output::Sinks sinks{};
        sinks.send_to_char = &capture;
        rots::output::set_sinks(sinks);
    }

    ~ScopedCapturingSendToCharSink() { register_game_output_sinks(); }

    ScopedCapturingSendToCharSink(const ScopedCapturingSendToCharSink &) = delete;
    ScopedCapturingSendToCharSink &operator=(const ScopedCapturingSendToCharSink &) = delete;

    static std::string last_message;

  private:
    static void capture(std::string_view message, char_data *) {
        last_message = std::string(message);
    }
};

std::string ScopedCapturingSendToCharSink::last_message;

} // namespace

// -----------------------------------------------------------------------
// do_pull (act_move.cpp:1795) -- M1, the task brief's HIGHEST PRIORITY
// rider: both resolver traps (:1838's `room = room_by_id_total(room_num);`
// and :1874's `next_room = room_by_id_total(next_room_num);`, each
// replacing a raw `&world[...]`) were completely untested before this
// task -- a mistaken room_by_id() substitution at either site would
// silently swap the working door-toggle path for the "$P seems to be
// broken." early return under exactly the single-room-world conditions
// many suites in this tree already use (per ls2-global-constraints.md's
// room_by_id() ban rationale). Both tests below drive the SAME lever
// object through both resolver sites in one call each.
// -----------------------------------------------------------------------

TEST(DoPullTest, LeverInPullersOwnRoomTogglesDoorWithoutRumblingMessage) {
    ScopedTestWorld test_world{1};
    // Site 2 (LS-2 whole-branch review B1): captured before this test points
    // the slot/vnum at itself, so both can be restored at the tail below.
    room_direction_data *const original_dir_option_north = room_by_id_total(0)->dir_option[NORTH];
    const int original_room_number = room_by_id_total(0)->number;
    room_direction_data lever_exit{};
    lever_exit.exit_info = 0; // not closed -- this pull SETs EX_CLOSED
    lever_exit.keyword = const_cast<char *>("lever");
    lever_exit.to_room = -1; // NOWHERE -- the reciprocal-side is a no-op here
    room_by_id_total(0)->dir_option[NORTH] = &lever_exit;
    room_by_id_total(0)->number = 9001;

    obj_data lever{};
    lever.in_room = 0; // LS1-ALLOW: obj-location
    lever.obj_flags.type_flag = ITEM_LEVER;
    lever.obj_flags.value[0] = 9001; // real_room()'s target vnum
    lever.obj_flags.value[1] = NORTH;

    descriptor_data descriptor{};
    init_output_descriptor(descriptor);
    char_data ch{};
    ch.desc = &descriptor;
    // Publishes the puller as room 0's only occupant and stamps its location
    // through set_location(); the `world[0].people = nullptr` that used to sit
    // after the do_pull() call below is now this guard's own unwind, which
    // also unlinks the stack char_data (LS-3a T3, test_placement.h).
    ScopedRoomOccupants occupants{room_by_id_total(0), 0, {&ch}};

    waiting_type wtl{};
    wtl.targ1.type = TARGET_OBJ;
    wtl.targ1.ptr.obj = &lever;

    do_pull(&ch, mutable_arg(""), &wtl, 0, 0);

    // Site 2 (LS-2 whole-branch review B1): lever_exit is a function-local
    // about to go out of scope -- restore the slot it dangled from, plus
    // the vnum this test stamped, so neither leaks into a later test
    // sharing this process's world[].
    room_by_id_total(0)->dir_option[NORTH] = original_dir_option_north;
    room_by_id_total(0)->number = original_room_number;

    const std::string output(descriptor.output);
    EXPECT_NE(output.find("closes slowly"), std::string::npos)
        << "Expected the :1838 room_by_id_total(room_num) conversion to resolve the lever's "
           "own room and toggle its exit's EX_CLOSED bit, sending \"closes slowly\" there: "
        << output;
    EXPECT_EQ(output.find("rumbling"), std::string::npos)
        << "Expected NO rumbling message: the puller's room and the lever's target room are "
           "the same (:1855's `location_of(ch) != room_num` must be false): "
        << output;
}

TEST(DoPullTest, LeverInADifferentRoomAnnouncesRumblingAndTogglesBothSidesReciprocally) {
    ScopedTestWorld test_world{2};
    // Site 3 (LS-2 whole-branch review B1): captured before this test points
    // the slots/vnum at itself, so all three can be restored at the tail
    // below. Since LS-3a T1 Stage A the next ScopedTestWorld construction also
    // clears dir_option[]/.number for every room, so this restore is what
    // keeps the dangling window closed in between rather than the only guard.
    room_direction_data *const original_room1_dir_north = room_by_id_total(1)->dir_option[NORTH];
    const int original_room1_number = room_by_id_total(1)->number;
    room_direction_data *const original_room0_dir_south = room_by_id_total(0)->dir_option[SOUTH];

    // The lever lives in room 1, controlling room 1's NORTH exit back to
    // room 0 -- exercises BOTH resolver traps: :1838 resolves room 1 (the
    // lever's own room), :1874 resolves room 0 (the reciprocal side, via
    // room 1's NORTH exit's to_room).
    room_direction_data lever_exit{};
    lever_exit.exit_info = EX_CLOSED; // starts closed -- this pull OPENS it
    lever_exit.keyword = const_cast<char *>("lever");
    lever_exit.to_room = 0;
    room_by_id_total(1)->dir_option[NORTH] = &lever_exit;
    room_by_id_total(1)->number = 9002;

    // room 0's reciprocal SOUTH exit (rev_dir[NORTH] == SOUTH) back to
    // room 1 -- lets the reciprocal-side toggle run to completion instead
    // of early-returning at act_move.cpp's `!next_room->dir_option[...]`
    // guard.
    room_direction_data reciprocal_exit{};
    reciprocal_exit.exit_info = 0; // not closed -- reciprocal pull CLOSES it
    reciprocal_exit.keyword = const_cast<char *>("door");
    reciprocal_exit.to_room = 1;
    room_by_id_total(0)->dir_option[SOUTH] = &reciprocal_exit;

    obj_data lever{};
    lever.in_room = 0; // LS1-ALLOW: obj-location (co-located with the puller, act_move.cpp:1814)
    lever.obj_flags.type_flag = ITEM_LEVER;
    lever.obj_flags.value[0] = 9002;
    lever.obj_flags.value[1] = NORTH;

    descriptor_data puller_descriptor{};
    init_output_descriptor(puller_descriptor);
    char_data ch{};
    ch.desc = &puller_descriptor;

    descriptor_data bystander_descriptor{};
    init_output_descriptor(bystander_descriptor);
    char_data bystander{};
    bystander.desc = &bystander_descriptor;

    // One guard per room: the puller alone in room 0, the bystander alone in
    // room 1. Both stamp their character's location through set_location() and
    // both take it back out on unwind, replacing the two
    // `world[N].people = nullptr` lines that used to follow do_pull().
    ScopedRoomOccupants room0_occupants{room_by_id_total(0), 0, {&ch}};
    ScopedRoomOccupants room1_occupants{room_by_id_total(1), 1, {&bystander}};

    waiting_type wtl{};
    wtl.targ1.type = TARGET_OBJ;
    wtl.targ1.ptr.obj = &lever;

    do_pull(&ch, mutable_arg(""), &wtl, 0, 0);

    // Site 3 (LS-2 whole-branch review B1): lever_exit/reciprocal_exit are
    // function-locals about to go out of scope -- restore both dir_option
    // slots plus the vnum this test stamped, so neither leaks into a later
    // test sharing this process's world[].
    room_by_id_total(1)->dir_option[NORTH] = original_room1_dir_north;
    room_by_id_total(1)->number = original_room1_number;
    room_by_id_total(0)->dir_option[SOUTH] = original_room0_dir_south;

    const std::string bystander_output(bystander_descriptor.output);
    EXPECT_NE(bystander_output.find("opens slowly"), std::string::npos)
        << "Expected room 1's occupant (co-located with the lever's target room, via :1838's "
           "conversion) to see the door open: "
        << bystander_output;

    const std::string puller_output(puller_descriptor.output);
    EXPECT_NE(puller_output.find("rumbling"), std::string::npos)
        << "Expected the puller (:1856's `send_to_room(..., location_of(ch))`) to hear "
           "rumbling, since the puller's room (0) differs from the lever's room (1): "
        << puller_output;
    EXPECT_NE(puller_output.find("opens slowly"), std::string::npos)
        << "Expected the :1874 room_by_id_total(next_room_num) conversion to resolve room 0 "
           "(the reciprocal side) and toggle ITS exit too -- the reciprocal branch mirrors "
           "would_open unconditionally (`... || would_open`), so it also OPENS here, reaching "
           "the puller (also in room 0) with \"opens slowly\": "
        << puller_output;
}

// -----------------------------------------------------------------------
// do_open (act_move.cpp:978) -- M5. Proves the door-family's shared
// reciprocal-door shape (room_by_id_total(other_room)->dir_option[...] +
// location_of(ch)) that do_close/do_lock/do_unlock (M5 remainder,
// deliberately not separately tested, see the block comment above)
// repeat verbatim.
// -----------------------------------------------------------------------

TEST(DoOpenTest, AnnouncesToTheOtherSideOfAReciprocalDoor) {
    ScopedTestWorld test_world{2};
    // Site 4 (LS-2 whole-branch review B1): captured before this test points
    // both slots at itself, so both can be restored at the tail below.
    room_direction_data *const original_room0_dir_north = room_by_id_total(0)->dir_option[NORTH];
    room_direction_data *const original_room1_dir_south = room_by_id_total(1)->dir_option[SOUTH];

    room_direction_data near_exit{};
    near_exit.exit_info = EX_ISDOOR | EX_CLOSED;
    near_exit.keyword = const_cast<char *>("door");
    near_exit.to_room = 1;
    room_by_id_total(0)->dir_option[NORTH] = &near_exit;

    room_direction_data far_exit{};
    far_exit.exit_info = EX_ISDOOR | EX_CLOSED;
    far_exit.keyword = const_cast<char *>("door");
    far_exit.to_room = 0; // must equal location_of(ch) for the reciprocal branch to fire
    room_by_id_total(1)->dir_option[SOUTH] = &far_exit;

    descriptor_data bystander_descriptor{};
    init_output_descriptor(bystander_descriptor);
    char_data bystander{};
    bystander.desc = &bystander_descriptor;

    char_data ch{};
    set_location(&ch, 0); // the opener is located in room 0, never chained into it
    ch.specials2.act = 0; // not IS_SHADOW

    // The reciprocal-open message's only recipient, alone in room 1; the
    // `world[1].people = nullptr` that used to follow do_open() is this
    // guard's own unwind.
    ScopedRoomOccupants room1_occupants{room_by_id_total(1), 1, {&bystander}};

    waiting_type wtl{};
    wtl.targ1.type = TARGET_DIR;
    wtl.targ1.ch_num = NORTH;

    do_open(&ch, mutable_arg(""), &wtl, 0, 0);

    // Site 4 (LS-2 whole-branch review B1): near_exit/far_exit are
    // function-locals about to go out of scope -- restore both dir_option
    // slots so neither dangles into a later test sharing this process's
    // world[].
    room_by_id_total(0)->dir_option[NORTH] = original_room0_dir_north;
    room_by_id_total(1)->dir_option[SOUTH] = original_room1_dir_south;

    EXPECT_FALSE(IS_SET(near_exit.exit_info, EX_CLOSED))
        << "Expected do_open's own side to open (unrelated to this task's conversion, a "
           "sanity check that the fixture actually reached the door-toggle branch).";
    EXPECT_FALSE(IS_SET(far_exit.exit_info, EX_CLOSED))
        << "Expected the :1057 room_by_id_total(other_room) conversion to resolve room 1 and "
           "the :1058 location_of(ch) conversion to match it against room 0, opening the "
           "reciprocal side too.";

    const std::string bystander_output(bystander_descriptor.output);
    EXPECT_NE(bystander_output.find("is opened from the other side"), std::string::npos)
        << "Expected room 1's occupant to see the reciprocal-open message: " << bystander_output;
}

// -----------------------------------------------------------------------
// do_enter/do_leave (act_move.cpp:1361/:1393) -- M6. Both pinned at their
// cheapest deterministic branch (the static already-indoors/already-
// outside message), matching the census's "2 cheap tests" framing; the
// "find an entrance and move" branches recurse into do_move (M4's own
// deferred territory) and are not repeated here.
// -----------------------------------------------------------------------

TEST(DoEnterTest, RefusesWhenAlreadyIndoors) {
    ScopedTestWorld test_world{1};
    // Site 5 (LS-2 whole-branch review B1): captured before this test
    // overwrites room_flags, restored at the tail below -- RoomStatContext
    // has no fixture here to do it for us.
    const long original_room_flags = room_by_id_total(0)->room_flags;
    room_by_id_total(0)->room_flags = INDOORS;

    char_data ch{};
    set_location(&ch, 0);

    ScopedCapturingSendToCharSink capture;
    do_enter(&ch, mutable_arg(""), nullptr, 0, 0);

    EXPECT_EQ(ScopedCapturingSendToCharSink::last_message, "You are already indoors.\n\r")
        << "Expected the :1376 room_of(ch)->room_flags conversion to read room[0]'s INDOORS "
           "flag correctly.";

    room_by_id_total(0)->room_flags = original_room_flags;
}

TEST(DoLeaveTest, RefusesWhenAlreadyOutside) {
    ScopedTestWorld test_world{1};
    room_by_id_total(0)->room_flags = 0; // not INDOORS

    char_data ch{};
    set_location(&ch, 0);

    ScopedCapturingSendToCharSink capture;
    do_leave(&ch, mutable_arg(""), nullptr, 0, 0);

    EXPECT_EQ(ScopedCapturingSendToCharSink::last_message,
              "You are outside.. where do you want to go?\n\r")
        << "Expected the :1415 room_of(ch)->room_flags conversion to read room[0]'s (default, "
           "non-INDOORS) flags correctly.";
}
