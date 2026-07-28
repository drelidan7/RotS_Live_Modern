// ranger_tests.cpp

// LS-3b Wave T2 (ruling R-3b-B; R-A5 characterization-first pattern from
// LS-3a's T0b findings). Characterizes do_whistle() -- Table 1's WIDEST
// cursor window (ranger.cpp:1647 save / :1674 write / :1712 restore, a full
// `0..top_of_world` zone scan) -- BEFORE its cursor site converts onto
// rots::entity::ScopedRenderLocation. This suite must pass unchanged both
// before and after that conversion (the T2 report records both runs).
//
// What it pins: do_whistle() broadcasts once per same-zone room while ch's
// location field is spoofed to each room in turn -- the ranger's own room
// gets the "$n whistles powerfully" line, every OTHER same-zone room gets
// "You hear a powerful whistle nearby", and a room in a DIFFERENT zone gets
// nothing at all (the `rm->zone != zone_num` filter). ch's real location is
// unchanged once the command returns.

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

// do_whistle has no header declaration anywhere in the tree (ACMD bodies
// are address-taken into combat_hooks.h's dispatch table, never called by
// name outside their own TU) -- forward-declared here with the ACMD()
// macro, mirroring act_offe_tests.cpp's `ACMD(do_rescue);` precedent.
ACMD(do_whistle);

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

// Three rooms: 0 (ch's real room) and 1 share zone 0; room 2 is stamped
// into a different zone (99) so the fixture can prove do_whistle's zone
// filter, not just that broadcasting works at all. One bystander per room,
// each with a capturing descriptor.
struct DoWhistleContext {
    ScopedTestWorld test_world { 3 };
    char_data ch { };
    char_data room0_bystander { };
    char_data room1_bystander { };
    char_data room2_bystander { };
    descriptor_data ch_descriptor { };
    descriptor_data room0_descriptor { };
    descriptor_data room1_descriptor { };
    descriptor_data room2_descriptor { };

    // Declared after every char_data member so all three unwind (in reverse
    // declaration order) before the characters they reference are destroyed.
    std::optional<ScopedRoomOccupants> room0_occupants;
    std::optional<ScopedRoomOccupants> room1_occupants;
    std::optional<ScopedRoomOccupants> room2_occupants;

    DoWhistleContext()
    {
        reset_capturing_descriptor(ch_descriptor, &ch);
        reset_capturing_descriptor(room0_descriptor, &room0_bystander);
        reset_capturing_descriptor(room1_descriptor, &room1_bystander);
        reset_capturing_descriptor(room2_descriptor, &room2_bystander);
        ch.desc = &ch_descriptor;
        room0_bystander.desc = &room0_descriptor;
        room1_bystander.desc = &room1_descriptor;
        room2_bystander.desc = &room2_descriptor;

        ch.player.name = const_cast<char*>("Legolas");
        ch.specials.position = POSITION_STANDING;
        // RACE_ORC (13) is do_whistle's own hard-coded skill-check racial
        // bonus (ranger.cpp:1661, `GET_RACE(ch) == 13 ? 99 : 0`) -- with it,
        // GET_SKILL(ch, SKILL_WHISTLE)'s default-knowledge value of 80 plus
        // the +99 bonus (179) always beats number(0, 99)'s maximum roll (99),
        // so the "can barely hear yourself" failure branch is unreachable
        // deterministically, with no RNG seeding needed.
        ch.player.race = RACE_ORC;
        SET_BIT(ch.specials2.pref, PRF_HOLYLIGHT);

        room0_bystander.specials.position = POSITION_STANDING;
        room0_bystander.player.race = RACE_HUMAN;
        SET_BIT(room0_bystander.specials2.pref, PRF_HOLYLIGHT);
        room1_bystander.specials.position = POSITION_STANDING;
        room1_bystander.player.race = RACE_HUMAN;
        SET_BIT(room1_bystander.specials2.pref, PRF_HOLYLIGHT);
        room2_bystander.specials.position = POSITION_STANDING;
        room2_bystander.player.race = RACE_HUMAN;
        SET_BIT(room2_bystander.specials2.pref, PRF_HOLYLIGHT);

        // Rooms 0 and 1 keep dummy_room_data()'s zone-0 default; room 2 is
        // stamped into a distinct zone so it falls outside do_whistle's
        // `rm->zone != zone_num` filter -- the negative control.
        room_by_id_total(2)->zone = 99;

        room0_occupants.emplace(room_by_id_total(0), 0,
            std::initializer_list<char_data*> { &ch, &room0_bystander });
        room1_occupants.emplace(room_by_id_total(1), 1,
            std::initializer_list<char_data*> { &room1_bystander });
        room2_occupants.emplace(room_by_id_total(2), 2,
            std::initializer_list<char_data*> { &room2_bystander });
    }

    ~DoWhistleContext()
    {
        // room_by_id_total(2)->zone reverts to 0 automatically: ScopedTestWorld's
        // OWN reset_all_rooms() re-runs dummy_room_data() (zone = 0) on every
        // room the next time a ScopedTestWorld is constructed in this
        // process, so nothing further needs restoring here explicitly --
        // matching mage_tests.cpp's ZoneGuard's own out-of-band restore
        // being unnecessary once a fresh ScopedTestWorld always resets.
    }
};

} // namespace

TEST(DoWhistleCharacterization, WhistleBroadcastsPerRoomWhileSpoofingTheCursorAcrossTheWholeZone)
{
    DoWhistleContext context;

    do_whistle(&context.ch, mutable_arg(""), nullptr, 0, 1);

    // The ranger's own message (send_to_char, before the scan begins).
    EXPECT_STREQ(context.ch_descriptor.output, "You whistle powerfully.\r\n");

    // Room 0 (ch's own, real room): the self-room branch's "$n whistles
    // powerfully" line -- rendered while the cursor briefly reads room 0's
    // own id (rm_num == cur_room_num), the one iteration where the spoofed
    // location and the real one coincide.
    // PERS() renders ch as "*an Orc*" here rather than "Legolas": room0_bystander
    // has not been introduced to ch (the game's recognition system), so $n
    // substitutes the race-starred anonymous form -- a real, pre-existing
    // PERS() behavior this test pins as observed, not a naming choice of its
    // own.
    EXPECT_STREQ(context.room0_descriptor.output, "*an Orc* whistles powerfully.\n\r");

    // Room 1 (same zone, a DIFFERENT room from ch's real one): the
    // "You hear a powerful whistle nearby" line -- rendered while the
    // cursor is genuinely spoofed away from ch's real room, the F15 site.
    EXPECT_STREQ(context.room1_descriptor.output, "You hear a powerful whistle nearby.\n\r");

    // Room 2 (a different zone entirely): the zone filter excludes it from
    // the scan -- nothing is ever rendered there.
    EXPECT_STREQ(context.room2_descriptor.output, "");

    // The cursor is restored: ch's real location is unchanged once the
    // whole zone scan completes.
    EXPECT_EQ(location_of(&context.ch), 0);
}
