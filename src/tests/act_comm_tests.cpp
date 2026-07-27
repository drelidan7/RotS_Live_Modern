// Coverage rider for act_comm.cpp's do_say() delivery walks (LS-2 Wave Task
// T3d; ls2-task-3d-report.md). combat_hooks_tests.cpp's
// CombatHooksDispatch.IssueCommandReachesTheRealDoSayWhenRegistered/...
// DefaultsToANoOp... pair only exercises do_say's FIRST guard (the
// IS_NPC(ch) && GET_INT(ch) < 6 early return) -- both converted room-occupant
// delivery walks (act_comm.cpp:122/:133, now
// rots::entity::occupants(room_of(ch))) had zero coverage before this rider.

#include "../comm.h"
#include "../db.h"
#include "../handler.h"
#include "../interpre.h"
#include "rots/core/character.h"
#include "rots/core/descriptor.h"
#include "rots/core/types.h"
#include "test_placement.h"
#include "test_world.h"

#include <gtest/gtest.h>

#include <string>

// do_say has no header declaration anywhere in the tree (ACMD bodies are
// address-taken into combat_hooks.h's dispatch table, never called by name
// outside their own TU) -- forward-declared here with the ACMD() macro,
// mirroring act_format_tests.cpp:41's `ACMD(do_bash);`.
ACMD(do_say);

namespace {

// Mirrors comm_act_tests.cpp's reset_capturing_descriptor() (duplicated,
// not shared -- that copy lives in a different TU's anonymous namespace).
// Also stamps a fake socket fd (matching act_info_format_tests.cpp's/
// interpre_account_menu_tests.cpp's established `.descriptor = 7`
// convention): do_say's own pre-say_to_char() walk guard
// (`s->desc && s->desc->descriptor && ...`, act_comm.cpp:123) checks this
// field directly, unlike act_impl()'s delivery gate which only checks
// `to->desc`.
void reset_capturing_descriptor(descriptor_data &descriptor, char_data *character) {
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = 0; // CON_PLYNG
    descriptor.character = character;
    descriptor.descriptor = 7;
}

// A speaker and a single listener sharing room 0 of a fresh one-room test
// world -- exactly the shape the converted occupants(room_of(ch)) walks in
// do_say need.
struct DoSayContext {
    ScopedTestWorld test_world;
    char_data speaker{};
    char_data listener{};
    descriptor_data speaker_descriptor{};
    descriptor_data listener_descriptor{};

    // The occupant chain: speaker at the head, listener behind it -- exactly
    // the head-first order this fixture used to publish by hand (LS-3a T3,
    // test_placement.h). Declared LAST so it unwinds before the characters it
    // manages and before the ScopedTestWorld whose room it points into. Its
    // constructor stamps both locations through set_location(), so this
    // fixture no longer writes in_room anywhere.
    ScopedRoomOccupants occupants{&test_world.room(), 0, {&speaker, &listener}};

    DoSayContext() {
        reset_capturing_descriptor(speaker_descriptor, &speaker);
        reset_capturing_descriptor(listener_descriptor, &listener);

        // say_to_char()'s delivery gate (via act()'s TO_CHAR path) requires
        // AWAKE(to) (GET_POS(to) > POSITION_SLEEPING); value-initialized
        // char_data defaults specials.position to POSITION_DEAD (0).
        speaker.specials.position = POSITION_STANDING;
        listener.specials.position = POSITION_STANDING;

        speaker.desc = &speaker_descriptor;
        listener.desc = &listener_descriptor;
    }

    // The chain-head restore, the two unlinks and the two NOWHERE
    // de-locations this destructor used to perform are now `occupants`, which
    // unwinds immediately after it. ScopedTestWorld leaves room 0's head null
    // at construction, so restoring the SAVED head is byte-identical to the
    // `people = original_people` this used to write.
    ~DoSayContext() = default;
};

} // namespace

TEST(DoSay, DeliversToTheOtherRoomOccupantButNotBackToTheSpeaker) {
    DoSayContext context;
    char argument[] = "hello there";

    do_say(&context.speaker, argument, nullptr, 0, 0);

    std::string listener_output(context.listener_descriptor.output);
    EXPECT_NE(listener_output.find("hello there"), std::string::npos)
        << "Expected the converted occupants(room_of(ch)) delivery walk (act_comm.cpp:122) to "
           "reach the listener. Actual output: \""
        << listener_output << "\"";

    // The walk's own `s != ch` filter (act_comm.cpp:123) must keep the
    // speaker from hearing their own say_to_char() delivery -- the speaker's
    // only output should be the PRF_ECHO-off "Ok." acknowledgement.
    EXPECT_STREQ(context.speaker_descriptor.output, "Ok.\n\r");
}
