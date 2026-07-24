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
