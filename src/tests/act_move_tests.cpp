#include "../combat_hooks.h"
#include "../comm.h"
#include "../handler.h"
#include "../interpre.h"
#include "../output_seam.h"
#include "../protos.h"
#include "../script.h"
#include "../utils.h"
#include "rots/core/character.h"
#include "rots/core/descriptor.h"
#include "rots/core/object.h"
#include "rots/core/room.h"
#include "test_placement.h"
#include "test_random_utils.h"
#include "test_world.h"

#include <gtest/gtest.h>
#include <memory>

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
        ch.abs_number = 9449;
        set_char_exists(ch.abs_number, &ch);
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
        remove_char_exists(ch.abs_number);
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

ACMD(do_move);
ACMD(do_flee);
void on_windblast_hit(char_data* character);
extern script_head* script_table;
extern int top_of_script_table;
extern char_data* waiting_list;
extern char_data* combat_list;

namespace {

// Counts command callbacks and chooses which one relocates the mover.
int movement_special_count = 0;
int movement_relocate_on_call = 0;

SPECIAL(relocate_during_move)
{
    if (callflag == SPECIAL_COMMAND && ++movement_special_count == movement_relocate_on_call) {
        char_from_room(ch);
        char_to_room(ch, 2);
    }
    return 0;
}

// Owns real room scripts, a registered mover and all globals used by movement.
struct MovementTriggerContext {
    // Rooms 0/1 form the attempted move; room 2 receives callback relocation.
    ScopedTestWorld test_world { 3 };
    // Shared exit data gives every random flee direction a usable destination.
    room_direction_data outbound { };
    room_direction_data return_exit { };
    // Live entities are registered so callback lifetime checks are meaningful.
    char_data actor { };
    char_data script_host { };
    // Captures the real before-enter script's marker output.
    descriptor_data descriptor { };
    // The marker and its script body are borrowed by the global script table.
    script_data trigger { };
    script_data marker { };
    script_data terminal { };
    script_head entry { };
    // Restored process-wide tables and action lists.
    script_head* previous_scripts = script_table;
    int previous_script_top = top_of_script_table;
    char_data* previous_waiting = waiting_list;
    char_data* previous_combat = combat_list;
    // Initial occupant chains unwind before their character storage.
    ScopedRoomOccupants origin { room_by_id_total(0), 0, { &actor } };
    ScopedRoomOccupants destination { room_by_id_total(1), 1, { &script_host } };

    MovementTriggerContext()
    {
        waiting_list = nullptr;
        combat_list = nullptr;
        movement_special_count = 0;
        movement_relocate_on_call = 0;
        clear_test_random_values();
        outbound.to_room = 1;
        return_exit.to_room = 0;
        for (int direction = 0; direction < NUM_OF_DIRS; ++direction) {
            room_by_id_total(0)->dir_option[direction] = &outbound;
            room_by_id_total(1)->dir_option[direction] = &return_exit;
            room_by_id_total(2)->dir_option[direction] = &outbound;
        }
        for (char_data* character : { &actor, &script_host }) {
            character->specials2.act = MOB_ISNPC;
            character->nr = -1; // Synthetic NPCs have no prototype-table entry.
            character->specials.position = POSITION_STANDING;
            character->player.race = RACE_HUMAN;
            character->player.name = const_cast<char*>("Mover");
            character->player.short_descr = const_cast<char*>("the mover");
            character->abilities.str = character->tmpabilities.str = 20;
            character->abilities.dex = character->tmpabilities.dex = 20;
            character->abilities.con = character->tmpabilities.con = 20;
            character->abilities.hit = character->tmpabilities.hit = 100;
            character->tmpabilities.move = 10000;
        }
        actor.abs_number = 9450;
        script_host.abs_number = 9451;
        set_char_exists(actor.abs_number, &actor);
        set_char_exists(script_host.abs_number, &script_host);
        descriptor.character = &actor;
        descriptor.connected = CON_PLYNG;
        descriptor.descriptor = 1;
        descriptor.output = descriptor.small_outbuf;
        descriptor.bufspace = SMALL_BUFSIZE - 1;
        actor.desc = &descriptor;
        trigger.command_type = ON_BEFORE_ENTER;
        trigger.next = &marker;
        marker.command_type = SCRIPT_SEND_TO_CHAR;
        marker.param[0] = SCRIPT_PARAM_CH2;
        marker.param[1] = SCRIPT_PARAM_CH2_NAME;
        marker.text = const_cast<char*>("before-enter-marker");
        marker.next = &terminal;
        terminal.command_type = SCRIPT_END;
        entry.number = 94500;
        entry.script = &trigger;
        script_host.specials.script_number = entry.number;
        script_table = &entry;
        top_of_script_table = 0;
    }

    ~MovementTriggerContext()
    {
        if (location_of(&actor) != NOWHERE) {
            char_from_room(&actor);
        }
        for (int room = 0; room < 3; ++room) {
            for (int direction = 0; direction < NUM_OF_DIRS; ++direction) {
                room_by_id_total(room)->dir_option[direction] = nullptr;
            }
        }
        script_table = previous_scripts;
        top_of_script_table = previous_script_top;
        RELEASE(script_host.specials.script_info);
        remove_char_exists(actor.abs_number);
        remove_char_exists(script_host.abs_number);
        waiting_list = previous_waiting;
        combat_list = previous_combat;
        clear_test_random_values();
    }

    void queue_draws()
    {
        for (int draw = 0; draw < 64; ++draw) {
            push_test_random_value(0.0);
        }
    }

    int marker_count() const
    {
        const std::string_view output(descriptor.output);
        int count = 0;
        std::size_t position = 0;
        while ((position = output.find("before-enter-marker", position)) != std::string_view::npos) {
            ++count;
            ++position;
        }
        return count;
    }
};

} // namespace

TEST(MovementTriggers, FleeAndWindblastRunTheAcceptedBeforeEnterOnlyOnce)
{
    for (const bool windblast : { false, true }) {
        MovementTriggerContext context;
        context.queue_draws();
        if (windblast) {
            on_windblast_hit(&context.actor);
        } else {
            do_flee(&context.actor, mutable_arg(""), nullptr, 0, 0);
        }
        EXPECT_EQ(location_of(&context.actor), 1);
        EXPECT_EQ(context.marker_count(), 1);

        do_move(&context.actor, mutable_arg("south"), nullptr, SOUTH + 1, SCMD_MOVING);
        do_move(&context.actor, mutable_arg("north"), nullptr, NORTH + 1, SCMD_MOVING);
        EXPECT_EQ(location_of(&context.actor), 1);
        EXPECT_EQ(context.marker_count(), 2) << "later ordinary movement must receive its own trigger";
    }
}

TEST(MovementTriggers, VetoPreventsFleeWindblastAndOrdinaryMovement)
{
    for (int operation = 0; operation < 3; ++operation) {
        MovementTriggerContext context;
        context.terminal.command_type = SCRIPT_RETURN_FALSE;
        context.queue_draws();
        if (operation == 0) {
            do_move(&context.actor, mutable_arg("north"), nullptr, NORTH + 1, SCMD_MOVING);
        } else if (operation == 1) {
            do_flee(&context.actor, mutable_arg(""), nullptr, 0, 0);
        } else {
            on_windblast_hit(&context.actor);
        }
        EXPECT_EQ(location_of(&context.actor), 0);
        EXPECT_GE(context.marker_count(), 1);
    }
}

TEST(MovementTriggers, ChangedHazeDirectionGetsAFreshTriggerAndSameDirectionReusesIt)
{
    for (const bool changed_direction : { false, true }) {
        MovementTriggerContext context;
        context.actor.specials.affected_by |= AFF_HAZE;
        push_test_random_value(0.0); // windblast's direction probe
        push_test_random_value(0.0); // origin movement cost
        push_test_random_value(0.0); // destination movement cost
        push_test_random_value(0.0); // haze fires
        double haze_direction = 0.0;
        int expected_markers = 1;
        if (changed_direction) {
            haze_direction = 0.3;
            expected_markers = 2;
        }
        push_test_random_value(haze_direction);
        context.queue_draws();
        on_windblast_hit(&context.actor);
        EXPECT_EQ(location_of(&context.actor), 1);
        EXPECT_EQ(context.marker_count(), expected_markers);
    }
}

TEST(MovementTriggers, CallbackRelocationCancelsTheOuterFleeTransition)
{
    for (int callback = 1; callback <= 3; ++callback) {
        MovementTriggerContext context;
        movement_relocate_on_call = callback;
        room_by_id_total(0)->funct = relocate_during_move;
        context.queue_draws();
        do_flee(&context.actor, mutable_arg(""), nullptr, 0, 0);
        EXPECT_EQ(location_of(&context.actor), 2);
        int expected_markers = 1;
        if (callback == 1) {
            expected_markers = 0;
        }
        EXPECT_EQ(context.marker_count(), expected_markers);
    }
}

namespace {

void initialize_additional_mover(char_data& character, descriptor_data& descriptor, int identity)
{
    character.nr = -1;
    character.abs_number = identity;
    character.specials2.act = MOB_ISNPC;
    character.specials.position = POSITION_STANDING;
    character.player.race = RACE_HUMAN;
    character.player.name = const_cast<char*>("Rider");
    character.player.short_descr = const_cast<char*>("the rider");
    character.abilities.str = character.tmpabilities.str = 20;
    character.abilities.dex = character.tmpabilities.dex = 20;
    character.abilities.con = character.tmpabilities.con = 20;
    character.abilities.hit = character.tmpabilities.hit = 100;
    character.tmpabilities.move = 10000;
    descriptor.character = &character;
    descriptor.connected = CON_PLYNG;
    descriptor.descriptor = 1;
    descriptor.output = descriptor.small_outbuf;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    character.desc = &descriptor;
    set_char_exists(identity, &character);
}

// The one rider whose independent special check should refuse movement.
char_data* vetoed_rider = nullptr;

SPECIAL(veto_secondary_rider)
{
    return callflag == SPECIAL_COMMAND && ch == vetoed_rider;
}

struct MountedMovementContext {
    // Real script/movement fixture with the primary rider as its actor.
    MovementTriggerContext scene;
    // The primary rider's mount and a separately validated passenger.
    char_data mount { };
    char_data passenger { };
    // Capture each participant's own before-enter script output.
    descriptor_data mount_descriptor { };
    descriptor_data passenger_descriptor { };
    // Publish all three in the original room and restore on scope exit.
    ScopedRoomOccupants origin { room_by_id_total(0), 0, { &scene.actor, &mount, &passenger } };

    MountedMovementContext()
    {
        initialize_additional_mover(mount, mount_descriptor, 9452);
        initialize_additional_mover(passenger, passenger_descriptor, 9453);
        mount.specials2.act |= MOB_MOUNT;
        mount.mount_data.rider = &scene.actor;
        mount.mount_data.rider_number = scene.actor.abs_number;
        scene.actor.mount_data.mount = &mount;
        scene.actor.mount_data.mount_number = mount.abs_number;
        scene.actor.mount_data.next_rider = &passenger;
        scene.actor.mount_data.next_rider_number = passenger.abs_number;
        passenger.mount_data.mount = &mount;
        passenger.mount_data.mount_number = mount.abs_number;
    }

    ~MountedMovementContext()
    {
        stop_riding_all(&mount);
        remove_char_exists(mount.abs_number);
        remove_char_exists(passenger.abs_number);
        vetoed_rider = nullptr;
    }
};

} // namespace

TEST(MovementTriggers, FleeingRiderDoesNotLendItsAcceptedTriggerToMountOrPassenger)
{
    MountedMovementContext context;
    context.scene.queue_draws();
    do_flee(&context.scene.actor, mutable_arg(""), nullptr, 0, 0);
    EXPECT_EQ(location_of(&context.scene.actor), 1);
    EXPECT_EQ(location_of(&context.mount), 1);
    EXPECT_EQ(location_of(&context.passenger), 1);
    EXPECT_EQ(context.scene.marker_count(), 1);
    EXPECT_NE(std::string_view(context.mount_descriptor.output).find("before-enter-marker"), std::string_view::npos);
    EXPECT_NE(std::string_view(context.passenger_descriptor.output).find("before-enter-marker"), std::string_view::npos);
}

TEST(MovementTriggers, PassengerSpecialCanRefuseIndependentlyOfTheFleeingRider)
{
    MountedMovementContext context;
    vetoed_rider = &context.passenger;
    room_by_id_total(0)->funct = veto_secondary_rider;
    context.scene.queue_draws();
    do_flee(&context.scene.actor, mutable_arg(""), nullptr, 0, 0);
    EXPECT_EQ(location_of(&context.scene.actor), 1);
    EXPECT_EQ(location_of(&context.mount), 1);
    EXPECT_EQ(location_of(&context.passenger), 0);
    EXPECT_EQ(context.passenger.mount_data.mount, nullptr);
    EXPECT_EQ(context.scene.marker_count(), 1);
}

TEST(MovementTriggers, ARefusedCheckedMoveCannotSuppressALaterOrdinaryMove)
{
    for (int refusal = 0; refusal < 5; ++refusal) {
        SCOPED_TRACE(refusal);
        MovementTriggerContext context;
        context.queue_draws();
        int move_cost = 0;
        ASSERT_EQ(rots::combat::check_simple_move(&context.actor, NORTH, &move_cost, SCMD_FLEE), 0);
        rots::combat::checked_movement movement { context.actor.abs_number, NORTH, 0, 1 };
        if (refusal == 0) {
            context.outbound.exit_info = EX_CLOSED | EX_ISDOOR;
        } else if (refusal == 1) {
            context.outbound.to_room = NOWHERE;
        } else if (refusal == 2) {
            context.actor.delay.wait_value = 5;
            context.actor.delay.priority = 90;
        } else if (refusal == 3) {
            movement.actor_number = 9459;
        } else {
            movement.direction = NUM_OF_DIRS;
        }
        rots::combat::move_after_validation(&context.actor, movement, SCMD_FLEE);
        EXPECT_EQ(location_of(&context.actor), 0);
        EXPECT_EQ(context.marker_count(), 1);
        context.outbound.exit_info = 0;
        context.outbound.to_room = 1;
        context.actor.delay.wait_value = 0;
        do_move(&context.actor, mutable_arg("north"), nullptr, NORTH + 1, SCMD_MOVING);
        EXPECT_EQ(location_of(&context.actor), 1);
        EXPECT_EQ(context.marker_count(), 2);
    }
}

TEST(MovementTriggers, ABeforeEnterScriptCannotRetargetAnAlreadyCapturedExit)
{
    MovementTriggerContext context;
    for (int room = 0; room < 3; ++room) {
        room_by_id_total(room)->number = 100 + room;
    }
    context.terminal.command_type = SCRIPT_CHANGE_EXIT_TO;
    context.terminal.param[0] = SCRIPT_PARAM_CH2_ROOM;
    context.terminal.param[1] = NORTH;
    context.terminal.param[2] = 102;
    context.queue_draws();
    do_move(&context.actor, mutable_arg("north"), nullptr, NORTH + 1, SCMD_MOVING);
    EXPECT_EQ(context.outbound.to_room, 2);
    EXPECT_EQ(location_of(&context.actor), 0);
    EXPECT_EQ(context.marker_count(), 1);
}

TEST(MovementTriggers, PrimaryRiderRelocationDuringMountCompletionCancelsTheTransfer)
{
    MountedMovementContext context;
    room_by_id_total(0)->funct = relocate_during_move;
    movement_relocate_on_call = 5; // probe, flee special, rider recheck, mount check, mount completion
    context.scene.queue_draws();
    do_flee(&context.scene.actor, mutable_arg(""), nullptr, 0, 0);
    EXPECT_EQ(location_of(&context.scene.actor), 2);
    EXPECT_EQ(location_of(&context.mount), 0);
    EXPECT_EQ(location_of(&context.passenger), 0);
}

namespace {

// The callback-owned rider to free, and the actor whose arrival triggers it.
std::unique_ptr<char_data>* retired_rider_owner = nullptr;
char_data* retirement_trigger = nullptr;

SPECIAL(free_next_rider_on_arrival)
{
    if (callflag == SPECIAL_ENTER && ch == retirement_trigger
        && retired_rider_owner && *retired_rider_owner) {
        char_data* rider = retired_rider_owner->get();
        stop_riding(rider);
        char_from_room(rider);
        remove_char_exists(rider->abs_number);
        retired_rider_owner->reset();
    }
    return 0;
}

} // namespace

TEST(MovementTriggers, ArrivalCallbackMayFreeTheNextRiderBeforeItsTurn)
{
    MountedMovementContext context;
    stop_riding(&context.passenger);
    auto next_rider = std::make_unique<char_data>();
    descriptor_data next_descriptor { };
    initialize_additional_mover(*next_rider, next_descriptor, 9454);
    char_to_room(next_rider.get(), 0);
    next_rider->mount_data.mount = &context.mount;
    next_rider->mount_data.mount_number = context.mount.abs_number;
    context.scene.actor.mount_data.next_rider = next_rider.get();
    context.scene.actor.mount_data.next_rider_number = next_rider->abs_number;
    retired_rider_owner = &next_rider;
    retirement_trigger = &context.scene.actor;
    room_by_id_total(1)->funct = free_next_rider_on_arrival;
    context.scene.queue_draws();
    do_flee(&context.scene.actor, mutable_arg(""), nullptr, 0, 0);
    EXPECT_EQ(next_rider, nullptr);
    EXPECT_EQ(location_of(&context.scene.actor), 1);
    EXPECT_EQ(location_of(&context.mount), 1);
    EXPECT_EQ(location_of(&context.passenger), 0);
    if (next_rider) {
        stop_riding(next_rider.get());
        char_from_room(next_rider.get());
        remove_char_exists(next_rider->abs_number);
    }
    retired_rider_owner = nullptr;
    retirement_trigger = nullptr;
}

void on_windblast_success(char_data* character, int mana_cost, int move_cost);

namespace {

// Callback-owned blast victim and optional later occupant removed by its special.
std::unique_ptr<char_data>* blast_victim_owner = nullptr;
std::unique_ptr<char_data>* blast_later_owner = nullptr;
bool retire_blast_victim = false;

void retire_movement_actor(std::unique_ptr<char_data>& owner)
{
    if (owner) {
        stop_riding(owner.get());
        char_from_room(owner.get());
        remove_char_exists(owner->abs_number);
        owner.reset();
    }
}

SPECIAL(retire_during_windblast)
{
    if (callflag == SPECIAL_COMMAND && blast_victim_owner && ch == blast_victim_owner->get()) {
        if (blast_later_owner) {
            retire_movement_actor(*blast_later_owner);
        }
        if (retire_blast_victim) {
            retire_movement_actor(*blast_victim_owner);
        }
        return 1;
    }
    return 0;
}

} // namespace

TEST(MovementTriggers, WindblastCallbacksMayFreeTheVictimOrALaterOccupant)
{
    for (const bool free_current : { false, true }) {
        MovementTriggerContext context;
        auto victim = std::make_unique<char_data>();
        auto later = std::make_unique<char_data>();
        descriptor_data victim_descriptor { };
        descriptor_data later_descriptor { };
        initialize_additional_mover(*victim, victim_descriptor, 9454);
        initialize_additional_mover(*later, later_descriptor, 9455);
        victim->abilities.hit = victim->tmpabilities.hit = 1000;
        later->abilities.hit = later->tmpabilities.hit = 1000;
        char_to_room(victim.get(), 0);
        char_to_room(later.get(), 0);
        blast_victim_owner = &victim;
        blast_later_owner = &later;
        retire_blast_victim = free_current;
        room_by_id_total(0)->funct = retire_during_windblast;
        push_test_random_value(0.0); // blast damage
        push_test_random_value(0.99); // victim fails its save and gets a movement attempt
        context.queue_draws();
        on_windblast_success(&context.actor, 0, 0);
        EXPECT_EQ(later, nullptr);
        EXPECT_EQ(victim == nullptr, free_current);
        EXPECT_EQ(location_of(&context.actor), 0);
        retire_movement_actor(victim);
        retire_movement_actor(later);
        blast_victim_owner = nullptr;
        blast_later_owner = nullptr;
    }
}

void assign_command_pointers(void);

namespace {

// The early follower callback may detach or retire its leader's mount.
char_data* early_follower = nullptr;
char_data* early_leader = nullptr;
std::unique_ptr<char_data>* early_mount_owner = nullptr;
int early_mount_action = 0;
bool early_callback_ran = false;

SPECIAL(change_mount_from_follower)
{
    if (callflag == SPECIAL_COMMAND && ch == early_follower) {
        early_callback_ran = true;
        if (early_mount_action != 0) {
            stop_riding(early_leader);
        }
        if (early_mount_action == 2) {
            retire_movement_actor(*early_mount_owner);
        }
        return 1;
    }
    return 0;
}

// Replacement storage deliberately differs while retaining the old registry slot.
char_data* replaced_passenger = nullptr;
char_data* replacement_passenger = nullptr;
char_data* replacement_mount = nullptr;
bool passenger_replaced = false;

SPECIAL(replace_passenger_during_check)
{
    if (callflag == SPECIAL_COMMAND && ch == replaced_passenger && !passenger_replaced) {
        stop_riding(replaced_passenger);
        remove_char_exists(replaced_passenger->abs_number);
        set_char_exists(replacement_passenger->abs_number, replacement_passenger);
        replacement_passenger->mount_data.mount = replacement_mount;
        replacement_passenger->mount_data.mount_number = replacement_mount->abs_number;
        replacement_mount->mount_data.rider->mount_data.next_rider = replacement_passenger;
        replacement_mount->mount_data.rider->mount_data.next_rider_number = replacement_passenger->abs_number;
        passenger_replaced = true;
        return 1;
    }
    return 0;
}

} // namespace

TEST(MovementTriggers, EarlyFollowerCannotDetachOrFreeTheMountBeforeItsMovementCharge)
{
    assign_command_pointers();
    for (const int action : { 0, 1, 2 }) {
        SCOPED_TRACE(action);
        MountedMovementContext context;
        stop_riding(&context.passenger);
        stop_riding(&context.scene.actor);
        auto mount = std::make_unique<char_data>();
        descriptor_data mount_descriptor { };
        initialize_additional_mover(*mount, mount_descriptor, 9454);
        mount->specials2.act |= MOB_MOUNT;
        char_to_room(mount.get(), 0);
        mount->mount_data.rider = &context.scene.actor;
        mount->mount_data.rider_number = context.scene.actor.abs_number;
        context.scene.actor.mount_data.mount = mount.get();
        context.scene.actor.mount_data.mount_number = mount->abs_number;
        context.passenger.specials2.act |= MOB_ORC_FRIEND | MOB_PET;
        context.passenger.master = &context.scene.actor;
        follow_type follower { context.passenger.abs_number, &context.passenger, nullptr };
        context.scene.actor.followers = &follower;
        early_follower = &context.passenger;
        early_leader = &context.scene.actor;
        early_mount_owner = &mount;
        early_mount_action = action;
        early_callback_ran = false;
        room_by_id_total(0)->funct = change_mount_from_follower;
        for (int draw = 0; draw < 64; ++draw) {
            push_test_random_value(0.99);
        }
        do_move(&context.scene.actor, mutable_arg("north"), nullptr, NORTH + 1, SCMD_MOVING);
        EXPECT_TRUE(early_callback_ran);
        int expected_room = 0;
        if (action == 0) {
            expected_room = 1;
        }
        EXPECT_EQ(location_of(&context.scene.actor), expected_room);
        EXPECT_EQ(mount == nullptr, action == 2);
        if (mount) {
            EXPECT_EQ(location_of(mount.get()), expected_room);
            stop_riding(&context.scene.actor);
            retire_movement_actor(mount);
        }
        context.scene.actor.followers = nullptr;
        context.passenger.master = nullptr;
        early_follower = nullptr;
        early_leader = nullptr;
        early_mount_owner = nullptr;
    }
}

TEST(MovementTriggers, RecycledPassengerSlotCannotInheritTheOriginalMovementCheck)
{
    MountedMovementContext context;
    char_data replacement { };
    descriptor_data replacement_descriptor { };
    initialize_additional_mover(replacement, replacement_descriptor, 9454);
    remove_char_exists(replacement.abs_number);
    replacement.abs_number = context.passenger.abs_number;
    char_to_room(&replacement, 0);
    replaced_passenger = &context.passenger;
    replacement_passenger = &replacement;
    replacement_mount = &context.mount;
    passenger_replaced = false;
    room_by_id_total(0)->funct = replace_passenger_during_check;
    context.scene.queue_draws();
    do_flee(&context.scene.actor, mutable_arg(""), nullptr, 0, 0);
    EXPECT_TRUE(passenger_replaced);
    EXPECT_EQ(location_of(&context.scene.actor), 1);
    EXPECT_EQ(location_of(&replacement), 0);
    EXPECT_EQ(std::string_view(replacement_descriptor.output).find("before-enter-marker"), std::string_view::npos);
    stop_riding(&replacement);
    char_from_room(&replacement);
    remove_char_exists(replacement.abs_number);
    set_char_exists(context.passenger.abs_number, &context.passenger);
    replaced_passenger = nullptr;
    replacement_passenger = nullptr;
    replacement_mount = nullptr;
}
