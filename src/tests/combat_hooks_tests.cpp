// combat_hooks_tests.cpp

// New test TU (blocker-buster wave, Task 2; plan
// docs/superpowers/plans/2026-07-19-blocker-buster.md; brief
// .superpowers/sdd/task-2-brief.md). Covers combat_hooks.h's boot-registered
// command-dispatch table (rots::combat::issue_command()): five
// representative cells (do_hit/do_flee, required by the brief; do_stand/
// do_wake as the brief's "+2 more representative cells"; do_mental added
// post-landing per review -- fight.cpp's per-tick mental-combat auto-attack
// calls it every combat pulse for a fighting mental/shadow combatant, the
// highest-value cell in the table) each get BOTH halves of coverage --
// issue_command() reaches the real ACMD body when the table is registered
// (gtest_main.cpp calls register_combat_command_dispatch() process-wide,
// mirroring every other *_hooks.h suite's registration parity), and
// defaults to a no-op when a cell is unregistered.
//
// DISCRIMINATOR SHAPE (mirrors comm_delay_tests.cpp's convention, adapted
// for ACMD's void/side-effecting shape): each REACHES-THE-REAL-BODY test
// drives ch/argument/wtl into the cheapest, most deterministic branch of the
// real do_x() body (verified by reading act_offe.cpp/act_move.cpp/
// clerics.cpp -- see each TEST's own comment for the exact branch and why it
// needs no heavier fixture) and asserts the branch's specific,
// otherwise-impossible side effect happened. Each DEFAULTS-TO-A-NO-OP test
// clears that ONE cell via ScopedUnregisteredCombatCommand (below), drives
// the identical fixture, and asserts NOTHING moved -- proving the
// tripwire-logged default is a true no-op, not a partial or silently-wrong
// mutation, the same proof obligation comm_delay_tests.cpp's suite
// documents for output_seam.h.
//
// Two discriminator styles, chosen per cell by which is cheaper to set up:
//   - do_hit/do_flee/do_mental: their cheapest branches are message-only (no
//     state mutation), so ScopedCapturingOutputSink (below) captures the
//     send_to_char() text instead -- do_flee's TACTICS_BERSERK guard needs
//     no world[] at all; do_hit's/do_mental's own first guards each need a
//     one-room ScopedTestWorld (test_world.h) because both read
//     world[ch->in_room].room_flags (the same PEACEROOM check, verified
//     identical in act_offe.cpp and clerics.cpp) before anything else.
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
#include "../db.h"
#include "../handler.h"
#include "../interpre.h"
#include "../limits.h"
#include "../output_seam.h"
#include "../pkill.h"
#include "../script.h"
#include "../utils.h"
#include "rots/core/character.h"
#include "rots/core/descriptor.h"
#include "rots/core/object.h"
#include "test_world.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

extern social_messg* soc_mess_list;

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
// do_look (act_info.cpp) -- DISCRIMINATOR: the GET_POS(ch) < POSITION_SLEEPING
// guard ("You can't see anything but stars!\n\r"), the cheapest deterministic
// path through do_look's real body. Reaching it needs ch->desc to be a
// non-null descriptor whose own `descriptor` field is truthy (do_look's very
// first two statements are `if (!ch->desc) return;` / `if
// (!ch->desc->descriptor) return;`, both silent early-outs -- without a real
// descriptor fixture, this test could not tell "the real body ran and hit an
// early guard" from "an unregistered cell no-op'd", since both look like
// nothing happened). No ScopedTestWorld needed at all: the position guard
// fires before any world[]/CAN_SEE() access, the same "cheapest branch"
// shape as do_flee's TACTICS_BERSERK guard above. spell-family closure wave
// Task 3: mage.cpp's do_look(...) calls (mage.cpp:834/966/1185/1280) are
// this cell's first real issue_command() caller anywhere in the tree -- the
// recurring say/move/dismount "first-caller pair" gap this wave's own
// census flagged as likely for look/hit/move.

TEST(CombatHooksDispatch, IssueCommandReachesTheRealDoLookWhenRegistered)
{
    ScopedCapturingOutputSink capture;
    descriptor_data desc {};
    desc.descriptor = 1;
    char_data character {};
    character.desc = &desc;
    character.specials.position = POSITION_STUNNED;

    rots::combat::issue_command(
        rots::combat::combat_command::look, &character, mutable_arg(""), nullptr, 0, 0);

    EXPECT_EQ(ScopedCapturingOutputSink::last_message, "You can't see anything but stars!\n\r")
        << "Expected the real do_look body's low-position guard to send its literal message.";
}

TEST(CombatHooksDispatch, IssueCommandDefaultsToANoOpWhenLookIsUnregistered)
{
    ScopedUnregisteredCombatCommand unregistered(rots::combat::combat_command::look);
    ScopedCapturingOutputSink capture;
    descriptor_data desc {};
    desc.descriptor = 1;
    char_data character {};
    character.desc = &desc;
    character.specials.position = POSITION_STUNNED;

    rots::combat::issue_command(
        rots::combat::combat_command::look, &character, mutable_arg(""), nullptr, 0, 0);

    EXPECT_TRUE(ScopedCapturingOutputSink::last_message.empty())
        << "Expected an unregistered look cell to leave send_to_char uncalled -- the real "
           "do_look body never ran.";
}


// do_dismount (ranger.cpp) -- DISCRIMINATOR: the IS_RIDING(ch)==false branch's
// unconditional send_to_char("You are not riding anything.\n\r", ch) -- the
// cheapest deterministic path through do_dismount's real body. IS_RIDING(ch)
// expands to (ch->mount_data.mount && char_exists(ch->mount_data.mount_number))
// (utils.h), so a default-initialized char_data{} (mount_data.mount == nullptr)
// short-circuits false without needing ScopedTestWorld/char_exists -- message-
// only, no state mutation, the same "cheapest branch" shape as do_flee's
// TACTICS_BERSERK guard above. do_dismount is this task's new 26th cell
// (combat_hooks.h/trio-task-1-brief.md Step 1): its real body still lives in
// ranger.cpp, a still-app-compiled combat-row TU (combat-trio-census.md
// section 5.1), so this pair proves the seam reaches across that boundary
// exactly like do_flee's/do_stand's existing pairs already do for their own
// still-app owners.

TEST(CombatHooksDispatch, IssueCommandReachesTheRealDoDismountWhenRegistered)
{
    ScopedCapturingOutputSink capture;
    char_data character {};
    character.mount_data.mount = nullptr;

    rots::combat::issue_command(
        rots::combat::combat_command::dismount, &character, mutable_arg(""), nullptr, 0, 0);

    EXPECT_EQ(ScopedCapturingOutputSink::last_message, "You are not riding anything.\n\r")
        << "Expected the real do_dismount body's not-riding branch to send its literal message.";
}

TEST(CombatHooksDispatch, IssueCommandDefaultsToANoOpWhenDismountIsUnregistered)
{
    ScopedUnregisteredCombatCommand unregistered(rots::combat::combat_command::dismount);
    ScopedCapturingOutputSink capture;
    char_data character {};
    character.mount_data.mount = nullptr;

    rots::combat::issue_command(
        rots::combat::combat_command::dismount, &character, mutable_arg(""), nullptr, 0, 0);

    EXPECT_TRUE(ScopedCapturingOutputSink::last_message.empty())
        << "Expected an unregistered dismount cell to leave send_to_char uncalled -- the real "
           "do_dismount body never ran.";
}

// do_move (act_move.cpp) -- DISCRIMINATOR: the no-exit branch's unconditional
// send_to_char("You cannot go that way.\n\r", ch) -- the cheapest
// deterministic path through do_move's real body. Unlike do_flee's/
// do_dismount's guards above, do_move's first two checks (AFF_HAZE,
// delay.wait_value) both short-circuit false on a default-initialized
// char_data{} (specials.affected_by == 0, delay.wait_value == 0) without
// touching world[], but its THIRD statement -- `!world[ch->in_room].
// dir_option[cmd]` -- reads world[] unconditionally, so this pair needs
// ScopedTestWorld exactly like do_hit's own PEACEROOM guard above (verified
// by reading act_move.cpp:646-706). do_move decrements its own `cmd`
// parameter before that read (`--cmd;`, line 661), so passing cmd=1 lines up
// with dir_option[0], zeroed by ScopedTestWorld's dummy_room_data() init --
// explicitly re-nulled here too, defensively, against a reused world from an
// earlier suite in the monolithic runner (same defensive stance as do_hit's
// own room_flags reset). do_move is this task's other genuine consumer
// (combat-trio wave Task 2; olog_hai.cpp's do_overrun body converted its
// direct do_move() call to issue_command()), so this pair proves the seam
// reaches this cell for real, mirroring do_flee's/do_dismount's existing
// pairs for their own still-app-owned bodies.

TEST(CombatHooksDispatch, IssueCommandReachesTheRealDoMoveWhenRegistered)
{
    ScopedTestWorld test_world(1);
    test_world.room().dir_option[0] = nullptr;
    ScopedCapturingOutputSink capture;
    char_data character {};
    character.in_room = 0;

    rots::combat::issue_command(
        rots::combat::combat_command::move, &character, mutable_arg(""), nullptr, 1, 0);

    EXPECT_EQ(ScopedCapturingOutputSink::last_message, "You cannot go that way.\n\r")
        << "Expected the real do_move body's no-exit branch to send its literal message.";
}

TEST(CombatHooksDispatch, IssueCommandDefaultsToANoOpWhenMoveIsUnregistered)
{
    ScopedTestWorld test_world(1);
    test_world.room().dir_option[0] = nullptr;
    ScopedUnregisteredCombatCommand unregistered(rots::combat::combat_command::move);
    ScopedCapturingOutputSink capture;
    char_data character {};
    character.in_room = 0;

    rots::combat::issue_command(
        rots::combat::combat_command::move, &character, mutable_arg(""), nullptr, 1, 0);

    EXPECT_TRUE(ScopedCapturingOutputSink::last_message.empty())
        << "Expected an unregistered move cell to leave send_to_char uncalled -- the real "
           "do_move body never ran.";
}

// do_say (act_comm.cpp) -- DISCRIMINATOR: the IS_NPC(ch) && GET_INT(ch) < 6
// guard is the very first statement in do_say's real body and returns before
// touching world[]/wtl/anything else, so it needs no ScopedTestWorld at all --
// the same "cheapest possible" shape as do_flee's/do_dismount's guards above.
// A default-initialized char_data{} already has tmpabilities.intel == 0 (< 6);
// only specials2.act needs MOB_ISNPC set. This cell's own real body
// (interpre.cpp:2274, `set_combat_command(combat_command::say, do_say);`) had
// zero issue_command() callers anywhere in the tree before the l4-seed wave's
// Task 2 (l4-task-2-brief.md Step 1/2/3) converted graph.cpp's/mudlle.cpp's/
// mudlle2.cpp's 14 direct do_say() call sites onto this cell -- this pair is
// that task's Step 5 discriminator-audit gap-fill, mirroring do_move's own
// pair (added when olog_hai.cpp became its first real consumer) for the same
// "newly exercised, previously registered-but-uncalled" reason.

TEST(CombatHooksDispatch, IssueCommandReachesTheRealDoSayWhenRegistered)
{
    ScopedCapturingOutputSink capture;
    char_data character {};
    character.specials2.act = MOB_ISNPC;

    rots::combat::issue_command(
        rots::combat::combat_command::say, &character, mutable_arg(""), nullptr, 0, 0);

    EXPECT_EQ(ScopedCapturingOutputSink::last_message, "You are too stupid to talk.\n\r")
        << "Expected the real do_say body's low-intelligence-NPC guard to send its literal "
           "message.";
}

TEST(CombatHooksDispatch, IssueCommandDefaultsToANoOpWhenSayIsUnregistered)
{
    ScopedUnregisteredCombatCommand unregistered(rots::combat::combat_command::say);
    ScopedCapturingOutputSink capture;
    char_data character {};
    character.specials2.act = MOB_ISNPC;

    rots::combat::issue_command(
        rots::combat::combat_command::say, &character, mutable_arg(""), nullptr, 0, 0);

    EXPECT_TRUE(ScopedCapturingOutputSink::last_message.empty())
        << "Expected an unregistered say cell to leave send_to_char uncalled -- the real do_say "
           "body never ran.";
}

// do_gen_com (act_comm.cpp) -- DISCRIMINATOR: same IS_NPC(ch) && GET_INT(ch) <
// 6 guard as do_say's above (do_gen_com's third early-return, act_comm.cpp),
// reached with the identical fixture -- PLR_FLAGGED(ch, PLR_NOSHOUT) is false
// for an NPC (PLR_FLAGGED short-circuits on !IS_NPC(ch)) and
// MOB_FLAGGED(ch, MOB_PET) is false because specials2.act only carries the
// MOB_ISNPC bit, so control falls through both earlier guards into this one
// without touching com_msgs[]/world[]/wtl. This cell's real body
// (interpre.cpp:2252, `set_combat_command(combat_command::gen_com,
// do_gen_com);`) had zero issue_command() callers anywhere in the tree before
// Cluster B wave Task 2 (cb-task-2-brief.md Step 1; cb-census.md section 5.3)
// converted script.cpp's one do_gen_com() call site (SCRIPT_DO_YELL) onto
// this cell -- this pair is that task's Step 3 discriminator-audit gap-fill,
// mirroring do_say's/do_move's own pairs (added when their first real
// consumer landed) for the same "newly exercised, previously
// registered-but-uncalled" reason.

TEST(CombatHooksDispatch, IssueCommandReachesTheRealDoGenComWhenRegistered)
{
    ScopedCapturingOutputSink capture;
    char_data character {};
    character.specials2.act = MOB_ISNPC;

    rots::combat::issue_command(
        rots::combat::combat_command::gen_com, &character, mutable_arg(""), nullptr, 0, 0);

    EXPECT_EQ(ScopedCapturingOutputSink::last_message, "You are too stupid to talk.\n\r")
        << "Expected the real do_gen_com body's low-intelligence-NPC guard to send its literal "
           "message.";
}

TEST(CombatHooksDispatch, IssueCommandDefaultsToANoOpWhenGenComIsUnregistered)
{
    ScopedUnregisteredCombatCommand unregistered(rots::combat::combat_command::gen_com);
    ScopedCapturingOutputSink capture;
    char_data character {};
    character.specials2.act = MOB_ISNPC;

    rots::combat::issue_command(
        rots::combat::combat_command::gen_com, &character, mutable_arg(""), nullptr, 0, 0);

    EXPECT_TRUE(ScopedCapturingOutputSink::last_message.empty())
        << "Expected an unregistered gen_com cell to leave send_to_char uncalled -- the real "
           "do_gen_com body never ran.";
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

// do_assist (act_offe.cpp) -- DISCRIMINATOR: the very first statement --
// `if (ch->specials.fighting) { send_to_char("You're already fighting!  How
// can you assist someone else?\n\r", ch); return; }` -- fires on any
// non-null specials.fighting pointer (never dereferenced on this branch, so
// a self-pointer is enough), needing no world[]/wtl/argument parsing at
// all. Message-only, so this uses ScopedCapturingOutputSink like do_hit/
// do_flee above rather than a state-mutation assertion.
// (behavior wave Task 2, bw-task-2-brief.md Step 4 -- one of six genuine
// discriminator gaps mobact.cpp's newly-converted do_* cells surfaced: the
// cell has been registered since the combat-hooks seam landed, but nothing
// exercised it from the caller side until now, the same "long-registered
// cell can still lack a caller-side discriminator" shape as the l4-seed
// wave's own say-cell gap-fill.)

TEST(CombatHooksDispatch, IssueCommandReachesTheRealDoAssistWhenRegistered)
{
    ScopedCapturingOutputSink capture;
    char_data character {};
    character.specials.fighting = &character;

    rots::combat::issue_command(
        rots::combat::combat_command::assist, &character, mutable_arg(""), nullptr, 0, 0);

    EXPECT_EQ(ScopedCapturingOutputSink::last_message,
        "You're already fighting!  How can you assist someone else?\n\r")
        << "Expected the real do_assist body's already-fighting branch to send its literal "
           "message.";
}

TEST(CombatHooksDispatch, IssueCommandDefaultsToANoOpWhenAssistIsUnregistered)
{
    ScopedUnregisteredCombatCommand unregistered(rots::combat::combat_command::assist);
    ScopedCapturingOutputSink capture;
    char_data character {};
    character.specials.fighting = &character;

    rots::combat::issue_command(
        rots::combat::combat_command::assist, &character, mutable_arg(""), nullptr, 0, 0);

    EXPECT_TRUE(ScopedCapturingOutputSink::last_message.empty())
        << "Expected an unregistered assist cell to leave send_to_char uncalled -- the real "
           "do_assist body never ran.";
}

// do_rescue (act_offe.cpp) -- DISCRIMINATOR: the wtl-present branch's first
// guard -- `if (wtl) { if ((wtl->targ1.type != TARGET_CHAR) || ...) {
// send_to_char("Alas! You lost your victim.\n\r", ch); return; } }` -- a
// zero-initialized waiting_type has targ1.type == 0 (TARGET_CHAR == 1, per
// rots/core/types.h), so this fires immediately, before IS_SHADOW/
// MOB_ORC_FRIEND/anything else is even reached (both are already false for
// a zero-initialized char_data). Cheapest possible do_rescue discriminator:
// no world[]/char lookup needed.
// (behavior wave Task 2 gap-fill, see do_assist's comment above.)

TEST(CombatHooksDispatch, IssueCommandReachesTheRealDoRescueWhenRegistered)
{
    ScopedCapturingOutputSink capture;
    char_data character {};
    waiting_type wtl {};

    rots::combat::issue_command(
        rots::combat::combat_command::rescue, &character, mutable_arg(""), &wtl, 0, 0);

    EXPECT_EQ(ScopedCapturingOutputSink::last_message, "Alas! You lost your victim.\n\r")
        << "Expected the real do_rescue body's invalid-wtl-target branch to send its literal "
           "message.";
}

TEST(CombatHooksDispatch, IssueCommandDefaultsToANoOpWhenRescueIsUnregistered)
{
    ScopedUnregisteredCombatCommand unregistered(rots::combat::combat_command::rescue);
    ScopedCapturingOutputSink capture;
    char_data character {};
    waiting_type wtl {};

    rots::combat::issue_command(
        rots::combat::combat_command::rescue, &character, mutable_arg(""), &wtl, 0, 0);

    EXPECT_TRUE(ScopedCapturingOutputSink::last_message.empty())
        << "Expected an unregistered rescue cell to leave send_to_char uncalled -- the real "
           "do_rescue body never ran.";
}

// do_wear (act_obj2.cpp) -- DISCRIMINATOR: the empty-argument branch's
// `if (!*arg1) { send_to_char("Wear what?\n\r", ch); return; }`, the very
// first check after argument_interpreter() splits an empty argument into
// two empty tokens. Message-only, no world[]/inventory state needed.
// (behavior wave Task 2 gap-fill, see do_assist's comment above.)

TEST(CombatHooksDispatch, IssueCommandReachesTheRealDoWearWhenRegistered)
{
    ScopedCapturingOutputSink capture;
    char_data character {};

    rots::combat::issue_command(
        rots::combat::combat_command::wear, &character, mutable_arg(""), nullptr, 0, 0);

    EXPECT_EQ(ScopedCapturingOutputSink::last_message, "Wear what?\n\r")
        << "Expected the real do_wear body's empty-argument branch to send its literal message.";
}

TEST(CombatHooksDispatch, IssueCommandDefaultsToANoOpWhenWearIsUnregistered)
{
    ScopedUnregisteredCombatCommand unregistered(rots::combat::combat_command::wear);
    ScopedCapturingOutputSink capture;
    char_data character {};

    rots::combat::issue_command(
        rots::combat::combat_command::wear, &character, mutable_arg(""), nullptr, 0, 0);

    EXPECT_TRUE(ScopedCapturingOutputSink::last_message.empty())
        << "Expected an unregistered wear cell to leave send_to_char uncalled -- the real "
           "do_wear body never ran.";
}

// do_sit/do_rest/do_sleep (act_move.cpp) -- DISCRIMINATOR: each one's
// POSITION_STANDING case, the same state-mutation shape as do_stand/do_wake
// above (GET_POS(ch) flips unconditionally; NOWHERE keeps each branch's
// act(..., TO_ROOM, ...) calls a safe no-op, needing no ScopedTestWorld).
// do_sleep's body also checks IS_RIDING(ch) before the switch -- false for
// a zero-initialized character, so the switch is reached directly.
// (behavior wave Task 2 gap-fill, see do_assist's comment above.)

TEST(CombatHooksDispatch, IssueCommandReachesTheRealDoSitWhenRegistered)
{
    char_data character {};
    character.in_room = NOWHERE;
    character.specials.position = POSITION_STANDING;

    rots::combat::issue_command(
        rots::combat::combat_command::sit, &character, mutable_arg(""), nullptr, 0, 0);

    EXPECT_EQ(GET_POS(&character), POSITION_SITTING)
        << "Expected the real do_sit body to sit the character down from standing.";
}

TEST(CombatHooksDispatch, IssueCommandDefaultsToANoOpWhenSitIsUnregistered)
{
    ScopedUnregisteredCombatCommand unregistered(rots::combat::combat_command::sit);
    char_data character {};
    character.in_room = NOWHERE;
    character.specials.position = POSITION_STANDING;

    rots::combat::issue_command(
        rots::combat::combat_command::sit, &character, mutable_arg(""), nullptr, 0, 0);

    EXPECT_EQ(GET_POS(&character), POSITION_STANDING)
        << "Expected an unregistered sit cell to leave the character's position untouched.";
}

TEST(CombatHooksDispatch, IssueCommandReachesTheRealDoRestWhenRegistered)
{
    char_data character {};
    character.in_room = NOWHERE;
    character.specials.position = POSITION_STANDING;

    rots::combat::issue_command(
        rots::combat::combat_command::rest, &character, mutable_arg(""), nullptr, 0, 0);

    EXPECT_EQ(GET_POS(&character), POSITION_RESTING)
        << "Expected the real do_rest body to sit the character down to rest from standing.";
}

TEST(CombatHooksDispatch, IssueCommandDefaultsToANoOpWhenRestIsUnregistered)
{
    ScopedUnregisteredCombatCommand unregistered(rots::combat::combat_command::rest);
    char_data character {};
    character.in_room = NOWHERE;
    character.specials.position = POSITION_STANDING;

    rots::combat::issue_command(
        rots::combat::combat_command::rest, &character, mutable_arg(""), nullptr, 0, 0);

    EXPECT_EQ(GET_POS(&character), POSITION_STANDING)
        << "Expected an unregistered rest cell to leave the character's position untouched.";
}

TEST(CombatHooksDispatch, IssueCommandReachesTheRealDoSleepWhenRegistered)
{
    char_data character {};
    character.in_room = NOWHERE;
    character.specials.position = POSITION_STANDING;

    rots::combat::issue_command(
        rots::combat::combat_command::sleep, &character, mutable_arg(""), nullptr, 0, 0);

    EXPECT_EQ(GET_POS(&character), POSITION_SLEEPING)
        << "Expected the real do_sleep body to lie the character down to sleep from standing.";
}

TEST(CombatHooksDispatch, IssueCommandDefaultsToANoOpWhenSleepIsUnregistered)
{
    ScopedUnregisteredCombatCommand unregistered(rots::combat::combat_command::sleep);
    char_data character {};
    character.in_room = NOWHERE;
    character.specials.position = POSITION_STANDING;

    rots::combat::issue_command(
        rots::combat::combat_command::sleep, &character, mutable_arg(""), nullptr, 0, 0);

    EXPECT_EQ(GET_POS(&character), POSITION_STANDING)
        << "Expected an unregistered sleep cell to leave the character's position untouched.";
}

// do_mental (clerics.cpp) -- DISCRIMINATOR: the PEACEROOM guard is the very
// first statement in do_mental's body (byte-for-byte the same shape as
// do_hit's own first guard above -- IS_SET(world[ch->in_room].room_flags,
// PEACEROOM) -- verified by reading clerics.cpp), so it needs the same
// one-room ScopedTestWorld and captured send_to_char() for the same reason.
// Chosen over do_mental's other early guards (no-victim, self-target, etc.)
// because it is reached with the least fixture: setting room_flags =
// PEACEROOM is the only state needed (unlike the no-victim branch, which
// would additionally require room_flags to NOT have PEACEROOM set, per
// do_hit's own test above) -- do_mental never gets far enough to touch
// ch->specials.fighting/big_brother/etc. This is the highest-value
// cell in the table: it is fight.cpp's ONLY combat_hooks.h target invoked
// unconditionally every combat pulse once fight.cpp's per-tick
// mental-combat auto-attack in the round loop reaches a fighting
// mental/shadow combatant, so this seam being correct for it matters more
// than for any of the other 24 cells.

TEST(CombatHooksDispatch, IssueCommandReachesTheRealDoMentalWhenRegistered)
{
    ScopedTestWorld test_world(1);
    test_world.room().room_flags = PEACEROOM;
    ScopedCapturingOutputSink capture;
    char_data character {};
    character.in_room = 0;

    rots::combat::issue_command(
        rots::combat::combat_command::mental, &character, mutable_arg(""), nullptr, 0, 0);

    EXPECT_EQ(ScopedCapturingOutputSink::last_message,
        "A peaceful feeling overwhelms you, and you cannot bring yourself to attack.\n\r")
        << "Expected the real do_mental body's PEACEROOM guard to send its literal message.";
}

TEST(CombatHooksDispatch, IssueCommandDefaultsToANoOpWhenMentalIsUnregistered)
{
    ScopedTestWorld test_world(1);
    test_world.room().room_flags = PEACEROOM;
    ScopedUnregisteredCombatCommand unregistered(rots::combat::combat_command::mental);
    ScopedCapturingOutputSink capture;
    char_data character {};
    character.in_room = 0;

    rots::combat::issue_command(
        rots::combat::combat_command::mental, &character, mutable_arg(""), nullptr, 0, 0);

    EXPECT_TRUE(ScopedCapturingOutputSink::last_message.empty())
        << "Expected an unregistered mental cell to leave send_to_char uncalled -- the real "
           "do_mental body never ran.";
}

// do_stat (act_info.cpp) -- DISCRIMINATOR: the GET_LEVEL(ch) < 6 guard is the
// very first statement in do_stat's real body and returns before touching
// wtl/world[]/anything else -- message-only, the same "cheapest possible"
// shape as do_say's/do_gen_com's guards above. A default-initialized
// char_data{} already has player.level == 0 (< 6). spec-pair wave Task 2
// (sp-task-2-brief.md; sp-census.md section 4): SPECIAL(resetter)'s
// do_stat(ch, ...) call (spec_pro.cpp) is this cell's first real
// issue_command() caller anywhere in the tree, the recurring "newly
// exercised, previously registered-but-uncalled" gap this wave's own census
// flagged for the door/stat/tell cells.

TEST(CombatHooksDispatch, IssueCommandReachesTheRealDoStatWhenRegistered)
{
    ScopedCapturingOutputSink capture;
    char_data character {};
    character.player.level = 0;

    rots::combat::issue_command(
        rots::combat::combat_command::stat, &character, mutable_arg(""), nullptr, 0, 0);

    EXPECT_EQ(ScopedCapturingOutputSink::last_message,
        "Sorry, you are too young to know your stats.\n\r")
        << "Expected the real do_stat body's low-level guard to send its literal message.";
}

TEST(CombatHooksDispatch, IssueCommandDefaultsToANoOpWhenStatIsUnregistered)
{
    ScopedUnregisteredCombatCommand unregistered(rots::combat::combat_command::stat);
    ScopedCapturingOutputSink capture;
    char_data character {};
    character.player.level = 0;

    rots::combat::issue_command(
        rots::combat::combat_command::stat, &character, mutable_arg(""), nullptr, 0, 0);

    EXPECT_TRUE(ScopedCapturingOutputSink::last_message.empty())
        << "Expected an unregistered stat cell to leave send_to_char uncalled -- the real "
           "do_stat body never ran.";
}

// do_tell (act_comm.cpp) -- DISCRIMINATOR: the IS_NPC(ch) && MOB_FLAGGED(ch,
// MOB_PET) && !MOB_FLAGGED(ch, MOB_ORC_FRIEND) guard is the very first
// statement in do_tell's real body -- message-only, no world[]/wtl touch, the
// same "cheapest possible" shape as do_say's/do_gen_com's guards above.
// spec-pair wave Task 2: SPECIAL(resetter)'s two do_tell(...) calls
// (spec_pro.cpp) are this cell's first real issue_command() caller anywhere
// in the tree.

TEST(CombatHooksDispatch, IssueCommandReachesTheRealDoTellWhenRegistered)
{
    ScopedCapturingOutputSink capture;
    char_data character {};
    character.specials2.act = MOB_ISNPC | MOB_PET;

    rots::combat::issue_command(
        rots::combat::combat_command::tell, &character, mutable_arg(""), nullptr, 0, 0);

    EXPECT_EQ(ScopedCapturingOutputSink::last_message, "Sorry, tamed mobiles can't do that.\n\r")
        << "Expected the real do_tell body's tamed-pet guard to send its literal message.";
}

TEST(CombatHooksDispatch, IssueCommandDefaultsToANoOpWhenTellIsUnregistered)
{
    ScopedUnregisteredCombatCommand unregistered(rots::combat::combat_command::tell);
    ScopedCapturingOutputSink capture;
    char_data character {};
    character.specials2.act = MOB_ISNPC | MOB_PET;

    rots::combat::issue_command(
        rots::combat::combat_command::tell, &character, mutable_arg(""), nullptr, 0, 0);

    EXPECT_TRUE(ScopedCapturingOutputSink::last_message.empty())
        << "Expected an unregistered tell cell to leave send_to_char uncalled -- the real "
           "do_tell body never ran.";
}

// do_lock/do_close/do_open/do_unlock (act_move.cpp) -- DISCRIMINATOR: all
// four share the identical IS_SHADOW(ch) guard as their very first statement
// (verified by reading act_move.cpp:978/1087/1222/1290), each sending the
// identical literal "You are too insubstantial to do that.\n\r" -- the
// cheapest deterministic path through all four bodies, message-only, no
// world[] touch (unlike do_move's own dir_option[] read above). IS_SHADOW
// expands to (IS_NPC(ch) && MOB_FLAGGED(ch, MOB_SHADOW)) for an NPC, so
// specials2.act needs both MOB_ISNPC and MOB_SHADOW set. spec-pair wave Task
// 2: SPECIAL(gatekeeper)'s/SPECIAL(gatekeeper2)'s/SPECIAL(gatekeeper_no_
// knock)'s/SPECIAL(ar_tarthalon)'s/SPECIAL(vampire_killer)'s do_lock/
// do_close/do_open/do_unlock(...) calls (spec_pro.cpp) are each of these
// four cells' first real issue_command() caller anywhere in the tree.

TEST(CombatHooksDispatch, IssueCommandReachesTheRealDoLockWhenRegistered)
{
    ScopedCapturingOutputSink capture;
    char_data character {};
    character.specials2.act = MOB_ISNPC | MOB_SHADOW;

    rots::combat::issue_command(
        rots::combat::combat_command::lock, &character, mutable_arg(""), nullptr, 0, 0);

    EXPECT_EQ(ScopedCapturingOutputSink::last_message, "You are too insubstantial to do that.\n\r")
        << "Expected the real do_lock body's shadow-form guard to send its literal message.";
}

TEST(CombatHooksDispatch, IssueCommandDefaultsToANoOpWhenLockIsUnregistered)
{
    ScopedUnregisteredCombatCommand unregistered(rots::combat::combat_command::lock);
    ScopedCapturingOutputSink capture;
    char_data character {};
    character.specials2.act = MOB_ISNPC | MOB_SHADOW;

    rots::combat::issue_command(
        rots::combat::combat_command::lock, &character, mutable_arg(""), nullptr, 0, 0);

    EXPECT_TRUE(ScopedCapturingOutputSink::last_message.empty())
        << "Expected an unregistered lock cell to leave send_to_char uncalled -- the real "
           "do_lock body never ran.";
}

TEST(CombatHooksDispatch, IssueCommandReachesTheRealDoCloseWhenRegistered)
{
    ScopedCapturingOutputSink capture;
    char_data character {};
    character.specials2.act = MOB_ISNPC | MOB_SHADOW;

    rots::combat::issue_command(
        rots::combat::combat_command::close, &character, mutable_arg(""), nullptr, 0, 0);

    EXPECT_EQ(ScopedCapturingOutputSink::last_message, "You are too insubstantial to do that.\n\r")
        << "Expected the real do_close body's shadow-form guard to send its literal message.";
}

TEST(CombatHooksDispatch, IssueCommandDefaultsToANoOpWhenCloseIsUnregistered)
{
    ScopedUnregisteredCombatCommand unregistered(rots::combat::combat_command::close);
    ScopedCapturingOutputSink capture;
    char_data character {};
    character.specials2.act = MOB_ISNPC | MOB_SHADOW;

    rots::combat::issue_command(
        rots::combat::combat_command::close, &character, mutable_arg(""), nullptr, 0, 0);

    EXPECT_TRUE(ScopedCapturingOutputSink::last_message.empty())
        << "Expected an unregistered close cell to leave send_to_char uncalled -- the real "
           "do_close body never ran.";
}

TEST(CombatHooksDispatch, IssueCommandReachesTheRealDoOpenWhenRegistered)
{
    ScopedCapturingOutputSink capture;
    char_data character {};
    character.specials2.act = MOB_ISNPC | MOB_SHADOW;

    rots::combat::issue_command(
        rots::combat::combat_command::open, &character, mutable_arg(""), nullptr, 0, 0);

    EXPECT_EQ(ScopedCapturingOutputSink::last_message, "You are too insubstantial to do that.\n\r")
        << "Expected the real do_open body's shadow-form guard to send its literal message.";
}

TEST(CombatHooksDispatch, IssueCommandDefaultsToANoOpWhenOpenIsUnregistered)
{
    ScopedUnregisteredCombatCommand unregistered(rots::combat::combat_command::open);
    ScopedCapturingOutputSink capture;
    char_data character {};
    character.specials2.act = MOB_ISNPC | MOB_SHADOW;

    rots::combat::issue_command(
        rots::combat::combat_command::open, &character, mutable_arg(""), nullptr, 0, 0);

    EXPECT_TRUE(ScopedCapturingOutputSink::last_message.empty())
        << "Expected an unregistered open cell to leave send_to_char uncalled -- the real "
           "do_open body never ran.";
}

TEST(CombatHooksDispatch, IssueCommandReachesTheRealDoUnlockWhenRegistered)
{
    ScopedCapturingOutputSink capture;
    char_data character {};
    character.specials2.act = MOB_ISNPC | MOB_SHADOW;

    rots::combat::issue_command(
        rots::combat::combat_command::unlock, &character, mutable_arg(""), nullptr, 0, 0);

    EXPECT_EQ(ScopedCapturingOutputSink::last_message, "You are too insubstantial to do that.\n\r")
        << "Expected the real do_unlock body's shadow-form guard to send its literal message.";
}

TEST(CombatHooksDispatch, IssueCommandDefaultsToANoOpWhenUnlockIsUnregistered)
{
    ScopedUnregisteredCombatCommand unregistered(rots::combat::combat_command::unlock);
    ScopedCapturingOutputSink capture;
    char_data character {};
    character.specials2.act = MOB_ISNPC | MOB_SHADOW;

    rots::combat::issue_command(
        rots::combat::combat_command::unlock, &character, mutable_arg(""), nullptr, 0, 0);

    EXPECT_TRUE(ScopedCapturingOutputSink::last_message.empty())
        << "Expected an unregistered unlock cell to leave send_to_char uncalled -- the real "
           "do_unlock body never ran.";
}

// special() (interpre.h:99; combat-pilot wave Task 2, brief
// .superpowers/sdd/pilot-task-2-brief.md, Step 3) -- a registered-hook seam,
// NOT a 26th combat_command cell (see combat_hooks.h's special_fn comment
// for why: special()'s int-returning, 6-parameter shape is categorically
// different from the ACMD table above -- pilot-census.md section 3.1).
// DISCRIMINATOR: a recording stub captures every argument call_special()
// forwards, proving the dispatch wrapper's parameter list matches
// interpre.h:99's real signature exactly (including the in_room default).
// This seam's own registrar (register_combat_command_dispatch(),
// interpre.cpp) registers the REAL special() function -- ScopedSpecialHandler
// below swaps in a recording stub (or nullptr) for one test and restores the
// real registration afterward via that same registrar, mirroring
// ScopedUnregisteredCombatCommand's restore-via-real-registrar shape above.
namespace {

struct RecordedSpecialCall {
    char_data* ch = nullptr;
    int cmd = 0;
    char* arg = nullptr;
    int callflag = 0;
    waiting_type* wtl = nullptr;
    int in_room = 0;
    bool called = false;
};

RecordedSpecialCall g_recorded_special_call;

int recording_special_stub(
    char_data* ch, int cmd, char* arg, int callflag, waiting_type* wtl, int in_room)
{
    g_recorded_special_call = RecordedSpecialCall { ch, cmd, arg, callflag, wtl, in_room, true };
    return 42;
}

// Swaps combat_hooks.h's single special-handler cell (distinct from the
// combat_command enum-indexed table above), then restores the REAL
// special() registration via register_combat_command_dispatch() on
// destruction -- there is no per-hook "restore just this one" entry point
// for this seam either, same rationale as ScopedUnregisteredCombatCommand.
class ScopedSpecialHandler {
public:
    explicit ScopedSpecialHandler(rots::combat::special_fn handler)
    {
        rots::combat::set_special_handler(handler);
    }

    ~ScopedSpecialHandler() { register_combat_command_dispatch(); }

    ScopedSpecialHandler(const ScopedSpecialHandler&) = delete;
    ScopedSpecialHandler& operator=(const ScopedSpecialHandler&) = delete;
};

} // namespace

TEST(CombatHooksSpecial, CallSpecialReachesARegisteredStubWithAllArgsIntact)
{
    g_recorded_special_call = RecordedSpecialCall {};
    ScopedSpecialHandler scoped(recording_special_stub);
    char_data character {};
    char argument_text[] = "test-arg";
    waiting_type wtl {};

    const int result = rots::combat::call_special(&character, 7, argument_text, SPECIAL_TARGET, &wtl, 42);

    EXPECT_TRUE(g_recorded_special_call.called)
        << "Expected the registered stub to have been reached.";
    EXPECT_EQ(g_recorded_special_call.ch, &character);
    EXPECT_EQ(g_recorded_special_call.cmd, 7);
    EXPECT_EQ(g_recorded_special_call.arg, argument_text);
    EXPECT_EQ(g_recorded_special_call.callflag, SPECIAL_TARGET);
    EXPECT_EQ(g_recorded_special_call.wtl, &wtl);
    EXPECT_EQ(g_recorded_special_call.in_room, 42)
        << "Expected the explicit in_room argument to reach the stub unchanged.";
    EXPECT_EQ(result, 42) << "Expected call_special() to forward the stub's return value.";
}

TEST(CombatHooksSpecial, CallSpecialOmittedInRoomArgumentDefaultsToNowhereLikeInterpreH99)
{
    g_recorded_special_call = RecordedSpecialCall {};
    ScopedSpecialHandler scoped(recording_special_stub);
    char_data character {};
    waiting_type wtl {};

    rots::combat::call_special(&character, 0, mutable_arg(""), 0, &wtl);

    EXPECT_EQ(g_recorded_special_call.in_room, NOWHERE)
        << "Expected call_special()'s own in_room default to mirror interpre.h:99's "
           "`int in_room = NOWHERE` default exactly.";
}

TEST(CombatHooksSpecial, CallSpecialDefaultsToLoggedZeroWhenUnregistered)
{
    ScopedSpecialHandler unregistered(nullptr);
    char_data character {};
    waiting_type wtl {};

    const int result = rots::combat::call_special(&character, 0, mutable_arg(""), 0, &wtl);

    EXPECT_EQ(result, 0)
        << "Expected an unregistered special handler to return the tripwire default 0 -- "
           "\"no spec-proc consumed the event\".";
}

// Task 4b hooks (combat-pilot wave): extract_char's discriminator suite
// RE-HOMED to entity_lifecycle_tests.cpp (l4-seed wave, Task 1;
// l4-task-1-brief.md Step 2a; l4-census.md section 3.4) -- extract_char()
// itself moved from combat_hooks.h/.cpp to entity_hooks.h/entity_lifecycle.cpp,
// so its CombatHooksExtractChar test suite (now ExtractCharHook) moved with
// it, updated to rots::entity::.

// -----------------------------------------------------------------------
// Task 4b hooks (combat-pilot wave): gain_exp/gain_exp_regardless/
// remove_fame_war_bonuses (limits.cpp; pilot-census.md section 7.4/7.5/7.6).
// Same recording-stub discriminator shape as this file's CombatHooksSpecial
// suite above (extract_char's own sibling suite moved to
// entity_lifecycle_tests.cpp -- see this file's comment above).
// -----------------------------------------------------------------------

namespace {

struct RecordedGainExpCall {
    char_data* ch = nullptr;
    int gain = 0;
    bool called = false;
};

RecordedGainExpCall g_recorded_gain_exp_call;

void recording_gain_exp_stub(char_data* ch, int gain)
{
    g_recorded_gain_exp_call = RecordedGainExpCall { ch, gain, true };
}

class ScopedGainExpHook {
public:
    explicit ScopedGainExpHook(rots::combat::gain_exp_fn hook)
    {
        rots::combat::set_gain_exp_hook(hook);
    }

    ~ScopedGainExpHook() { register_gain_exp_hook(); }

    ScopedGainExpHook(const ScopedGainExpHook&) = delete;
    ScopedGainExpHook& operator=(const ScopedGainExpHook&) = delete;
};

RecordedGainExpCall g_recorded_gain_exp_regardless_call;

void recording_gain_exp_regardless_stub(char_data* ch, int gain)
{
    g_recorded_gain_exp_regardless_call = RecordedGainExpCall { ch, gain, true };
}

class ScopedGainExpRegardlessHook {
public:
    explicit ScopedGainExpRegardlessHook(rots::combat::gain_exp_regardless_fn hook)
    {
        rots::combat::set_gain_exp_regardless_hook(hook);
    }

    ~ScopedGainExpRegardlessHook() { register_gain_exp_regardless_hook(); }

    ScopedGainExpRegardlessHook(const ScopedGainExpRegardlessHook&) = delete;
    ScopedGainExpRegardlessHook& operator=(const ScopedGainExpRegardlessHook&) = delete;
};

struct RecordedRemoveFameWarBonusesCall {
    char_data* ch = nullptr;
    affected_type* pkaff = nullptr;
    bool called = false;
};

RecordedRemoveFameWarBonusesCall g_recorded_remove_fame_war_bonuses_call;

void recording_remove_fame_war_bonuses_stub(char_data* ch, affected_type* pkaff)
{
    g_recorded_remove_fame_war_bonuses_call = RecordedRemoveFameWarBonusesCall { ch, pkaff, true };
}

class ScopedRemoveFameWarBonusesHook {
public:
    explicit ScopedRemoveFameWarBonusesHook(rots::combat::remove_fame_war_bonuses_fn hook)
    {
        rots::combat::set_remove_fame_war_bonuses_hook(hook);
    }

    ~ScopedRemoveFameWarBonusesHook() { register_remove_fame_war_bonuses_hook(); }

    ScopedRemoveFameWarBonusesHook(const ScopedRemoveFameWarBonusesHook&) = delete;
    ScopedRemoveFameWarBonusesHook& operator=(const ScopedRemoveFameWarBonusesHook&) = delete;
};

} // namespace

TEST(CombatHooksGainExp, DispatchReachesARegisteredStubWithArgsIntact)
{
    g_recorded_gain_exp_call = RecordedGainExpCall {};
    ScopedGainExpHook scoped(recording_gain_exp_stub);
    char_data character {};

    rots::combat::gain_exp(&character, 500);

    EXPECT_TRUE(g_recorded_gain_exp_call.called)
        << "Expected the registered stub to have been reached.";
    EXPECT_EQ(g_recorded_gain_exp_call.ch, &character);
    EXPECT_EQ(g_recorded_gain_exp_call.gain, 500);
}

TEST(CombatHooksGainExp, DispatchDefaultsToANoOpWhenUnregistered)
{
    g_recorded_gain_exp_call = RecordedGainExpCall {};
    ScopedGainExpHook unregistered(nullptr);
    char_data character {};

    rots::combat::gain_exp(&character, 500);

    EXPECT_FALSE(g_recorded_gain_exp_call.called)
        << "Expected an unregistered gain_exp hook to leave the (unrelated) stub's own "
           "recording flag untouched -- the real forwarder never ran, and the tripwire default "
           "is a logged no-op, not a call to any stub.";
}

TEST(CombatHooksGainExpRegardless, DispatchReachesARegisteredStubWithArgsIntact)
{
    g_recorded_gain_exp_regardless_call = RecordedGainExpCall {};
    ScopedGainExpRegardlessHook scoped(recording_gain_exp_regardless_stub);
    char_data character {};

    rots::combat::gain_exp_regardless(&character, -750);

    EXPECT_TRUE(g_recorded_gain_exp_regardless_call.called)
        << "Expected the registered stub to have been reached.";
    EXPECT_EQ(g_recorded_gain_exp_regardless_call.ch, &character);
    EXPECT_EQ(g_recorded_gain_exp_regardless_call.gain, -750);
}

TEST(CombatHooksGainExpRegardless, DispatchDefaultsToANoOpWhenUnregistered)
{
    g_recorded_gain_exp_regardless_call = RecordedGainExpCall {};
    ScopedGainExpRegardlessHook unregistered(nullptr);
    char_data character {};

    rots::combat::gain_exp_regardless(&character, -750);

    EXPECT_FALSE(g_recorded_gain_exp_regardless_call.called)
        << "Expected an unregistered gain_exp_regardless hook to leave the (unrelated) stub's "
           "own recording flag untouched -- the real forwarder never ran, and the tripwire "
           "default is a logged no-op, not a call to any stub.";
}

TEST(CombatHooksRemoveFameWarBonuses, DispatchReachesARegisteredStubWithArgsIntact)
{
    g_recorded_remove_fame_war_bonuses_call = RecordedRemoveFameWarBonusesCall {};
    ScopedRemoveFameWarBonusesHook scoped(recording_remove_fame_war_bonuses_stub);
    char_data character {};
    affected_type pkaff {};

    rots::combat::remove_fame_war_bonuses(&character, &pkaff);

    EXPECT_TRUE(g_recorded_remove_fame_war_bonuses_call.called)
        << "Expected the registered stub to have been reached.";
    EXPECT_EQ(g_recorded_remove_fame_war_bonuses_call.ch, &character);
    EXPECT_EQ(g_recorded_remove_fame_war_bonuses_call.pkaff, &pkaff);
}

TEST(CombatHooksRemoveFameWarBonuses, DispatchDefaultsToANoOpWhenUnregistered)
{
    g_recorded_remove_fame_war_bonuses_call = RecordedRemoveFameWarBonusesCall {};
    ScopedRemoveFameWarBonusesHook unregistered(nullptr);
    char_data character {};
    affected_type pkaff {};

    rots::combat::remove_fame_war_bonuses(&character, &pkaff);

    EXPECT_FALSE(g_recorded_remove_fame_war_bonuses_call.called)
        << "Expected an unregistered remove_fame_war_bonuses hook to leave the (unrelated) "
           "stub's own recording flag untouched -- the real forwarder never ran, and the "
           "tripwire default is a logged no-op, not a call to any stub.";
}

// -----------------------------------------------------------------------
// Task 4b hooks (combat-pilot wave): app-other trio -- crash_crashsave
// (objsave.cpp), call_trigger (script.cpp), pkill_create (pkill.cpp);
// pilot-census.md section 3.7. Same recording-stub discriminator shape as
// the suites above, EXCEPT call_trigger's unregistered-default test, which
// is the brief's MANDATORY proof that an unregistered hook returns TRUE
// (not FALSE) -- a FALSE default would silently veto/immortalize the
// caller's event (fight.cpp:1003/1843's ON_DIE/ON_DAMAGE call sites both
// treat a FALSE return as "a script vetoed this event").
// -----------------------------------------------------------------------

namespace {

struct RecordedCrashCrashsaveCall {
    char_data* ch = nullptr;
    int rent_code = 0;
    bool called = false;
};

RecordedCrashCrashsaveCall g_recorded_crash_crashsave_call;

void recording_crash_crashsave_stub(char_data* ch, int rent_code)
{
    g_recorded_crash_crashsave_call = RecordedCrashCrashsaveCall { ch, rent_code, true };
}

class ScopedCrashCrashsaveHook {
public:
    explicit ScopedCrashCrashsaveHook(rots::combat::crash_crashsave_fn hook)
    {
        rots::combat::set_crash_crashsave_hook(hook);
    }

    ~ScopedCrashCrashsaveHook() { register_crash_crashsave_hook(); }

    ScopedCrashCrashsaveHook(const ScopedCrashCrashsaveHook&) = delete;
    ScopedCrashCrashsaveHook& operator=(const ScopedCrashCrashsaveHook&) = delete;
};

struct RecordedCallTriggerCall {
    int trigger_type = 0;
    void* subject = nullptr;
    void* subject2 = nullptr;
    void* subject3 = nullptr;
    bool called = false;
};

RecordedCallTriggerCall g_recorded_call_trigger_call;

int recording_call_trigger_stub(int trigger_type, void* subject, void* subject2, void* subject3)
{
    g_recorded_call_trigger_call
        = RecordedCallTriggerCall { trigger_type, subject, subject2, subject3, true };
    return 0; // FALSE -- distinguishable from the tripwire's own TRUE default.
}

class ScopedCallTriggerHook {
public:
    explicit ScopedCallTriggerHook(rots::combat::call_trigger_fn hook)
    {
        rots::combat::set_call_trigger_hook(hook);
    }

    ~ScopedCallTriggerHook() { register_call_trigger_hook(); }

    ScopedCallTriggerHook(const ScopedCallTriggerHook&) = delete;
    ScopedCallTriggerHook& operator=(const ScopedCallTriggerHook&) = delete;
};

struct RecordedPkillCreateCall {
    char_data* victim = nullptr;
    bool called = false;
};

RecordedPkillCreateCall g_recorded_pkill_create_call;

void recording_pkill_create_stub(char_data* victim)
{
    g_recorded_pkill_create_call = RecordedPkillCreateCall { victim, true };
}

class ScopedPkillCreateHook {
public:
    explicit ScopedPkillCreateHook(rots::combat::pkill_create_fn hook)
    {
        rots::combat::set_pkill_create_hook(hook);
    }

    ~ScopedPkillCreateHook() { register_pkill_create_hook(); }

    ScopedPkillCreateHook(const ScopedPkillCreateHook&) = delete;
    ScopedPkillCreateHook& operator=(const ScopedPkillCreateHook&) = delete;
};

} // namespace

TEST(CombatHooksCrashCrashsave, DispatchReachesARegisteredStubWithArgsIntact)
{
    g_recorded_crash_crashsave_call = RecordedCrashCrashsaveCall {};
    ScopedCrashCrashsaveHook scoped(recording_crash_crashsave_stub);
    char_data character {};

    rots::combat::crash_crashsave(&character, 99);

    EXPECT_TRUE(g_recorded_crash_crashsave_call.called)
        << "Expected the registered stub to have been reached.";
    EXPECT_EQ(g_recorded_crash_crashsave_call.ch, &character);
    EXPECT_EQ(g_recorded_crash_crashsave_call.rent_code, 99);
}

// DISCRIMINATOR: an omitted rent_code argument reaches the stub as
// handler.h:254's own RENT_CRASH default, mirroring
// CallSpecialOmittedInRoomArgumentDefaultsToNowhereLikeInterpreH99's
// omitted-argument proof above.

TEST(CombatHooksCrashCrashsave, OmittedRentCodeDefaultsToRentCrashLikeHandlerH254)
{
    g_recorded_crash_crashsave_call = RecordedCrashCrashsaveCall {};
    ScopedCrashCrashsaveHook scoped(recording_crash_crashsave_stub);
    char_data character {};

    rots::combat::crash_crashsave(&character);

    EXPECT_TRUE(g_recorded_crash_crashsave_call.called);
    EXPECT_EQ(g_recorded_crash_crashsave_call.rent_code, RENT_CRASH);
}

TEST(CombatHooksCrashCrashsave, DispatchDefaultsToANoOpWhenUnregistered)
{
    g_recorded_crash_crashsave_call = RecordedCrashCrashsaveCall {};
    ScopedCrashCrashsaveHook unregistered(nullptr);
    char_data character {};

    rots::combat::crash_crashsave(&character, 99);

    EXPECT_FALSE(g_recorded_crash_crashsave_call.called)
        << "Expected an unregistered crash_crashsave hook to leave the (unrelated) stub's own "
           "recording flag untouched -- the real forwarder never ran, and the tripwire default "
           "is a logged no-op, not a call to any stub.";
}

TEST(CombatHooksCallTrigger, DispatchReachesARegisteredStubWithArgsIntact)
{
    g_recorded_call_trigger_call = RecordedCallTriggerCall {};
    ScopedCallTriggerHook scoped(recording_call_trigger_stub);
    char_data subject {};
    char_data subject2 {};
    obj_data subject3 {};

    const int result = rots::combat::call_trigger(ON_DIE, &subject, &subject2, &subject3);

    EXPECT_TRUE(g_recorded_call_trigger_call.called)
        << "Expected the registered stub to have been reached.";
    EXPECT_EQ(g_recorded_call_trigger_call.trigger_type, ON_DIE);
    EXPECT_EQ(g_recorded_call_trigger_call.subject, &subject);
    EXPECT_EQ(g_recorded_call_trigger_call.subject2, &subject2);
    EXPECT_EQ(g_recorded_call_trigger_call.subject3, &subject3);
    EXPECT_EQ(result, 0) << "Expected call_trigger() to forward the stub's own return value.";
}

// MANDATORY (pilot-task-4b-brief.md): an unregistered call_trigger hook MUST
// return TRUE, not FALSE -- fight.cpp:1003's `if (call_trigger(ON_DIE, ...)
// == FALSE)` and fight.cpp:1843's `if (!call_trigger(ON_DAMAGE, ...))` both
// treat FALSE as "a script vetoed this event"; a FALSE default here would
// silently immortalize/veto every future caller once Task 5 converts those
// call sites, instead of the documented "no script attached, proceed
// normally" semantics.

TEST(CombatHooksCallTrigger, DispatchDefaultsToLoggedTrueWhenUnregistered)
{
    ScopedCallTriggerHook unregistered(nullptr);
    char_data subject {};

    const int result = rots::combat::call_trigger(ON_DIE, &subject, nullptr, nullptr);

    EXPECT_EQ(result, 1)
        << "Expected an unregistered call_trigger hook to default to TRUE (1) -- \"no script "
           "attached / proceed normally\" -- NOT FALSE, which fight.cpp's real ON_DIE/ON_DAMAGE "
           "call sites treat as a script veto that would silently immortalize the caller.";
}

TEST(CombatHooksPkillCreate, DispatchReachesARegisteredStubWithArgsIntact)
{
    g_recorded_pkill_create_call = RecordedPkillCreateCall {};
    ScopedPkillCreateHook scoped(recording_pkill_create_stub);
    char_data victim {};

    rots::combat::pkill_create(&victim);

    EXPECT_TRUE(g_recorded_pkill_create_call.called)
        << "Expected the registered stub to have been reached.";
    EXPECT_EQ(g_recorded_pkill_create_call.victim, &victim);
}

TEST(CombatHooksPkillCreate, DispatchDefaultsToANoOpWhenUnregistered)
{
    g_recorded_pkill_create_call = RecordedPkillCreateCall {};
    ScopedPkillCreateHook unregistered(nullptr);
    char_data victim {};

    rots::combat::pkill_create(&victim);

    EXPECT_FALSE(g_recorded_pkill_create_call.called)
        << "Expected an unregistered pkill_create hook to leave the (unrelated) stub's own "
           "recording flag untouched -- the real forwarder never ran, and the tripwire default "
           "is a logged no-op, not a call to any stub.";
}

// ---------------------------------------------------------------------------
// Behavior-wave Task 1 hooks (one_mobile_activity + Crash_idlesave/
// Crash_extract_objs sibling pair). CONSUMER-FREE this task -- no
// mobact.cpp/limits.cpp call site converts yet -- same "recording stub
// proves forward-then-tripwire" discriminator shape as the App-other-trio
// suite above.
// ---------------------------------------------------------------------------

// mobact.cpp has no dedicated header (see combat_hooks.h's own comment);
// forward-declared locally here, mirroring comm.cpp's/gtest_main.cpp's own
// local declaration of this same registrar.
void register_one_mobile_activity_hook();

namespace {

struct RecordedOneMobileActivityCall {
    char_data* ch = nullptr;
    bool called = false;
};

RecordedOneMobileActivityCall g_recorded_one_mobile_activity_call;

void recording_one_mobile_activity_stub(char_data* ch)
{
    g_recorded_one_mobile_activity_call = RecordedOneMobileActivityCall { ch, true };
}

class ScopedOneMobileActivityHook {
public:
    explicit ScopedOneMobileActivityHook(rots::combat::mobile_activity_fn hook)
    {
        rots::combat::set_one_mobile_activity_hook(hook);
    }

    ~ScopedOneMobileActivityHook() { register_one_mobile_activity_hook(); }

    ScopedOneMobileActivityHook(const ScopedOneMobileActivityHook&) = delete;
    ScopedOneMobileActivityHook& operator=(const ScopedOneMobileActivityHook&) = delete;
};

struct RecordedCrashIdlesaveCall {
    char_data* ch = nullptr;
    bool called = false;
};

RecordedCrashIdlesaveCall g_recorded_crash_idlesave_call;

void recording_crash_idlesave_stub(char_data* ch)
{
    g_recorded_crash_idlesave_call = RecordedCrashIdlesaveCall { ch, true };
}

class ScopedCrashIdlesaveHook {
public:
    explicit ScopedCrashIdlesaveHook(rots::combat::crash_idlesave_fn hook)
    {
        rots::combat::set_crash_idlesave_hook(hook);
    }

    ~ScopedCrashIdlesaveHook() { register_crash_idlesave_hook(); }

    ScopedCrashIdlesaveHook(const ScopedCrashIdlesaveHook&) = delete;
    ScopedCrashIdlesaveHook& operator=(const ScopedCrashIdlesaveHook&) = delete;
};

struct RecordedCrashExtractObjsCall {
    obj_data* obj = nullptr;
    bool called = false;
};

RecordedCrashExtractObjsCall g_recorded_crash_extract_objs_call;

void recording_crash_extract_objs_stub(obj_data* obj)
{
    g_recorded_crash_extract_objs_call = RecordedCrashExtractObjsCall { obj, true };
}

class ScopedCrashExtractObjsHook {
public:
    explicit ScopedCrashExtractObjsHook(rots::combat::crash_extract_objs_fn hook)
    {
        rots::combat::set_crash_extract_objs_hook(hook);
    }

    ~ScopedCrashExtractObjsHook() { register_crash_extract_objs_hook(); }

    ScopedCrashExtractObjsHook(const ScopedCrashExtractObjsHook&) = delete;
    ScopedCrashExtractObjsHook& operator=(const ScopedCrashExtractObjsHook&) = delete;
};

} // namespace

TEST(CombatHooksOneMobileActivity, DispatchReachesARegisteredStubWithArgIntact)
{
    g_recorded_one_mobile_activity_call = RecordedOneMobileActivityCall {};
    ScopedOneMobileActivityHook scoped(recording_one_mobile_activity_stub);
    char_data character {};

    rots::combat::dispatch_one_mobile_activity(&character);

    EXPECT_TRUE(g_recorded_one_mobile_activity_call.called)
        << "Expected the registered stub to have been reached.";
    EXPECT_EQ(g_recorded_one_mobile_activity_call.ch, &character);
}

TEST(CombatHooksOneMobileActivity, DispatchDefaultsToANoOpWhenUnregistered)
{
    g_recorded_one_mobile_activity_call = RecordedOneMobileActivityCall {};
    ScopedOneMobileActivityHook unregistered(nullptr);
    char_data character {};

    rots::combat::dispatch_one_mobile_activity(&character);

    EXPECT_FALSE(g_recorded_one_mobile_activity_call.called)
        << "Expected an unregistered one_mobile_activity hook to leave the (unrelated) stub's own "
           "recording flag untouched -- the real forwarder never ran, and the tripwire default "
           "is a loud logged no-op, not a call to any stub.";
}

TEST(CombatHooksCrashIdlesave, DispatchReachesARegisteredStubWithArgIntact)
{
    g_recorded_crash_idlesave_call = RecordedCrashIdlesaveCall {};
    ScopedCrashIdlesaveHook scoped(recording_crash_idlesave_stub);
    char_data character {};

    rots::combat::crash_idlesave(&character);

    EXPECT_TRUE(g_recorded_crash_idlesave_call.called)
        << "Expected the registered stub to have been reached.";
    EXPECT_EQ(g_recorded_crash_idlesave_call.ch, &character);
}

TEST(CombatHooksCrashIdlesave, DispatchDefaultsToANoOpWhenUnregistered)
{
    g_recorded_crash_idlesave_call = RecordedCrashIdlesaveCall {};
    ScopedCrashIdlesaveHook unregistered(nullptr);
    char_data character {};

    rots::combat::crash_idlesave(&character);

    EXPECT_FALSE(g_recorded_crash_idlesave_call.called)
        << "Expected an unregistered crash_idlesave hook to leave the (unrelated) stub's own "
           "recording flag untouched -- the real forwarder never ran, and the tripwire default "
           "is a logged no-op, not a call to any stub.";
}

TEST(CombatHooksCrashExtractObjs, DispatchReachesARegisteredStubWithArgIntact)
{
    g_recorded_crash_extract_objs_call = RecordedCrashExtractObjsCall {};
    ScopedCrashExtractObjsHook scoped(recording_crash_extract_objs_stub);
    obj_data object {};

    rots::combat::crash_extract_objs(&object);

    EXPECT_TRUE(g_recorded_crash_extract_objs_call.called)
        << "Expected the registered stub to have been reached.";
    EXPECT_EQ(g_recorded_crash_extract_objs_call.obj, &object);
}

TEST(CombatHooksCrashExtractObjs, DispatchDefaultsToANoOpWhenUnregistered)
{
    g_recorded_crash_extract_objs_call = RecordedCrashExtractObjsCall {};
    ScopedCrashExtractObjsHook unregistered(nullptr);
    obj_data object {};

    rots::combat::crash_extract_objs(&object);

    EXPECT_FALSE(g_recorded_crash_extract_objs_call.called)
        << "Expected an unregistered crash_extract_objs hook to leave the (unrelated) stub's own "
           "recording flag untouched -- the real forwarder never ran, and the tripwire default "
           "is a logged no-op, not a call to any stub.";
}

// -----------------------------------------------------------------------
// act_move.cpp/act_info.cpp display-and-movement inversion trio
// (spell-family closure wave Task 1; sf-census.md sections 4.2/4.3).
// CONSUMER-FREE this task -- no ranger.cpp/mage.cpp call site converts
// yet -- same "recording stub proves forward-then-tripwire" discriminator
// shape as the pairs above. Unlike those hooks, these three have
// pre-existing bare-name app-tier callers (act_offe.cpp/ranger.cpp/
// act_info.cpp itself) whose real bodies stay UNCHANGED -- gtest_main.cpp
// registers the real forwarders (register_check_simple_move_hook()/
// register_list_char_to_char_hook()/register_do_identify_object_hook())
// for the whole test binary, restored on scope exit here exactly like
// every Scoped*Hook fixture above.
// -----------------------------------------------------------------------

namespace {

struct RecordedCheckSimpleMoveCall {
    char_data* ch = nullptr;
    int cmd = 0;
    int* mv_cost = nullptr;
    int mode = 0;
    bool called = false;
};

RecordedCheckSimpleMoveCall g_recorded_check_simple_move_call;

int recording_check_simple_move_stub(char_data* ch, int cmd, int* mv_cost, int mode)
{
    g_recorded_check_simple_move_call = RecordedCheckSimpleMoveCall { ch, cmd, mv_cost, mode, true };
    return 0;
}

class ScopedCheckSimpleMoveHook {
public:
    explicit ScopedCheckSimpleMoveHook(rots::combat::check_simple_move_fn hook)
    {
        rots::combat::set_check_simple_move_hook(hook);
    }

    ~ScopedCheckSimpleMoveHook() { register_check_simple_move_hook(); }

    ScopedCheckSimpleMoveHook(const ScopedCheckSimpleMoveHook&) = delete;
    ScopedCheckSimpleMoveHook& operator=(const ScopedCheckSimpleMoveHook&) = delete;
};

struct RecordedListCharToCharCall {
    char_data* list = nullptr;
    char_data* ch = nullptr;
    int mode = 0;
    bool called = false;
};

RecordedListCharToCharCall g_recorded_list_char_to_char_call;

void recording_list_char_to_char_stub(char_data* list, char_data* ch, int mode)
{
    g_recorded_list_char_to_char_call = RecordedListCharToCharCall { list, ch, mode, true };
}

class ScopedListCharToCharHook {
public:
    explicit ScopedListCharToCharHook(rots::combat::list_char_to_char_fn hook)
    {
        rots::combat::set_list_char_to_char_hook(hook);
    }

    ~ScopedListCharToCharHook() { register_list_char_to_char_hook(); }

    ScopedListCharToCharHook(const ScopedListCharToCharHook&) = delete;
    ScopedListCharToCharHook& operator=(const ScopedListCharToCharHook&) = delete;
};

struct RecordedDoIdentifyObjectCall {
    char_data* ch = nullptr;
    obj_data* j = nullptr;
    bool called = false;
};

RecordedDoIdentifyObjectCall g_recorded_do_identify_object_call;

void recording_do_identify_object_stub(char_data* ch, obj_data* j)
{
    g_recorded_do_identify_object_call = RecordedDoIdentifyObjectCall { ch, j, true };
}

class ScopedDoIdentifyObjectHook {
public:
    explicit ScopedDoIdentifyObjectHook(rots::combat::do_identify_object_fn hook)
    {
        rots::combat::set_do_identify_object_hook(hook);
    }

    ~ScopedDoIdentifyObjectHook() { register_do_identify_object_hook(); }

    ScopedDoIdentifyObjectHook(const ScopedDoIdentifyObjectHook&) = delete;
    ScopedDoIdentifyObjectHook& operator=(const ScopedDoIdentifyObjectHook&) = delete;
};

} // namespace

TEST(CombatHooksCheckSimpleMove, DispatchReachesARegisteredStubWithArgsIntact)
{
    g_recorded_check_simple_move_call = RecordedCheckSimpleMoveCall {};
    ScopedCheckSimpleMoveHook scoped(recording_check_simple_move_stub);
    char_data character {};
    int mv_cost = 0;

    const int result = rots::combat::check_simple_move(&character, 2, &mv_cost, 5);

    EXPECT_TRUE(g_recorded_check_simple_move_call.called)
        << "Expected the registered stub to have been reached.";
    EXPECT_EQ(g_recorded_check_simple_move_call.ch, &character);
    EXPECT_EQ(g_recorded_check_simple_move_call.cmd, 2);
    EXPECT_EQ(g_recorded_check_simple_move_call.mv_cost, &mv_cost);
    EXPECT_EQ(g_recorded_check_simple_move_call.mode, 5);
    EXPECT_EQ(result, 0) << "Expected check_simple_move() to forward the stub's own return value.";
}

TEST(CombatHooksCheckSimpleMove, DispatchDefaultsToABlockedMoveWhenUnregistered)
{
    g_recorded_check_simple_move_call = RecordedCheckSimpleMoveCall {};
    ScopedCheckSimpleMoveHook unregistered(nullptr);
    char_data character {};
    int mv_cost = 0;

    const int result = rots::combat::check_simple_move(&character, 2, &mv_cost, 5);

    EXPECT_FALSE(g_recorded_check_simple_move_call.called)
        << "Expected an unregistered check_simple_move hook to leave the (unrelated) stub's own "
           "recording flag untouched -- the real forwarder never ran.";
    EXPECT_EQ(result, 1)
        << "Expected an unregistered check_simple_move hook to default to 1 (\"intercepted, move "
           "blocked\"), not a falsely-permissive 0 (\"move succeeds\").";
}

TEST(CombatHooksListCharToChar, DispatchReachesARegisteredStubWithArgsIntact)
{
    g_recorded_list_char_to_char_call = RecordedListCharToCharCall {};
    ScopedListCharToCharHook scoped(recording_list_char_to_char_stub);
    char_data list_head {};
    char_data viewer {};

    rots::combat::list_char_to_char(&list_head, &viewer, 3);

    EXPECT_TRUE(g_recorded_list_char_to_char_call.called)
        << "Expected the registered stub to have been reached.";
    EXPECT_EQ(g_recorded_list_char_to_char_call.list, &list_head);
    EXPECT_EQ(g_recorded_list_char_to_char_call.ch, &viewer);
    EXPECT_EQ(g_recorded_list_char_to_char_call.mode, 3);
}

TEST(CombatHooksListCharToChar, DispatchDefaultsToANoOpWhenUnregistered)
{
    g_recorded_list_char_to_char_call = RecordedListCharToCharCall {};
    ScopedListCharToCharHook unregistered(nullptr);
    char_data list_head {};
    char_data viewer {};

    rots::combat::list_char_to_char(&list_head, &viewer, 3);

    EXPECT_FALSE(g_recorded_list_char_to_char_call.called)
        << "Expected an unregistered list_char_to_char hook to leave the (unrelated) stub's own "
           "recording flag untouched -- the real forwarder never ran, and the tripwire default is "
           "a logged no-op, not a call to any stub.";
}

TEST(CombatHooksDoIdentifyObject, DispatchReachesARegisteredStubWithArgsIntact)
{
    g_recorded_do_identify_object_call = RecordedDoIdentifyObjectCall {};
    ScopedDoIdentifyObjectHook scoped(recording_do_identify_object_stub);
    char_data character {};
    obj_data object {};

    rots::combat::do_identify_object(&character, &object);

    EXPECT_TRUE(g_recorded_do_identify_object_call.called)
        << "Expected the registered stub to have been reached.";
    EXPECT_EQ(g_recorded_do_identify_object_call.ch, &character);
    EXPECT_EQ(g_recorded_do_identify_object_call.j, &object);
}

TEST(CombatHooksDoIdentifyObject, DispatchDefaultsToANoOpWhenUnregistered)
{
    g_recorded_do_identify_object_call = RecordedDoIdentifyObjectCall {};
    ScopedDoIdentifyObjectHook unregistered(nullptr);
    char_data character {};
    obj_data object {};

    rots::combat::do_identify_object(&character, &object);

    EXPECT_FALSE(g_recorded_do_identify_object_call.called)
        << "Expected an unregistered do_identify_object hook to leave the (unrelated) stub's own "
           "recording flag untouched -- the real forwarder never ran, and the tripwire default is "
           "a logged no-op, not a call to any stub.";
}

// -----------------------------------------------------------------------
// Cluster B wave Task 1: three new combat_command cells (action/emote/
// shutdown; cb-task-1-brief.md Step 1; cb-census.md section 5.3).
// CONSUMER-FREE this task -- script.cpp's/shapemob.cpp's own call sites do
// not convert yet (that is a later Cluster B task); these pairs prove the
// registered path reaches the real ACMD body (action/emote) or a stub
// (shutdown, see its own comment below) and the unregistered path stays a
// safe no-op, the same discriminator shape as this file's other cells.
// -----------------------------------------------------------------------

// do_action (act_soci.cpp) -- DISCRIMINATOR: the CMD_SOCIAL/subcmd-0 branch
// with a zeroed social_messg (soc_mess_list swapped to point at a
// stack-local instance, mirroring act_format_tests.cpp's
// ActSoci.DoActionAllowsMissingNoArgumentMessages fixture exactly) bypasses
// find_action()/social_parser() entirely -- act_nr comes straight from
// wtl->subcmd, so this needs no social_list_top/soc_mess_list content at
// all beyond the swapped pointer. With every social_messg field zeroed
// (char_no_arg/others_no_arg both null, min_actor_position 0), do_action's
// only observable output is the unconditional trailing "\n\r".

TEST(CombatHooksDispatch, IssueCommandReachesTheRealDoActionWhenRegistered)
{
    ScopedCapturingOutputSink capture;
    social_messg social_message {};
    social_messg* previous_social_message_list = soc_mess_list;
    soc_mess_list = &social_message;
    char_data character {};
    waiting_type wtl {};
    wtl.cmd = CMD_SOCIAL;
    wtl.subcmd = 0;

    rots::combat::issue_command(
        rots::combat::combat_command::action, &character, mutable_arg(""), &wtl, 0, 0);

    EXPECT_EQ(ScopedCapturingOutputSink::last_message, "\n\r")
        << "Expected the real do_action body's no-victim branch to send its unconditional "
           "trailing message.";

    soc_mess_list = previous_social_message_list;
}

TEST(CombatHooksDispatch, IssueCommandDefaultsToANoOpWhenActionIsUnregistered)
{
    ScopedUnregisteredCombatCommand unregistered(rots::combat::combat_command::action);
    ScopedCapturingOutputSink capture;
    social_messg social_message {};
    social_messg* previous_social_message_list = soc_mess_list;
    soc_mess_list = &social_message;
    char_data character {};
    waiting_type wtl {};
    wtl.cmd = CMD_SOCIAL;
    wtl.subcmd = 0;

    rots::combat::issue_command(
        rots::combat::combat_command::action, &character, mutable_arg(""), &wtl, 0, 0);

    EXPECT_TRUE(ScopedCapturingOutputSink::last_message.empty())
        << "Expected an unregistered action cell to leave send_to_char uncalled -- the real "
           "do_action body never ran.";

    soc_mess_list = previous_social_message_list;
}

// do_emote (act_wiz.cpp) -- DISCRIMINATOR: the empty-argument branch's
// unconditional send_to_char("Yes.. But what?\n\r", ch) -- the cheapest
// deterministic path through do_emote's real body (wtl is null, so the
// TARGET_TEXT substitution never fires; argument is an empty mutable_arg,
// so the leading-space skip loop and the `!*(argument+i)` guard fire
// immediately). Message-only, no world[]/state needed.

TEST(CombatHooksDispatch, IssueCommandReachesTheRealDoEmoteWhenRegistered)
{
    ScopedCapturingOutputSink capture;
    char_data character {};

    rots::combat::issue_command(
        rots::combat::combat_command::emote, &character, mutable_arg(""), nullptr, 0, 0);

    EXPECT_EQ(ScopedCapturingOutputSink::last_message, "Yes.. But what?\n\r")
        << "Expected the real do_emote body's empty-argument branch to send its literal message.";
}

TEST(CombatHooksDispatch, IssueCommandDefaultsToANoOpWhenEmoteIsUnregistered)
{
    ScopedUnregisteredCombatCommand unregistered(rots::combat::combat_command::emote);
    ScopedCapturingOutputSink capture;
    char_data character {};

    rots::combat::issue_command(
        rots::combat::combat_command::emote, &character, mutable_arg(""), nullptr, 0, 0);

    EXPECT_TRUE(ScopedCapturingOutputSink::last_message.empty())
        << "Expected an unregistered emote cell to leave send_to_char uncalled -- the real "
           "do_emote body never ran.";
}

// do_shutdown (act_wiz.cpp) -- DESIGN-RISK (cb-census.md section 5.3):
// do_shutdown's real body force-shuts-down the server, so unlike every
// other cell in this table this pair NEVER invokes the genuine ACMD --
// registering a recording stub (matching acmd_fn's fixed signature) proves
// issue_command() reaches a registered handler with its arguments intact,
// the same "recording stub, not the real body" shape as this file's
// CombatHooksGainExp/CombatHooksCrashCrashsave suites above, adapted to
// combat_command's enum-indexed dispatch instead of a single fn-ptr hook.

namespace {

// Records the do_shutdown() ACMD arguments the recording stub last saw,
// so a test can assert the shutdown cell's dispatch forwarded them intact
// without ever invoking the real force-shutdown body (no-death-test rule).
struct RecordedShutdownCall {
    char_data* ch = nullptr; // The acting character, forwarded verbatim from issue_command().
    char* argument = nullptr; // The raw command-line argument buffer, forwarded verbatim.
    waiting_type* wtl = nullptr; // The waiting-list slot, forwarded verbatim.
    int cmd = 0; // The command-table index issue_command() was called with.
    int subcmd = 0; // The subcommand index issue_command() was called with.
    bool called = false; // Set true once the stub runs; lets a test assert non-invocation too.
};

// File-scope recording slot the stub writes into and each test resets before/after use.
RecordedShutdownCall g_recorded_shutdown_call;

void recording_shutdown_stub(
    char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd)
{
    g_recorded_shutdown_call = RecordedShutdownCall { ch, argument, wtl, cmd, subcmd, true };
}

// Swaps combat_hooks.h's `shutdown` cell to a caller-supplied handler (the
// recording stub above, or nullptr), then restores the REAL registration
// via register_combat_command_dispatch() on destruction -- same
// restore-via-real-registrar shape as ScopedUnregisteredCombatCommand,
// generalized to accept a non-null stub since this cell's own discriminator
// must never register the real do_shutdown body.
class ScopedShutdownCommandOverride {
public:
    explicit ScopedShutdownCommandOverride(rots::combat::acmd_fn handler)
    {
        rots::combat::set_combat_command(rots::combat::combat_command::shutdown, handler);
    }

    ~ScopedShutdownCommandOverride() { register_combat_command_dispatch(); }

    ScopedShutdownCommandOverride(const ScopedShutdownCommandOverride&) = delete;
    ScopedShutdownCommandOverride& operator=(const ScopedShutdownCommandOverride&) = delete;
};

} // namespace

TEST(CombatHooksDispatch, IssueCommandReachesARegisteredStubForShutdownWithArgsIntact)
{
    g_recorded_shutdown_call = RecordedShutdownCall {};
    ScopedShutdownCommandOverride scoped(recording_shutdown_stub);
    char_data character {};
    char argument_text[] = "";
    waiting_type wtl {};

    rots::combat::issue_command(
        rots::combat::combat_command::shutdown, &character, argument_text, &wtl, 0, SCMD_SHUTDOWN);

    EXPECT_TRUE(g_recorded_shutdown_call.called)
        << "Expected the registered stub to have been reached.";
    EXPECT_EQ(g_recorded_shutdown_call.ch, &character);
    EXPECT_EQ(g_recorded_shutdown_call.argument, argument_text);
    EXPECT_EQ(g_recorded_shutdown_call.wtl, &wtl);
    EXPECT_EQ(g_recorded_shutdown_call.cmd, 0);
    EXPECT_EQ(g_recorded_shutdown_call.subcmd, SCMD_SHUTDOWN);
}

TEST(CombatHooksDispatch, IssueCommandDefaultsToANoOpWhenShutdownIsUnregistered)
{
    g_recorded_shutdown_call = RecordedShutdownCall {};
    ScopedUnregisteredCombatCommand unregistered(rots::combat::combat_command::shutdown);
    char_data character {};
    char argument_text[] = "";
    waiting_type wtl {};

    rots::combat::issue_command(
        rots::combat::combat_command::shutdown, &character, argument_text, &wtl, 0, SCMD_SHUTDOWN);

    EXPECT_FALSE(g_recorded_shutdown_call.called)
        << "Expected an unregistered shutdown cell to leave the (unrelated) stub's own recording "
           "flag untouched -- the real do_shutdown body never ran (and never will, in this "
           "discriminator).";
}
