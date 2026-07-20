// combat_hooks_tests.cpp

// New test TU (blocker-buster wave, Task 2; plan
// docs/superpowers/plans/2026-07-19-blocker-buster.md; brief
// .superpowers/sdd/task-2-brief.md). Covers combat_hooks.h's boot-registered
// command-dispatch table (rots::combat::issue_command()): four
// representative cells (do_hit/do_flee, required by the brief, plus
// do_stand/do_wake as the "+2 more representative cells") each get BOTH
// halves of coverage -- issue_command() reaches the real ACMD body when the
// table is registered (gtest_main.cpp calls register_combat_command_
// dispatch() process-wide, mirroring every other *_hooks.h suite's
// registration parity), and defaults to a no-op when a cell is
// unregistered.
//
// DISCRIMINATOR SHAPE (mirrors comm_delay_tests.cpp's convention, adapted
// for ACMD's void/side-effecting shape): each REACHES-THE-REAL-BODY test
// drives ch/argument/wtl into the cheapest, most deterministic branch of the
// real do_x() body (verified by reading act_offe.cpp/act_move.cpp -- see
// each TEST's own comment for the exact branch and why it needs no heavier
// fixture) and asserts the branch's specific, otherwise-impossible side
// effect happened. Each DEFAULTS-TO-A-NO-OP test clears that ONE cell via
// ScopedUnregisteredCombatCommand (below), drives the identical fixture, and
// asserts NOTHING moved -- proving the tripwire-logged default is a true
// no-op, not a partial or silently-wrong mutation, the same proof
// obligation comm_delay_tests.cpp's suite documents for output_seam.h.
//
// Two discriminator styles, chosen per cell by which is cheaper to set up:
//   - do_hit/do_flee: their cheapest branches are message-only (no state
//     mutation), so ScopedCapturingOutputSink (below) captures the
//     send_to_char() text instead -- do_flee's TACTICS_BERSERK guard needs
//     no world[] at all; do_hit's no-victim branch needs a one-room
//     ScopedTestWorld (test_world.h) because its very first guard reads
//     world[ch->in_room].room_flags before anything else.
//   - do_stand/do_wake: their cheapest branches ARE a state mutation
//     (GET_POS(ch) flips), so these assert on that field directly, no
//     output capture needed. Both call act(..., TO_ROOM, ...) on the chosen
//     branch; character.in_room = NOWHERE keeps that call a proven no-op
//     (comm.cpp's act_impl() only walks world[] for TO_ROOM when
//     ch->in_room != NOWHERE -- verified by reading act_impl(), the same
//     precedent weapon_master_handler_tests.cpp's
//     SwordProcRegainsEnergyWhenSlashProcSucceeds documents), so neither
//     needs ScopedTestWorld.
#include "../combat_hooks.h"
#include "../comm.h"
#include "../interpre.h"
#include "../output_seam.h"
#include "../utils.h"
#include "rots/core/character.h"
#include "test_world.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

namespace {

// Captures the most recent send_to_char() message for this scope, then
// restores comm.cpp's real output sinks on destruction -- adapted from
// comm_delay_tests.cpp's ScopedOutputSinks (same swap-then-
// register_game_output_sinks()-on-exit RAII shape), but installs a
// CAPTURING send_to_char instead of a null one: do_hit's/do_flee's chosen
// discriminator branches (see each TEST's own comment) are message-only, no
// state mutation, so this is how those tests observe "the real ACMD body
// ran" instead of asserting on a struct field the way the do_stand/do_wake
// tests below do. Every other Sinks field stays null for this scope; do_hit/
// do_flee's chosen branches call nothing but send_to_char, so no other
// forwarder needs a real (or captured) sink here.
class ScopedCapturingOutputSink {
public:
    ScopedCapturingOutputSink()
    {
        last_message.clear();
        rots::output::Sinks sinks {};
        sinks.send_to_char = &capture;
        rots::output::set_sinks(sinks);
    }

    ~ScopedCapturingOutputSink()
    {
        register_game_output_sinks();
    }

    ScopedCapturingOutputSink(const ScopedCapturingOutputSink&) = delete;
    ScopedCapturingOutputSink& operator=(const ScopedCapturingOutputSink&) = delete;

    static std::string last_message;

private:
    static void capture(std::string_view message, char_data*)
    {
        last_message = std::string(message);
    }
};

std::string ScopedCapturingOutputSink::last_message;

// Temporarily clears ONE cell of combat_hooks.h's dispatch table (every
// other cell -- and every other seam's hooks -- stays registered), then
// restores the FULL table via register_combat_command_dispatch() on
// destruction. There is no per-cell "restore just this one" entry point, so
// re-running the (idempotent) registrar is the cheapest way back to the
// real state -- the same "swap out, restore via the real registrar" shape
// as ScopedCapturingOutputSink/ScopedOutputSinks above/elsewhere.
class ScopedUnregisteredCombatCommand {
public:
    explicit ScopedUnregisteredCombatCommand(rots::combat::combat_command command)
    {
        rots::combat::set_combat_command(command, nullptr);
    }

    ~ScopedUnregisteredCombatCommand()
    {
        register_combat_command_dispatch();
    }

    ScopedUnregisteredCombatCommand(const ScopedUnregisteredCombatCommand&) = delete;
    ScopedUnregisteredCombatCommand& operator=(const ScopedUnregisteredCombatCommand&) = delete;
};

} // namespace

// do_hit (act_offe.cpp) -- DISCRIMINATOR: the no-victim-resolved branch
// ("Hit who?\n\r"), the cheapest deterministic path through do_hit's real
// body (empty argument -> *arg stays 0 -> wtl is null so victim stays 0 ->
// the final `else { if (*arg) ... else send_to_char("Hit who?\n\r", ch); }`
// branch). Needs a one-room ScopedTestWorld: do_hit's very first statement
// (IS_SET(world[ch->in_room].room_flags, PEACEROOM)) reads world[] before
// anything else, so an unallocated BASE_WORLD would crash before reaching
// the discriminator at all; room_flags is zeroed explicitly (defensive
// against a reused world from an earlier suite in the monolithic runner).

TEST(CombatHooksDispatch, IssueCommandReachesTheRealDoHitWhenRegistered)
{
    ScopedTestWorld test_world(1);
    test_world.room().room_flags = 0;
    ScopedCapturingOutputSink capture;
    char_data character {};
    character.in_room = 0;

    rots::combat::issue_command(
        rots::combat::combat_command::hit, &character, mutable_arg(""), nullptr, 0, 0);

    EXPECT_EQ(ScopedCapturingOutputSink::last_message, "Hit who?\n\r")
        << "Expected the real do_hit body's no-victim branch to send its literal message.";
}

TEST(CombatHooksDispatch, IssueCommandDefaultsToANoOpWhenHitIsUnregistered)
{
    ScopedTestWorld test_world(1);
    test_world.room().room_flags = 0;
    ScopedUnregisteredCombatCommand unregistered(rots::combat::combat_command::hit);
    ScopedCapturingOutputSink capture;
    char_data character {};
    character.in_room = 0;

    rots::combat::issue_command(
        rots::combat::combat_command::hit, &character, mutable_arg(""), nullptr, 0, 0);

    EXPECT_TRUE(ScopedCapturingOutputSink::last_message.empty())
        << "Expected an unregistered hit cell to leave send_to_char uncalled -- the real do_hit "
           "body never ran.";
}

// do_flee (act_offe.cpp) -- DISCRIMINATOR: the TACTICS_BERSERK guard is the
// very first statement in do_flee's body and returns before touching
// world[]/exits/any other state, so it needs no ScopedTestWorld at all --
// the cheapest possible do_flee discriminator.

TEST(CombatHooksDispatch, IssueCommandReachesTheRealDoFleeWhenRegistered)
{
    ScopedCapturingOutputSink capture;
    char_data character {};
    character.specials.tactics = TACTICS_BERSERK;

    rots::combat::issue_command(
        rots::combat::combat_command::flee, &character, mutable_arg(""), nullptr, 0, 0);

    EXPECT_EQ(ScopedCapturingOutputSink::last_message, "You are too enraged to flee!\n\r")
        << "Expected the real do_flee body's berserk guard to send its literal message.";
}

TEST(CombatHooksDispatch, IssueCommandDefaultsToANoOpWhenFleeIsUnregistered)
{
    ScopedUnregisteredCombatCommand unregistered(rots::combat::combat_command::flee);
    ScopedCapturingOutputSink capture;
    char_data character {};
    character.specials.tactics = TACTICS_BERSERK;

    rots::combat::issue_command(
        rots::combat::combat_command::flee, &character, mutable_arg(""), nullptr, 0, 0);

    EXPECT_TRUE(ScopedCapturingOutputSink::last_message.empty())
        << "Expected an unregistered flee cell to leave send_to_char uncalled -- the real "
           "do_flee body never ran.";
}

// do_stand (act_move.cpp) -- DISCRIMINATOR: the POSITION_SITTING branch's
// unconditional `GET_POS(ch) = POSITION_STANDING` (character.specials.fighting
// is null, so the non-fighting arm of that branch's inner if/else runs) -- a
// real state mutation, distinct from a no-op default, that needs no output
// capture. See this file's header comment for why NOWHERE keeps the
// branch's act(..., TO_ROOM, ...) calls a safe no-op.

TEST(CombatHooksDispatch, IssueCommandReachesTheRealDoStandWhenRegistered)
{
    char_data character {};
    character.in_room = NOWHERE;
    character.specials.position = POSITION_SITTING;
    character.specials.fighting = nullptr;

    rots::combat::issue_command(
        rots::combat::combat_command::stand, &character, mutable_arg(""), nullptr, 0, 0);

    EXPECT_EQ(GET_POS(&character), POSITION_STANDING)
        << "Expected the real do_stand body to stand the character up from sitting.";
}

TEST(CombatHooksDispatch, IssueCommandDefaultsToANoOpWhenStandIsUnregistered)
{
    ScopedUnregisteredCombatCommand unregistered(rots::combat::combat_command::stand);
    char_data character {};
    character.in_room = NOWHERE;
    character.specials.position = POSITION_SITTING;
    character.specials.fighting = nullptr;

    rots::combat::issue_command(
        rots::combat::combat_command::stand, &character, mutable_arg(""), nullptr, 0, 0);

    EXPECT_EQ(GET_POS(&character), POSITION_SITTING)
        << "Expected an unregistered stand cell to leave the character's position untouched.";
}

// do_wake (act_move.cpp) -- DISCRIMINATOR: the empty-argument/already-asleep
// branch's unconditional `GET_POS(ch) = POSITION_SITTING` -- another real
// state mutation, same NOWHERE rationale as do_stand above (this branch
// also fires an act(..., TO_ROOM, ...) call).

TEST(CombatHooksDispatch, IssueCommandReachesTheRealDoWakeWhenRegistered)
{
    char_data character {};
    character.in_room = NOWHERE;
    character.specials.position = POSITION_SLEEPING;

    rots::combat::issue_command(
        rots::combat::combat_command::wake, &character, mutable_arg(""), nullptr, 0, 0);

    EXPECT_EQ(GET_POS(&character), POSITION_SITTING)
        << "Expected the real do_wake body to sit the character up from sleeping.";
}

TEST(CombatHooksDispatch, IssueCommandDefaultsToANoOpWhenWakeIsUnregistered)
{
    ScopedUnregisteredCombatCommand unregistered(rots::combat::combat_command::wake);
    char_data character {};
    character.in_room = NOWHERE;
    character.specials.position = POSITION_SLEEPING;

    rots::combat::issue_command(
        rots::combat::combat_command::wake, &character, mutable_arg(""), nullptr, 0, 0);

    EXPECT_EQ(GET_POS(&character), POSITION_SLEEPING)
        << "Expected an unregistered wake cell to leave the character's position untouched.";
}
