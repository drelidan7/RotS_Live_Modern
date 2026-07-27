// Coverage rider for act_offe.cpp's do_rescue() (LS-2 Wave Task T3d;
// ls2-task-3d-report.md) -- the wave's MANDATORY rider. do_rescue's
// find-first-break walk (act_offe.cpp:755, converted this task) is Family F:
// tmp_ch is declared UNINITIALIZED (`struct char_data *victim, *tmp_ch;`)
// and relied on the raw for-loop's own init-expression to leave it null on
// the empty-room / no-matching-fighter path -- the exact vampire_killer UB
// trap Amendment 1 (LS-1) first found. The conversion adds a mandatory
// `= nullptr` pre-init; only a real empty-room negative control proves that
// pre-init is load-bearing rather than decorative.
//
// combat_hooks_tests.cpp's existing CombatHooksDispatch.IssueCommandReaches
// TheRealDoRescueWhenRegistered/...DefaultsToANoOp... pair only exercises
// do_rescue's FIRST guard (an invalid wtl target, returning before the walk
// is ever reached) -- the walk itself, the "no mortal is fighting" message,
// and the success path all had zero coverage before this rider.

#include "../comm.h"
#include "../handler.h"
#include "../interpre.h"
#include "../utils.h"
#include "rots/core/character.h"
#include "rots/core/descriptor.h"
#include "rots/core/types.h"
#include "test_placement.h"
#include "test_random_utils.h"
#include "test_world.h"

#include <gtest/gtest.h>

// do_rescue has no header declaration anywhere in the tree (ACMD bodies are
// address-taken into combat_hooks.h's dispatch table, never called by name
// outside their own TU) -- forward-declared here with the ACMD() macro,
// mirroring act_format_tests.cpp:41's `ACMD(do_bash);`.
ACMD(do_rescue);

// combat_list is a comm.cpp/fight.cpp-owned process-wide global that
// set_fighting() (fight.cpp) links stack-local char_data into -- mirrors
// fight_proc_tests.cpp's own `extern char_data* combat_list;` /
// save-and-restore convention (that file's PerformViolenceTest fixture).
extern char_data *combat_list;
// do_rescue's set_fighting()/delay path can also link a character into the
// process-wide waiting_list (comm.cpp's delayed-command list). This fixture's
// char_data are stack-local, so a stale entry left behind outlives its storage
// and the NEXT test to walk waiting_list dereferences freed stack memory --
// see abort_delay_impl(), comm.cpp:2847-2848. Saved/restored exactly like
// combat_list above (olog_hai_tests.cpp sets the same precedent).
extern char_data *waiting_list;

namespace {

// Mirrors comm_act_tests.cpp's reset_capturing_descriptor() (duplicated,
// not shared -- that copy lives in a different TU's anonymous namespace).
// Points a descriptor's output at its own small_outbuf so do_rescue's
// send_to_char()/act() calls can be inspected directly instead of going to
// a real socket.
void reset_capturing_descriptor(descriptor_data &descriptor, char_data *character) {
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = 0; // CON_PLYNG
    descriptor.character = character;
}

// A rescuer (ch) and the character being rescued (victim), sharing room 0
// of a fresh one-room test world. Tests that need a third character
// (the one caught fighting victim) chain it in themselves -- the
// not-found rider deliberately leaves the room holding only ch/victim.
struct DoRescueContext {
    ScopedTestWorld test_world;
    char_data ch{};
    char_data victim{};
    descriptor_data ch_descriptor{};
    char_data *original_combat_list;
    char_data *original_waiting_list;

    // The occupant chain: rescuer at the head, victim behind it -- the same
    // head-first order this fixture published by hand before LS-3a T3
    // (test_placement.h). A test that needs a third character in the room
    // restates the WHOLE chain in a NESTED ScopedRoomOccupants rather than
    // re-pointing a link (the pilot's idiom rule 4). Declared LAST so it
    // unwinds before the characters it manages and before the ScopedTestWorld
    // whose room it points into; its constructor stamps both locations
    // through set_location(), so this fixture writes in_room nowhere.
    ScopedRoomOccupants occupants{&test_world.room(), 0, {&ch, &victim}};

    DoRescueContext()
        : original_combat_list(combat_list), original_waiting_list(waiting_list) {
        combat_list = nullptr;
        waiting_list = nullptr;
        clear_test_random_values();

        reset_capturing_descriptor(ch_descriptor, &ch);
        ch.desc = &ch_descriptor;

        ch.player.race = RACE_HUMAN;
        ch.player.name = const_cast<char *>("Rescuer");
        victim.player.name = const_cast<char *>("Victim");

        // act()'s TO_CHAR/TO_NOTVICT delivery gate requires AWAKE(to)
        // (GET_POS(to) > POSITION_SLEEPING); value-initialized char_data
        // defaults specials.position to POSITION_DEAD (0), which would
        // silently swallow the "no mortal is fighting" message.
        ch.specials.position = POSITION_STANDING;
        victim.specials.position = POSITION_STANDING;
    }

    ~DoRescueContext() {
        // Unlink anything set_fighting() may have added to the process-wide
        // combat_list before this scope's stack-local char_data go away.
        ch.specials.fighting = nullptr;
        victim.specials.fighting = nullptr;
        ch.next_fighting = nullptr;
        victim.next_fighting = nullptr;
        ch.delay.next = nullptr;
        victim.delay.next = nullptr;
        combat_list = original_combat_list;
        // Restoring the head drops every entry added during the test --
        // including the test body's own stack-local tmp_ch, which this
        // fixture has no handle on.
        waiting_list = original_waiting_list;

        // The chain-head restore, the two unlinks and the two NOWHERE
        // de-locations this body used to perform are now `occupants`, which
        // unwinds right after it.
        clear_test_random_values();
    }
};

// Arbitrary abs_number used only to satisfy char_exists() for the wtl-path
// target lookup -- picked well clear of the small hand-picked ids other
// suites use (act_info_format_tests.cpp's 9001-9004, olog_hai_tests.cpp's
// 41-46), set then removed within each test.
constexpr int kVictimAbsNumber = 9301;

} // namespace

// (a) FOUND: a third character (tmp_ch) is fighting victim in the room --
// the converted occupants() walk must find it and the rescue must proceed,
// observable via set_fighting()'s two-way link (do_rescue's own
// `set_fighting(ch, tmp_ch); set_fighting(tmp_ch, ch);` pair).
TEST(DoRescue, FindsTheFighterAndRescuesTheVictimWhenSomeoneIsFightingThem) {
    DoRescueContext context;
    char_data tmp_ch{};
    // A mid-test chain extension is a NESTED ScopedRoomOccupants restating the
    // WHOLE chain (rescuer, victim, fighter), not a re-pointed link -- the
    // pilot's idiom rule 4. Declared after tmp_ch so it unwinds first, which
    // is also what takes the stack char_data back out of the process-global
    // room's occupant chain (THE FIXTURE-HYGIENE RULE); the enclosing
    // fixture's own helper then restores the pre-test head. The enclosing
    // destructor resolves its room through the ScopedTestWorld it owns, not
    // through room_of(&member), so the nested unwind's NOWHERE parking cannot
    // mislead it.
    ScopedRoomOccupants occupants{
        &context.test_world.room(), 0, {&context.ch, &context.victim, &tmp_ch}};
    tmp_ch.specials.fighting = &context.victim;

    // percent = number(1, 101) must not exceed prob = GET_SKILL(ch,
    // SKILL_RESCUE) (80, the macro's own default for an empty
    // ch->knowledge); hooking 0.0 forces percent to the guaranteed-success
    // floor of 1.
    push_test_random_value(0.0);

    waiting_type wtl{};
    wtl.targ1.type = TARGET_CHAR;
    wtl.targ1.ch_num = kVictimAbsNumber;
    wtl.targ1.ptr.ch = &context.victim;
    set_char_exists(kVictimAbsNumber);

    do_rescue(&context.ch, mutable_arg(""), &wtl, 0, 0);

    EXPECT_EQ(context.ch.specials.fighting, &tmp_ch)
        << "Expected the converted occupants() walk to find tmp_ch and set_fighting(ch, "
           "tmp_ch) to link them.";
    EXPECT_EQ(tmp_ch.specials.fighting, &context.ch)
        << "Expected set_fighting(tmp_ch, ch) to link the reciprocal side.";

    tmp_ch.specials.fighting = nullptr;
    tmp_ch.next_fighting = nullptr;
    remove_char_exists(kVictimAbsNumber);
}

// (b) NOT FOUND: nobody in the room is fighting victim -- the walk's
// pre-initialized `tmp_ch = nullptr` must survive untouched (this is the
// exact case that would read uninitialized memory without the mandatory
// pre-init), do_rescue must take the `if (!tmp_ch)` failure branch, and
// nothing may be mutated.
TEST(DoRescue, ReportsNoMortalFightingWhenNobodyInTheRoomIsFightingTheVictim) {
    DoRescueContext context;
    push_test_random_value(0.0);

    waiting_type wtl{};
    wtl.targ1.type = TARGET_CHAR;
    wtl.targ1.ch_num = kVictimAbsNumber;
    wtl.targ1.ptr.ch = &context.victim;
    set_char_exists(kVictimAbsNumber);

    do_rescue(&context.ch, mutable_arg(""), &wtl, 0, 0);

    // player.sex is SEX_NEUTRAL (0, value-initialized) on victim, so $M
    // expands via HMHR() to "it".
    EXPECT_STREQ(context.ch_descriptor.output, "But no mortal is fighting it!\n\r")
        << "Expected the literal no-fighter message once the walk's pre-initialized tmp_ch "
           "survives with no match.";
    EXPECT_EQ(context.ch.specials.fighting, nullptr)
        << "Expected nothing to be mutated on the not-found path.";
    EXPECT_EQ(context.victim.specials.fighting, nullptr);

    remove_char_exists(kVictimAbsNumber);
}
