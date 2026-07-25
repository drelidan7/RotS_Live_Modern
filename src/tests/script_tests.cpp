// Coverage rider for the LS-1 Wave (Tranche C) script batch: mudlle.cpp +
// script.cpp had zero behavioral coverage anywhere in the tree before this
// (script_hooks_tests.cpp only exercises the combat_hooks.h/script_hooks.h
// dispatch *hooks*, never a real script.cpp function body).
//
// get_room_param() exercises the three SCRIPT_PARAM_CHn_ROOM self-room
// conversions (world[info->ch[n]->in_room] -> room_of(info->ch[n])).
//
// trigger_room_event()'s ON_BEFORE_ENTER/ON_ENTER cases needed scrutiny
// beyond what the census's Family classification covered: their original
// loop conditions were `tmpch && return_value` (not a `break`), so a naive
// occupants() conversion would keep scanning past the first occupant whose
// trigger denies entry (return_value == 0) and let a LATER occupant's
// trigger silently overwrite return_value. The conversion added an
// `if (!return_value) break;` guard to preserve the original stop-on-first-
// denial semantics.
//
// Forcing return_value to 0 requires a real attached mudlle script (a
// deeper harness than this rider's budget covers), so this test instead
// pins the achievable, still-meaningful baseline: with two unscripted
// occupants in the room (char_data::specials.script_number == 0, the
// zero-initialized default -- char_has_script() finds nothing, so
// trigger_before_char_enter()/trigger_char_enter() take their own early
// `return 1` path without running any script), trigger_room_event() must
// still walk BOTH occupants via the converted occupants(room) range and
// return 1 (nothing denied entry), proving the walk itself -- and the
// converted room_of()/occupants() call sites -- are not broken. The
// compound-condition guard's exact short-circuit edge is additionally
// backed by the boot-golden/seed42-golden byte-identity this batch's gate
// already produced, and by the same guard shape already regression-tested
// directly against real product code in mobact_tests.cpp
// (MobactStandardAggressive.TargetsTheFirstEligibleOccupantNotALaterOne).

#include "../handler.h"
#include "../protos.h"
#include "../script.h"
#include "rots/core/character.h"
#include "rots/core/room.h"
#include "test_world.h"

#include <gtest/gtest.h>

room_data *get_room_param(int param, info_script *info);
int trigger_room_event(int trigger_type, room_data *room, char_data *ch);

extern room_data world;
extern int top_of_world;

namespace {

TEST(GetRoomParam, Chn1RoomReturnsTheCharactersOwnRoomViaRoomOf) {
    ScopedTestWorld test_world{2};
    char_data character{};
    character.in_room = 1;

    info_script info{};
    info.ch[0] = &character;

    EXPECT_EQ(get_room_param(SCRIPT_PARAM_CH1_ROOM, &info), &world[1])
        << "Expected the SCRIPT_PARAM_CH1_ROOM case's converted room_of(info->ch[0]) to resolve "
           "to the character's own room, matching world[character.in_room] byte-for-byte.";
}

TEST(GetRoomParam, Chn1RoomReturnsNullWhenNoCharacterIsBound) {
    info_script info{};
    info.ch[0] = nullptr;

    EXPECT_EQ(get_room_param(SCRIPT_PARAM_CH1_ROOM, &info), nullptr);
}

struct RoomTriggerContext {
    ScopedTestWorld test_world{1};
    char_data actor{};
    char_data occupant_a{};
    char_data occupant_b{};
    char_data *original_people = nullptr;

    RoomTriggerContext() {
        top_of_world = 0;
        original_people = world[0].people;

        actor.in_room = 0;
        // Zero-initialized specials.script_number on both occupants means
        // char_has_script() finds nothing for either ON_BEFORE_ENTER or
        // ON_ENTER, so trigger_before_char_enter()/trigger_char_enter() take
        // their own `if (!(script_position = char_has_script(...)))`-false
        // early return of 1 without running any script -- deterministic,
        // no mudlle VM required.
        occupant_a.in_room = 0;
        occupant_b.in_room = 0;
        occupant_a.next_in_room = &occupant_b;
        occupant_b.next_in_room = nullptr;
        world[0].people = &occupant_a;
    }

    ~RoomTriggerContext() {
        world[0].people = original_people;
        occupant_a.next_in_room = nullptr;
        occupant_b.next_in_room = nullptr;
    }
};

} // namespace

TEST(TriggerRoomEvent, BeforeEnterWalksAllUnscriptedOccupantsAndReturnsOne) {
    RoomTriggerContext context;

    EXPECT_EQ(trigger_room_event(ON_BEFORE_ENTER, &world[0], &context.actor), 1)
        << "Expected the converted occupants(room) walk to reach every occupant (neither of which "
           "has an attached script) and leave return_value at its initial 1.";
}

TEST(TriggerRoomEvent, EnterWalksAllUnscriptedOccupantsAndReturnsOne) {
    RoomTriggerContext context;

    EXPECT_EQ(trigger_room_event(ON_ENTER, &world[0], &context.actor), 1)
        << "Expected the converted occupants(room) walk (ON_ENTER case) to reach every occupant "
           "and leave return_value at its initial 1.";
}


// -----------------------------------------------------------------------
// LS-2 T4-legacy follow-up (ls2-global-constraints.md's "Inherited test
// debt" section, item 1; ls1-wholebranch-review.md C.1): the denial edge
// (return_value == 0) neither RoomTriggerContext test above can reach,
// pinned via a REAL attached script -- run_script() interprets a hand-built
// script_data chain directly (no mudlle source/bytecode compiler involved;
// same technique poison_notification_tests.cpp's PoisonRemovalScriptTest
// already established for a different script.cpp entry point), and
// script_table/top_of_script_table (no header declares either -- forward-
// declared locally here, matching script.cpp's/act_wiz.cpp's own
// precedent) are temporarily pointed at a single fabricated entry.
//
// The two tests below don't just prove the denial return value -- they
// prove the `if (!return_value) break;` guard the LS-1 conversion added
// (replacing the original raw for-loop's own `tmpch && return_value`
// condition) actually STOPS the walk: occupant_a's script denies first,
// and occupant_b (deliberately UNSCRIPTED, script_number == 0) sits right
// behind it in the chain. If the guard were ever dropped, the walk would
// keep going, reach occupant_b, and trigger_before_char_enter()/
// trigger_char_enter()'s own early "no script -> return 1" path would
// SILENTLY OVERWRITE return_value back to 1 (allow) -- exactly the failure
// mode the guard exists to prevent, and exactly what these tests would
// catch (they'd observe a 1, not a 0).

extern struct script_head* script_table;
extern int top_of_script_table;

namespace {

// trigger_marker_type: ON_BEFORE_ENTER or ON_ENTER -- the "section header"
// script_data node char_has_script() scans for; the real command to run
// starts at its ->next (script.cpp's own scan-then-run-successor shape,
// see trigger_before_char_enter()/trigger_char_enter()).
struct ScriptedDenialContext {
    ScopedTestWorld test_world{1};
    char_data actor{};
    char_data occupant_a{}; // scripted: denies via SCRIPT_RETURN_FALSE.
    char_data occupant_b{}; // deliberately UNSCRIPTED -- must never be consulted.

    script_data trigger_marker{};
    script_data return_false_command{};
    script_head scripted_entry{};

    struct script_head* previous_script_table = nullptr;
    int previous_top_of_script_table = 0;

    static constexpr int kScriptNumber = 90001; // arbitrary vnum, unique to this fixture.

    explicit ScriptedDenialContext(int trigger_marker_type)
    {
        top_of_world = 0;
        actor.in_room = 0;

        occupant_a.in_room = 0;
        occupant_a.specials.script_number = kScriptNumber;
        occupant_a.next_in_room = &occupant_b;

        occupant_b.in_room = 0;
        occupant_b.specials.script_number = 0; // unscripted -- char_has_script() must find nothing.
        occupant_b.next_in_room = nullptr;

        world[0].people = &occupant_a;

        trigger_marker.command_type = trigger_marker_type;
        trigger_marker.next = &return_false_command;
        return_false_command.command_type = SCRIPT_RETURN_FALSE;
        return_false_command.next = nullptr;

        scripted_entry.number = kScriptNumber;
        scripted_entry.name = nullptr;
        scripted_entry.virt_num = 0;
        scripted_entry.description = nullptr;
        scripted_entry.host = nullptr;
        scripted_entry.script = &trigger_marker;

        previous_script_table = script_table;
        previous_top_of_script_table = top_of_script_table;
        script_table = &scripted_entry;
        top_of_script_table = 0;
    }

    ~ScriptedDenialContext()
    {
        script_table = previous_script_table;
        top_of_script_table = previous_top_of_script_table;
        RELEASE(occupant_a.specials.script_info);
        RELEASE(occupant_b.specials.script_info);
        world[0].people = nullptr;
        occupant_a.next_in_room = nullptr;
    }
};

} // namespace

TEST(TriggerRoomEvent, BeforeEnterStopsAtTheFirstDenyingOccupantAndReturnsZero) {
    ScriptedDenialContext context(ON_BEFORE_ENTER);

    EXPECT_EQ(trigger_room_event(ON_BEFORE_ENTER, &world[0], &context.actor), 0)
        << "Expected occupant_a's attached SCRIPT_RETURN_FALSE script to deny entry.";
}

TEST(TriggerRoomEvent, BeforeEnterNeverConsultsAnOccupantAfterADenial) {
    ScriptedDenialContext context(ON_BEFORE_ENTER);

    const int result = trigger_room_event(ON_BEFORE_ENTER, &world[0], &context.actor);

    EXPECT_EQ(result, 0)
        << "If the `if (!return_value) break;` guard were ever dropped, the walk would continue "
           "to occupant_b (unscripted -- trigger_before_char_enter()'s own early 'no script' path "
           "unconditionally returns 1) and silently overwrite return_value back to 1. Observing 0 "
           "here proves the walk stopped at occupant_a and never reached occupant_b.";
}

TEST(TriggerRoomEvent, EnterStopsAtTheFirstDenyingOccupantAndReturnsZero) {
    ScriptedDenialContext context(ON_ENTER);

    EXPECT_EQ(trigger_room_event(ON_ENTER, &world[0], &context.actor), 0)
        << "Expected occupant_a's attached SCRIPT_RETURN_FALSE script to deny entry (ON_ENTER case, "
           "reached before the trailing object-trigger loop).";
}

TEST(TriggerRoomEvent, EnterNeverConsultsAnOccupantAfterADenial) {
    ScriptedDenialContext context(ON_ENTER);

    const int result = trigger_room_event(ON_ENTER, &world[0], &context.actor);

    EXPECT_EQ(result, 0)
        << "Same guard-regression check as the ON_BEFORE_ENTER case above, for ON_ENTER's "
           "trigger_char_enter() walk.";
}
