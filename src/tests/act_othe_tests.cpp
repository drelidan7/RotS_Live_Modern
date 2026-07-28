// act_othe_tests.cpp

// LS-3b Wave T2 (ruling R-3b-B; R-A5 characterization-first pattern from
// LS-3a's T0b findings). Characterizes do_knock() -- one of Table 1's two
// widest/highest-risk cursor windows (act_othe.cpp:1821 save / :1822 write /
// :1842 restore) -- BEFORE its cursor site converts onto
// rots::entity::ScopedRenderLocation. This suite must pass unchanged both
// before and after that conversion (the T2 report records both runs).
//
// What it pins: do_knock() renders its "you hear a knock" message to the
// room BEHIND the door -- i.e. while ch's location field is spoofed to that
// room -- not to ch's own room, and ch's real location is unchanged once
// the command returns. This is exactly the F15 persistence-spoof/cursor
// behavior ScopedRenderLocation must reproduce byte-for-byte.

#include "../comm.h"
#include "../handler.h"
#include "../interpre.h"
#include "../utils.h"
#include "rots/core/character.h"
#include "rots/core/descriptor.h"
#include "rots/core/room.h"
#include "rots/core/types.h"
#include "test_placement.h"
#include "test_world.h"

#include <gtest/gtest.h>

#include <optional>

// do_knock has no header declaration anywhere in the tree (ACMD bodies are
// address-taken into combat_hooks.h's dispatch table, never called by name
// outside their own TU) -- forward-declared here with the ACMD() macro,
// mirroring act_offe_tests.cpp's `ACMD(do_rescue);` precedent.
ACMD(do_knock);

namespace {

// Mirrors comm_act_tests.cpp's reset_capturing_descriptor() (duplicated,
// not shared -- that copy lives in a different TU's anonymous namespace).
void reset_capturing_descriptor(descriptor_data& descriptor, char_data* character)
{
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = 0; // CON_PLAYING
    descriptor.character = character;
}

// Two rooms (0 and 1) joined by a door ("gate") ch stands north of: room 0's
// NORTH exit leads to room 1, and room 1's SOUTH exit leads back, both
// EX_ISDOOR and sharing the same keyword so do_knock's reverse-exit branch
// (`EXIT(ch, rev_dir[tmp])->keyword` non-null) renders the
// "You hear a knock on the gate." line rather than the directionless
// "sounding {dir}ward" fallback. A bystander sits in each room with a
// capturing descriptor so the test can tell which room actually received
// each broadcast -- the whole point of characterizing the cursor.
struct DoKnockContext {
    ScopedTestWorld test_world { 2 };
    char_data ch { };
    char_data room0_bystander { };
    char_data room1_bystander { };
    descriptor_data ch_descriptor { };
    descriptor_data room0_descriptor { };
    descriptor_data room1_descriptor { };
    char door_keyword[8] = "gate";
    room_direction_data door_0_to_1 { };
    room_direction_data door_1_to_0 { };

    // Declared after every char_data/room_direction_data member so both
    // unwind (in reverse declaration order) before the characters and doors
    // they reference are destroyed.
    std::optional<ScopedRoomOccupants> room0_occupants;
    std::optional<ScopedRoomOccupants> room1_occupants;

    DoKnockContext()
    {
        reset_capturing_descriptor(ch_descriptor, &ch);
        reset_capturing_descriptor(room0_descriptor, &room0_bystander);
        reset_capturing_descriptor(room1_descriptor, &room1_bystander);
        ch.desc = &ch_descriptor;
        room0_bystander.desc = &room0_descriptor;
        room1_bystander.desc = &room1_descriptor;

        ch.player.name = const_cast<char*>("Frodo");
        ch.specials.position = POSITION_STANDING;
        ch.player.race = RACE_HUMAN;
        ch.player.sex = SEX_MALE; // pins $m/$s -> "him"/"his" in the no-target branch
        // PRF_HOLYLIGHT on everyone sidesteps CAN_SEE()'s darkness gate --
        // real light bookkeeping is out of scope for this cursor
        // characterization (comm_act_tests.cpp's RoomPairContext precedent).
        SET_BIT(ch.specials2.pref, PRF_HOLYLIGHT);
        room0_bystander.specials.position = POSITION_STANDING;
        room0_bystander.player.race = RACE_HUMAN;
        SET_BIT(room0_bystander.specials2.pref, PRF_HOLYLIGHT);
        room1_bystander.specials.position = POSITION_STANDING;
        room1_bystander.player.race = RACE_HUMAN;
        SET_BIT(room1_bystander.specials2.pref, PRF_HOLYLIGHT);

        door_0_to_1.keyword = door_keyword;
        door_0_to_1.exit_info = EX_ISDOOR;
        door_0_to_1.to_room = 1;
        door_0_to_1.exit_width = 4;
        door_0_to_1.key = -1;
        door_0_to_1.general_description = nullptr;

        door_1_to_0 = door_0_to_1;
        door_1_to_0.to_room = 0;

        room_by_id_total(0)->dir_option[NORTH] = &door_0_to_1;
        room_by_id_total(1)->dir_option[SOUTH] = &door_1_to_0;

        room0_occupants.emplace(room_by_id_total(0), 0,
            std::initializer_list<char_data*> { &ch, &room0_bystander });
        room1_occupants.emplace(room_by_id_total(1), 1,
            std::initializer_list<char_data*> { &room1_bystander });
    }

    ~DoKnockContext()
    {
        // The two room_direction_data members above are about to be
        // destroyed along with this context (they are plain members, not
        // RAII) -- clear the world's pointers to them FIRST, before that
        // happens, so no later test in the monolithic single-process runner
        // ever dereferences a dangling exit.
        room_by_id_total(0)->dir_option[NORTH] = nullptr;
        room_by_id_total(1)->dir_option[SOUTH] = nullptr;
    }
};

} // namespace

TEST(DoKnockCharacterization, KnockRendersTheEchoInTheRoomBehindTheDoorNotChsOwnRoom)
{
    DoKnockContext context;

    do_knock(&context.ch, mutable_arg("gate"), nullptr, 0, 0);

    // The two messages that render BEFORE the cursor is written (ch's real
    // room, room 0): the actor's own confirmation and the room-0 bystander's
    // "$n knocked" broadcast.
    EXPECT_STREQ(context.ch_descriptor.output, "You knocked on the gate.\n");
    EXPECT_STREQ(context.room0_descriptor.output, "Frodo knocked on the gate.\n\n\r");

    // The message that renders WHILE the cursor is parked on room 1 (the F15
    // persistence-spoof site): only room 1's bystander receives it, and room
    // 0's bystander output above shows no trace of it -- proving act()'s
    // TO_ROOM resolution followed the spoofed location, not ch's real one.
    EXPECT_STREQ(context.room1_descriptor.output, "You hear a knock on the gate.\n\n\r");

    // The cursor is restored: ch's real location is unchanged once the
    // command returns.
    EXPECT_EQ(location_of(&context.ch), 0);
}

TEST(DoKnockCharacterization, KnockWithNoTargetSendsTheSelfDirectedMessageAndNeverMovesTheCursor)
{
    // The `!*argument` early-return branch (act_othe.cpp:1804-1807) never
    // reaches the cursor window at all -- a negative control confirming the
    // cursor conversion cannot affect this path.
    DoKnockContext context;

    do_knock(&context.ch, mutable_arg(""), nullptr, 0, 0);

    EXPECT_STREQ(context.ch_descriptor.output, "You knocked yourself on your head.\n\r");
    EXPECT_STREQ(context.room0_descriptor.output, "Frodo knocked himself on his head.\n\r\n\r");
    EXPECT_STREQ(context.room1_descriptor.output, "");
    EXPECT_EQ(location_of(&context.ch), 0);
}
