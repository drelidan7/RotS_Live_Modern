// Coverage rider for act_obj1.cpp's location-read conversions (LS-2 Wave
// Task T3a, commit 9915927; ls2-task-3a-review.md IM-3 -- "commit 9915927
// converted 21 sites across 13 previously-untested live functions with
// ZERO riders", and the census's own waiver was false: the boot golden
// never executes do_get/do_butcher/perform_drop_gold et al., so it
// witnesses compilation, not the conversions). This follow-up (LS-2 T4)
// adds targeted coverage, prioritising the sites where the conversion
// changed a world[] resolve feeding a WALK (get_from_room's FIND_ALL loop,
// do_get's container-scan loop) -- the class of site where a wrong
// resolver or a bad walk would be silent.
//
// load_scalp() is deliberately NOT covered here: re-reading its diff,
// the only token IM-3/the census counted there is `scalp->in_room =
// NOWHERE` (act_obj1.cpp:848), a WRITE that was already annotated
// `// LS1-ALLOW: obj-location` and never converted -- LS-2 is a reads-only
// wave (ls2-global-constraints.md), so this function carries no
// location-READ conversion to regress in the first place, despite being
// one of the "13 previously-untested" functions the review counted.

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

// None of these five have a header declaration anywhere in the tree
// (act_obj1.cpp's own internal helpers/ACMD bodies) -- forward-declared
// here, mirroring act_offe_tests.cpp's `ACMD(do_rescue);` precedent.
int perform_get_from_room(char_data *character, obj_data *item);
void get_from_room(struct char_data *ch, char *arg);
ACMD(do_get);
void perform_drop_gold(struct char_data *ch, int amount, sh_int);
ACMD(do_butcher);

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

// A single player (ch) alone in a fresh one-room test world, with enough
// carry capacity/visibility for every function this file drives.
struct ActObj1Context {
    ScopedTestWorld test_world{1};
    char_data ch{};
    descriptor_data ch_descriptor{};
    char_data *original_people = nullptr;
    // Site 6 (LS-2 whole-branch review B1): world[0].light is never reset by
    // ScopedTestWorld's reuse branch -- saved here, restored in the dtor
    // below, so it doesn't leak into a later test sharing this process's
    // world[0].
    byte original_light = 0;

    ActObj1Context() {
        original_light = world[0].light;
        world[0].light = 1; // CAN_SEE()'s/CAN_SEE_OBJ()'s darkness check.
        original_people = world[0].people;

        reset_capturing_descriptor(ch_descriptor, &ch);
        ch.desc = &ch_descriptor;
        ch.in_room = 0;
        ch.next_in_room = nullptr;
        world[0].people = &ch;

        ch.player.race = RACE_HUMAN;
        ch.player.name = const_cast<char *>("Getter");
        ch.player.level = 20;
        ch.tmpabilities.str = 18; // CAN_CARRY_W headroom.
        ch.tmpabilities.dex = 18; // CAN_CARRY_N headroom.
        ch.specials.position = POSITION_STANDING;
    }

    ~ActObj1Context() {
        world[0].light = original_light;
        world[0].people = original_people;
        world[0].contents = nullptr;
        ch.next_in_room = nullptr;
        ch.in_room = NOWHERE;
        ch.carrying = nullptr;
    }

    std::string_view output() const { return ch_descriptor.small_outbuf; }
};

// A plain, takeable, unnamed-type object -- item_number == -1 so
// perform_get_from_room()'s OBJ-log ternary (`(item->item_number >= 0) ?
// obj_index[item->item_number].virt : -1`) never indexes obj_index
// (unpopulated in this test binary).
obj_data make_takeable_object(const char *name, const char *short_description) {
    obj_data item{};
    item.item_number = -1;
    item.name = const_cast<char *>(name);
    item.short_description = const_cast<char *>(short_description);
    item.obj_flags.wear_flags = ITEM_TAKE;
    item.obj_flags.weight = 5;
    return item;
}

} // namespace

// perform_get_from_room() -- the OBJ-log line (:303) reads room_of(character)
// for the room's name/number; item.touched == 1 and a non-FOOD/DRINKCON type
// (both true by default here) are what gate that line, so this test's own
// setup already reaches the converted read every time it runs.
TEST(PerformGetFromRoom, MovesTheItemFromTheRoomIntoTheCharactersInventory) {
    ActObj1Context context;
    obj_data item = make_takeable_object("sword blade", "a plain sword");
    item.touched = 1;
    item.in_room = 0;
    item.next_content = nullptr;
    world[0].contents = &item;

    const int result = perform_get_from_room(&context.ch, &item);

    EXPECT_EQ(result, 1);
    EXPECT_EQ(context.ch.carrying, &item)
        << "Expected can_take_obj()'s pass to route through obj_from_room()/obj_to_char().";
    EXPECT_EQ(world[0].contents, nullptr)
        << "Expected the item to have left the room's contents list.";
}

// get_from_room()'s FIND_ALL branch (:324) -- the walk this task's brief
// calls out as the highest-priority class ("a walk where a bad conversion
// would be silent"). Two objects in the room; both must be picked up.
TEST(GetFromRoom, WithAllArgumentPicksUpEveryVisibleObjectInTheRoom) {
    ActObj1Context context;
    obj_data first = make_takeable_object("dagger blade", "a dagger");
    obj_data second = make_takeable_object("pouch leather", "a pouch");
    first.in_room = 0;
    second.in_room = 0;
    first.next_content = &second;
    second.next_content = nullptr;
    world[0].contents = &first;

    get_from_room(&context.ch, mutable_arg("all"));

    EXPECT_EQ(world[0].contents, nullptr) << "Expected the room's converted room_of(ch)->contents "
                                             "walk to reach and pick up every object.";
    bool has_first = false, has_second = false;
    for (obj_data *carried = context.ch.carrying; carried; carried = carried->next_content) {
        has_first |= (carried == &first);
        has_second |= (carried == &second);
    }
    EXPECT_TRUE(has_first);
    EXPECT_TRUE(has_second);
}

TEST(GetFromRoom, ReportsNothingHereWhenTheRoomIsEmpty) {
    ActObj1Context context;
    world[0].contents = nullptr;

    get_from_room(&context.ch, mutable_arg("all"));

    EXPECT_NE(context.output().find("doesn't seem to be anything here"), std::string_view::npos)
        << "Expected the empty room_of(ch)->contents walk (zero iterations) to leave `found` at "
           "its initial 0: "
        << context.output();
}

// do_get()'s container-scanning walk (:380, `for (cont = room_of(ch)->contents; ...
// if (CAN_SEE_OBJ(ch, cont) && GET_ITEM_TYPE(cont) == ITEM_CONTAINER) { found = 1; ... }`)
// -- "get <item> all" (arg2 == "all") is the syntax that reaches it.
TEST(DoGet, ReportsNoContainersWhenNoneArePresentInTheRoom) {
    ActObj1Context context;
    obj_data non_container = make_takeable_object("rock granite", "a rock");
    non_container.obj_flags.type_flag = ITEM_TRASH; // present, visible, but NOT a container.
    non_container.in_room = 0;
    non_container.next_content = nullptr;
    world[0].contents = &non_container;

    do_get(&context.ch, mutable_arg("sword all"), nullptr, 0, 0);

    EXPECT_NE(context.output().find("can't seem to find any containers"), std::string_view::npos)
        << "Expected the walk to visit the non-container object, correctly filter it out via "
           "GET_ITEM_TYPE(cont) == ITEM_CONTAINER, and leave `found` at 0: "
        << context.output();
}

TEST(DoGet, FindsAContainerAmongRoomContentsAndStopsReportingNoContainers) {
    ActObj1Context context;
    obj_data container = make_takeable_object("chest wooden", "a wooden chest");
    container.obj_flags.type_flag = ITEM_CONTAINER;
    container.in_room = 0;
    container.next_content = nullptr;
    world[0].contents = &container;

    do_get(&context.ch, mutable_arg("sword all"), nullptr, 0, 0);

    EXPECT_EQ(context.output().find("can't seem to find any containers"), std::string_view::npos)
        << "Expected the converted room_of(ch)->contents walk to find the container (setting "
           "found=1) and route into get_from_container() instead of the no-containers message: "
        << context.output();
}

// perform_drop_gold() (:449, `obj_to_room(obj, location_of(ch))`).
TEST(PerformDropGold, CreatesTheGoldObjectInTheDroppersOwnRoom) {
    ActObj1Context context;
    context.ch.points.gold = 100;
    world[0].contents = nullptr;

    perform_drop_gold(&context.ch, 25, 0);

    ASSERT_NE(world[0].contents, nullptr)
        << "Expected obj_to_room(obj, location_of(ch)) to place the new money object into the "
           "dropper's own room's contents list.";
    EXPECT_EQ(GET_ITEM_TYPE(world[0].contents), ITEM_MONEY);
    EXPECT_EQ(world[0].contents->obj_flags.value[0], 25);
    EXPECT_EQ(context.ch.points.gold, 75);

    // O-I1 (LS-2 whole-branch review, Opus): create_money() (object_utils.cpp)
    // splices the new heap obj_data into the process-global object_list;
    // ActObj1Context's dtor unconditionally nulls world[0].contents, which
    // would orphan this object from the room while object_list still holds
    // it -- arming a nullptr deref in obj_from_room()'s unchecked search
    // cursor the next time anything walks object_list (e.g. `do_purge
    // zone`). extract_obj() removes it from the room's contents list, from
    // object_list, and frees it (item_number == -1 here, so its
    // obj_index_by_id() decrement is correctly skipped, same as every other
    // object this file constructs).
    extract_obj(world[0].contents);
}

// do_butcher()'s corpse-search resolver (:668, `get_obj_in_list_vis(ch,
// "corpse", room_of(ch)->contents, 9999)`, the no-argument path).
TEST(DoButcher, ReportsNoCorpseWhenNoneIsPresentInTheRoom) {
    ActObj1Context context;
    world[0].contents = nullptr;

    do_butcher(&context.ch, mutable_arg(""), nullptr, 0, SCMD_BUTCHER);

    EXPECT_NE(context.output().find("You see no corpse here"), std::string_view::npos)
        << "Expected the converted room_of(ch)->contents resolver to find nothing in an empty "
           "room: "
        << context.output();
}
