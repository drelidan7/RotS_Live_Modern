// Coverage rider for act_obj2.cpp's location-read conversions (LS-2 Wave
// Task T3a, commit 9915927; ls2-task-3a-review.md IM-3 -- see
// act_obj1_tests.cpp's file banner for the full background). do_light()/
// do_blowout() were the reviewer's own named "cheapest closure" candidates
// (a torch found via the converted room_of(ch)->contents resolver, plus
// the two kept-raw `world[ch->in_room].light++/--` writes); do_drink()'s
// room-flag reads are the other named candidate.

#include "../comm.h"
#include "../handler.h"
#include "../interpre.h"
#include "../utils.h"
#include "rots/core/character.h"
#include "rots/core/descriptor.h"
#include "rots/core/object.h"
#include "rots/core/types.h"
#include "test_world.h"

#include <gtest/gtest.h>

// None of these six have a header declaration anywhere in the tree
// (act_obj2.cpp's own internal helper/ACMD bodies) -- forward-declared
// here, mirroring act_offe_tests.cpp's `ACMD(do_rescue);` precedent.
void weight_change_object(struct obj_data *obj, int weight);
ACMD(do_drink);
ACMD(do_eat);
ACMD(do_pour);
ACMD(do_light);
ACMD(do_blowout);

// generic_water/generic_poison are process-wide singleton stand-ins for a
// DRINK_WATER/DRINK_POISON room's water supply (spec_ass.cpp); no test
// anywhere in the tree owns a reset fixture for them yet, so
// DoDrinkFromTheRoomsWaterFlag below saves/restores the one field group it
// touches -- the same discipline script_tests.cpp's T4 follow-up uses for
// script_table, or comm.cpp's weather_info save/restore precedent.
extern struct obj_data generic_water;

extern struct room_data world;
extern int top_of_world;

namespace {

// Points a descriptor's output at its own small_outbuf so send_to_char()/
// act() calls land somewhere inspectable instead of a real socket --
// duplicated locally per act_offe_tests.cpp's/comm_act_tests.cpp's own
// established convention (each test file keeps its own copy).
void reset_capturing_descriptor(descriptor_data &descriptor, char_data *character) {
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = 0; // CON_PLYNG
    descriptor.character = character;
}

// A single player (ch) alone in a fresh one-room test world.
struct ActObj2Context {
    ScopedTestWorld test_world{1};
    char_data ch{};
    descriptor_data ch_descriptor{};
    char_data *original_people = nullptr;

    ActObj2Context() {
        world[0].light = 1; // CAN_SEE()'s/CAN_SEE_OBJ()'s darkness check.
        world[0].room_flags = 0;
        original_people = world[0].people;

        reset_capturing_descriptor(ch_descriptor, &ch);
        ch.desc = &ch_descriptor;
        ch.in_room = 0;
        ch.next_in_room = nullptr;
        world[0].people = &ch;

        ch.player.race = RACE_HUMAN;
        ch.player.name = const_cast<char *>("Drinker");
        ch.player.level = 20;
        ch.specials.position = POSITION_STANDING;
    }

    ~ActObj2Context() {
        world[0].people = original_people;
        world[0].contents = nullptr;
        world[0].room_flags = 0;
        ch.next_in_room = nullptr;
        ch.in_room = NOWHERE;
        ch.carrying = nullptr;
    }

    std::string_view output() const { return ch_descriptor.small_outbuf; }
};

obj_data make_light_source(const char *name, const char *short_description) {
    obj_data item{};
    item.item_number = -1;
    item.name = const_cast<char *>(name);
    item.short_description = const_cast<char *>(short_description);
    item.obj_flags.wear_flags = ITEM_TAKE;
    item.obj_flags.type_flag = ITEM_LIGHT;
    item.obj_flags.value[2] = 24; // hours of fuel remaining -- nonzero, so do_light() proceeds.
    return item;
}

} // namespace

// weight_change_object() (:43-56) -- `obj->in_room != NOWHERE` selects the
// direct room-weight-adjustment branch over the carried_by/in_obj branches.
TEST(WeightChangeObject, AdjustsWeightDirectlyWhenTheObjectIsInARoom) {
    obj_data item{};
    item.item_number = -1;
    item.in_room = 0; // != NOWHERE.
    item.carried_by = nullptr;
    item.in_obj = nullptr;
    item.obj_flags.weight = 10;

    weight_change_object(&item, 5);

    EXPECT_EQ(GET_OBJ_WEIGHT(&item), 15)
        << "Expected the `obj->in_room != NOWHERE` branch (not the carried_by/in_obj branches, "
           "neither of which is wired up on this fixture) to have fired.";
}

// do_drink()'s room-flag reads (:106-109, `IS_SET(room_of(ch)->room_flags, "
// DRINK_WATER | DRINK_POISON)`) plus the object-search fallback (:118,
// `get_obj_in_list_vis(ch, arg, room_of(ch)->contents, 9999)`).
TEST(DoDrink, ReportsCannotFindWhenNoWaterFlagAndNoObjectAnywhere) {
    ActObj2Context context;
    world[0].room_flags = 0; // no DRINK_WATER/DRINK_POISON.
    world[0].contents = nullptr;

    do_drink(&context.ch, mutable_arg("water"), nullptr, 0, SCMD_DRINK);

    EXPECT_NE(context.output().find("You can't find it!"), std::string_view::npos)
        << "Expected the room-flag read to see neither bit set, then the room_of(ch)->contents "
           "search (carrying is also empty) to find nothing: "
        << context.output();
}

TEST(DoDrink, DrinksGenericWaterWhenTheRoomHasTheDrinkWaterFlag) {
    ActObj2Context context;
    world[0].room_flags = DRINK_WATER;

    // generic_water is a process-wide singleton this test does not own --
    // snapshot and restore the whole object around the call.
    const obj_data saved_generic_water = generic_water;
    generic_water.obj_flags.type_flag = ITEM_DRINKCON; // :127's type check.
    generic_water.obj_flags.value[1] = 10; // amount available (:149's "It's empty" guard).
    generic_water.obj_flags.value[2] = 0;  // drinks[0]/drink_aff[0] == "water".
    generic_water.obj_flags.value[3] = 0;  // not poisoned.

    do_drink(&context.ch, mutable_arg("water"), nullptr, 0, SCMD_DRINK);

    EXPECT_NE(context.output().find("You drink the water"), std::string_view::npos)
        << "Expected the room-flag read (room_of(ch)->room_flags, DRINK_WATER set) to route "
           "into the generic_water branch instead of the object-search fallback: "
        << context.output();

    generic_water = saved_generic_water;
}

// do_eat()'s object-search fallback (:237, same shape as do_drink's :118).
TEST(DoEat, ReportsNothingToEatWhenNoFoodIsAnywhere) {
    ActObj2Context context;
    world[0].contents = nullptr;

    do_eat(&context.ch, mutable_arg("bread"), nullptr, 0, SCMD_EAT);

    EXPECT_NE(context.output().find("don't seem to have any"), std::string_view::npos)
        << "Expected the converted room_of(ch)->contents search (carrying is also empty) to find "
           "nothing: "
        << context.output();
}

// do_pour()'s room-flag read (:321) plus object-search fallback (:329) --
// same shape as do_drink's, exercised on a different subcmd/function.
TEST(DoPour, ReportsCannotFindWhenNoWaterFlagAndNoObjectAnywhere) {
    ActObj2Context context;
    world[0].room_flags = 0;
    world[0].contents = nullptr;

    do_pour(&context.ch, mutable_arg("water"), nullptr, 0, SCMD_POUR);

    EXPECT_NE(context.output().find("You can't find it!"), std::string_view::npos)
        << "Expected the room-flag read to see neither bit set, then the room_of(ch)->contents "
           "search to find nothing: "
        << context.output();
}

// do_light() (:694/:704/:726) -- torch found via the converted
// room_of(ch)->contents resolver; the write this task deliberately leaves
// raw (`world[ch->in_room].light++`) is exercised as a side effect.
TEST(DoLight, LightsATorchFoundInTheRoomAndIncrementsRoomLight) {
    ActObj2Context context;
    obj_data torch = make_light_source("torch wooden", "a wooden torch");
    torch.in_room = 0;
    torch.next_content = nullptr;
    world[0].contents = &torch;
    const int light_before = world[0].light;

    do_light(&context.ch, mutable_arg("torch"), nullptr, 0, 0);

    EXPECT_EQ(torch.obj_flags.value[3], 1) << "Expected the torch to end up lit.";
    EXPECT_EQ(world[0].light, light_before + 1)
        << "Expected the kept-raw world[ch->in_room].light++ write to fire once the converted "
           "resolver found the torch.";
}

TEST(DoLight, ReportsNothingToLightWhenNoTorchIsPresent) {
    ActObj2Context context;
    world[0].contents = nullptr;

    do_light(&context.ch, mutable_arg("torch"), nullptr, 0, 0);

    EXPECT_NE(context.output().find("What do you want to light"), std::string_view::npos)
        << "Expected the converted room_of(ch)->contents search (equipment[WEAR_LIGHT] is also "
           "empty) to find nothing: "
        << context.output();
}

// do_blowout() (:736/:752/:769) -- the reverse of do_light(), same resolver
// shape, mirrored write.
TEST(DoBlowout, ExtinguishesALitTorchFoundInTheRoomAndDecrementsRoomLight) {
    ActObj2Context context;
    obj_data torch = make_light_source("torch wooden", "a wooden torch");
    torch.obj_flags.value[3] = 1; // already lit.
    torch.in_room = 0;
    torch.next_content = nullptr;
    world[0].contents = &torch;
    world[0].light = 1; // matches the lit torch (do_light() would have incremented it).

    do_blowout(&context.ch, mutable_arg("torch"), nullptr, 0, 0);

    EXPECT_EQ(torch.obj_flags.value[3], 0) << "Expected the torch to end up unlit.";
    EXPECT_EQ(world[0].light, 0)
        << "Expected the kept-raw world[ch->in_room].light-- write to fire once the converted "
           "resolver found the torch.";
}
