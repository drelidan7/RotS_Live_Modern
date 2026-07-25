// Coverage rider for shop.cpp's closing_time() patron-eviction walk (LS-2
// Wave Task T3d; ls2-task-3d-report.md). shop.cpp has ZERO test coverage of
// any kind before this rider -- no src/tests/*.cpp names shop_keeper,
// closing_time, opening_time, has_already, shopping_buy/sell/list/value, or
// boot_the_shops. closing_time's walk (shop.cpp:106) is Family A
// (save-next mutating walk): only its `.people` init read converts to
// room_of(keeper)->people; the walk structure and the next_patron save-next
// line stay raw because the body relocates the current node out of the
// walked chain via char_from_room()/char_to_room() -- exactly the kind of
// chain-relocating loop this test proves still works correctly with no
// safety net.

#include "../comm.h"
#include "../handler.h"
#include "../interpre.h"
#include "../utils.h"
#include "rots/core/character.h"
#include "rots/core/room.h"
#include "rots/core/types.h"
#include "test_world.h"

#include <gtest/gtest.h>

// closing_time() has no header declaration anywhere in the tree (a
// shop.cpp-internal helper, only called from shop_keeper() itself) --
// forward-declared here with its real signature.
void closing_time(struct char_data *keeper);

namespace {

// A shopkeeper and two patrons sharing room 0 of a fresh two-room test
// world, with a single open (not EX_CLOSED), non-NOWHERE door exit at
// NORTH leading to room 1 -- closing_time()'s own two exit-scan loops
// (shop.cpp:84-96) require exactly this shape to reach the eviction walk
// at all: a door exit that is NOT closed (so the first loop's `break`
// fires) and no non-door exit anywhere (so the second loop's `tmp` reaches
// NUM_OF_DIRS untouched).
struct ClosingTimeContext {
    static constexpr int door_direction = NORTH;

    ScopedTestWorld test_world{2};
    room_direction_data exit_to_room1{};
    char_data keeper{};
    char_data patron_one{};
    char_data patron_two{};
    char_data *original_people = nullptr;

    ClosingTimeContext() {
        for (int room = 0; room < 2; ++room) {
            world[room].room_flags = 0;
            world[room].sector_type = 0;
            world[room].people = nullptr;
            for (int dir = 0; dir < NUM_OF_DIRS; ++dir) {
                world[room].dir_option[dir] = nullptr;
            }
        }

        // Not EX_CLOSED, valid to_room -- satisfies the first exit-scan
        // loop's break condition at dir == door_direction. A non-null
        // keyword is mandatory (not just descriptive): closing_time()'s
        // OWN trailing exit-scan loop (shop.cpp:116-120, after the patron
        // walk this test targets) unconditionally calls
        // do_close(keeper, EXIT(keeper, tmp)->keyword, ...) for every
        // EX_ISDOOR exit, and do_close's half_chop() dereferences that
        // pointer unconditionally -- a null keyword segfaults there
        // (verified via lldb backtrace during this rider's own
        // development), matching act_format_tests.cpp's DoorContext's own
        // `const_cast<char*>("gate")` convention.
        exit_to_room1.exit_info = EX_ISDOOR;
        exit_to_room1.to_room = 1;
        exit_to_room1.keyword = const_cast<char *>("door");
        world[0].dir_option[door_direction] = &exit_to_room1;

        original_people = test_world.room().people;

        keeper.in_room = 0;
        patron_one.in_room = 0;
        patron_two.in_room = 0;
        keeper.next_in_room = &patron_one;
        patron_one.next_in_room = &patron_two;
        patron_two.next_in_room = nullptr;
        test_world.room().people = &keeper;

        // player.race stays at its value-initialized default (0): neither
        // RACE_GOOD nor RACE_EVIL of GET_RACE(ch)==0 is true, so
        // char_from_room()/char_to_room()'s zone-power bookkeeping
        // (zone_by_id(r->zone)->{white,dark}_power) is skipped entirely --
        // this fixture has no zone_table, so reaching that branch would
        // dereference zone_by_id()'s nullptr-on-invalid-zone result.
    }

    ~ClosingTimeContext() {
        test_world.room().people = original_people;
        world[0].dir_option[door_direction] = nullptr;
        world[1].people = nullptr;
        keeper.next_in_room = nullptr;
        patron_one.next_in_room = nullptr;
        patron_two.next_in_room = nullptr;
        keeper.in_room = NOWHERE;
        patron_one.in_room = NOWHERE;
        patron_two.in_room = NOWHERE;
    }
};

} // namespace

TEST(ClosingTime, EvictsBothPatronsThroughTheDoorButLeavesTheKeeperBehind) {
    ClosingTimeContext context;

    closing_time(&context.keeper);

    EXPECT_EQ(location_of(&context.keeper), 0)
        << "Expected the keeper (tmpch == keeper) to be skipped by the walk's own filter and "
           "stay in room 0.";
    EXPECT_EQ(location_of(&context.patron_one), 1)
        << "Expected the converted room_of(keeper)->people init read's walk to reach patron_one "
           "and relocate it through the door to room 1.";
    EXPECT_EQ(location_of(&context.patron_two), 1)
        << "Expected the save-next line (shop.cpp:107, left raw) to correctly preserve the walk "
           "across patron_one's mid-loop char_from_room()/char_to_room() relocation, reaching "
           "patron_two too.";
}
