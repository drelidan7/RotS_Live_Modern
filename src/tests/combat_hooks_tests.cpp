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
#include "../handler.h"
#include "../interpre.h"
#include "../limits.h"
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

// -----------------------------------------------------------------------
// Task 4b hooks (combat-pilot wave): extract_char (see combat_hooks.h's
// Task 4b section for the full seam design). CONSUMER-FREE this wave --
// no fight.cpp/clerics.cpp call site converts yet -- so, like
// CombatHooksSpecial above and big_brother_hooks_tests.cpp's suites, both
// hooks are exercised directly against a recording stub rather than the
// real (session-coupled, expensive-to-fixture) body: "registered stub
// receives args intact; unregistered default semantics asserted" is the
// brief's own discriminator shape for this task.
// -----------------------------------------------------------------------

namespace {

struct RecordedExtractCharCall {
    char_data* ch = nullptr;
    int new_room = 0;
    bool called = false;
};

RecordedExtractCharCall g_recorded_extract_char_call;

void recording_extract_char_stub(char_data* ch, int new_room)
{
    g_recorded_extract_char_call = RecordedExtractCharCall { ch, new_room, true };
}

// Swaps combat_hooks.h's extract_char hook, then restores the REAL
// handler.cpp forwarder via register_extract_char_hook() on destruction --
// same restore-via-real-registrar shape as ScopedSpecialHandler above.
class ScopedExtractCharHook {
public:
    explicit ScopedExtractCharHook(rots::combat::extract_char_fn hook)
    {
        rots::combat::set_extract_char_hook(hook);
    }

    ~ScopedExtractCharHook() { register_extract_char_hook(); }

    ScopedExtractCharHook(const ScopedExtractCharHook&) = delete;
    ScopedExtractCharHook& operator=(const ScopedExtractCharHook&) = delete;
};

} // namespace

// rots::combat::extract_char(ch, new_room) -- DISCRIMINATOR: a recording
// stub proves the 2-arg dispatch overload forwards both arguments intact.

TEST(CombatHooksExtractChar, TwoArgDispatchReachesARegisteredStubWithArgsIntact)
{
    g_recorded_extract_char_call = RecordedExtractCharCall {};
    ScopedExtractCharHook scoped(recording_extract_char_stub);
    char_data character {};

    rots::combat::extract_char(&character, 7);

    EXPECT_TRUE(g_recorded_extract_char_call.called)
        << "Expected the registered stub to have been reached.";
    EXPECT_EQ(g_recorded_extract_char_call.ch, &character);
    EXPECT_EQ(g_recorded_extract_char_call.new_room, 7);
}

// DISCRIMINATOR: the 1-arg overload forwards to the 2-arg overload with the
// -1 sentinel, mirroring handler.cpp's own extract_char(ch) ->
// extract_char(ch, -1) forward exactly (pilot-census.md section 3.6).

TEST(CombatHooksExtractChar, OneArgDispatchForwardsWithNegativeOneSentinel)
{
    g_recorded_extract_char_call = RecordedExtractCharCall {};
    ScopedExtractCharHook scoped(recording_extract_char_stub);
    char_data character {};

    rots::combat::extract_char(&character);

    EXPECT_TRUE(g_recorded_extract_char_call.called);
    EXPECT_EQ(g_recorded_extract_char_call.ch, &character);
    EXPECT_EQ(g_recorded_extract_char_call.new_room, -1)
        << "Expected the 1-arg overload to reach the stub with handler.h:197's own sentinel "
           "default (-1), matching the real extract_char(ch) -> extract_char(ch, -1) forward.";
}

TEST(CombatHooksExtractChar, DispatchDefaultsToANoOpWhenUnregistered)
{
    g_recorded_extract_char_call = RecordedExtractCharCall {};
    ScopedExtractCharHook unregistered(nullptr);
    char_data character {};

    rots::combat::extract_char(&character, 7);

    EXPECT_FALSE(g_recorded_extract_char_call.called)
        << "Expected an unregistered extract_char hook to leave the (unrelated) stub's own "
           "recording flag untouched -- the real forwarder never ran, and the tripwire default "
           "is a logged no-op, not a call to any stub.";
}

// -----------------------------------------------------------------------
// Task 4b hooks (combat-pilot wave): gain_exp/gain_exp_regardless/
// remove_fame_war_bonuses (limits.cpp; pilot-census.md section 7.4/7.5/7.6).
// Same recording-stub discriminator shape as CombatHooksExtractChar above.
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
