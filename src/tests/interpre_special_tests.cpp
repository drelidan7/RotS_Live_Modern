// Coverage rider for interpre.cpp's special() REAL body (LS-2 Wave Task
// T3d; ls2-task-3d-report.md). combat_hooks_tests.cpp's CombatHooksSpecial
// suite only exercises rots::combat::call_special()'s HOOK SEAM against a
// recording stub -- interpre.cpp's own special() (the seam's real
// registered target, register_combat_command_dispatch()) had zero direct
// coverage before this rider, including its two LS-2-converted read sites:
// the room room_by_id_total(in_room)->funct dispatch (:1333/:1334) and the
// rots::entity::occupants(room_by_id_total(in_room)) room-occupant walk
// (:1352, replacing the raw `for (k = world[in_room].people; k; k =
// k->next_in_room)` loop-condition/declaration-deletion pair).

#include "../db.h"
#include "../interpre.h"
#include "../utils.h"
#include "rots/core/character.h"
#include "rots/core/types.h"
#include "test_world.h"

#include <gtest/gtest.h>

// mob_index has no header declaration anywhere in the tree (an app-global
// db.cpp owns) -- forward-declared locally, mirroring
// act_wiz_format_tests.cpp:129 / characterization_combat_tests.cpp:36.
// index_data's full definition (not just a forward-declared struct pointer)
// comes from ../db.h -- needed here because this file allocates/subscripts
// it (`new index_data[1]{}`, `mob_index[0].func`), unlike the two mirrored
// sites which only ever passed the pointer around.
extern struct index_data *mob_index;

namespace {

struct RecordedCall {
    bool called = false;
};

RecordedCall g_room_funct_call;
RecordedCall g_mob_spec_call;

// Room spec-proc stub: records that it fired and returns 0 (does NOT
// consume the event), so special() falls through to the occupant walk
// below it -- proving both converted read sites are reached in one call,
// per the census's own stated rider design ("register a recording
// room_data::funct and a recording mob spec ... assert both fire").
SPECIAL(recording_room_funct) {
    g_room_funct_call.called = true;
    return 0;
}

// Mob spec-proc stub: records that it fired and returns 1 (consumes the
// event), so special()'s occupant walk short-circuits here with a non-zero
// result -- proving the converted rots::entity::occupants() walk actually
// reaches and dispatches to a room occupant, not just iterates past it.
SPECIAL(recording_mob_spec) {
    g_mob_spec_call.called = true;
    return 1;
}

// Swaps the process-global mob_index for a single fabricated entry whose
// func is this file's recording stub, restoring the prior table on scope
// exit -- mirrors act_wiz_format_tests.cpp's ScopedMobIndexEntry, but
// installs a recording (not no-op) stub since this suite asserts dispatch
// actually happened.
class ScopedRecordingMobIndex {
  public:
    ScopedRecordingMobIndex() : previous_(mob_index) {
        mob_index = new index_data[1]{};
        mob_index[0].virt = 100;
        mob_index[0].number = 1;
        mob_index[0].func = &recording_mob_spec;
    }

    ~ScopedRecordingMobIndex() {
        delete[] mob_index;
        mob_index = previous_;
    }

    ScopedRecordingMobIndex(const ScopedRecordingMobIndex &) = delete;
    ScopedRecordingMobIndex &operator=(const ScopedRecordingMobIndex &) = delete;

  private:
    index_data *previous_;
};

// A calling PC (ch) and a spec-bearing mob, both chained into room 0's
// occupant list (ch first, mob second) -- exactly the shape
// rots::entity::occupants(room_by_id_total(in_room)) walks. The room's
// own .funct is wired to the OTHER recording stub above.
struct SpecialDispatchContext {
    ScopedTestWorld test_world;
    ScopedRecordingMobIndex mob_index_scope;
    char_data ch{};
    char_data mob_occupant{};
    char_data *original_people = nullptr;

    SpecialDispatchContext() {
        g_room_funct_call = RecordedCall{};
        g_mob_spec_call = RecordedCall{};

        original_people = test_world.room().people;
        test_world.room().funct = &recording_room_funct;

        ch.in_room = 0;
        mob_occupant.in_room = 0;
        mob_occupant.specials2.act = MOB_ISNPC | MOB_SPEC;
        mob_occupant.nr = 0; // indexes mob_index_scope's single fabricated entry

        ch.next_in_room = &mob_occupant;
        mob_occupant.next_in_room = nullptr;
        test_world.room().people = &ch;
    }

    ~SpecialDispatchContext() {
        test_world.room().people = original_people;
        test_world.room().funct = nullptr;
        ch.next_in_room = nullptr;
        mob_occupant.next_in_room = nullptr;
        ch.in_room = NOWHERE;
        mob_occupant.in_room = NOWHERE;
    }
};

} // namespace

TEST(InterpreSpecial, DispatchesBothTheRoomFunctAndTheRoomOccupantMobSpec) {
    SpecialDispatchContext context;
    waiting_type wtl{};

    const int result = special(&context.ch, 0, mutable_arg(""), SPECIAL_COMMAND, &wtl);

    EXPECT_TRUE(g_room_funct_call.called)
        << "The converted room_by_id_total(in_room)->funct dispatch (interpre.cpp:1333/1334) "
           "must still fire.";
    EXPECT_TRUE(g_mob_spec_call.called)
        << "The converted rots::entity::occupants(room_by_id_total(in_room)) walk "
           "(interpre.cpp:1352) must still reach the room occupant's registered spec-proc.";
    EXPECT_EQ(result, 1) << "The mob spec-proc consumed the event, so special() must return 1.";
}

TEST(InterpreSpecial, ReturnsZeroWhenNeitherTheRoomNorAnyOccupantConsumesTheEvent) {
    ScopedTestWorld test_world;
    char_data ch{};
    ch.in_room = 0;
    ch.next_in_room = nullptr;
    char_data *original_people = test_world.room().people;
    test_world.room().people = &ch;
    test_world.room().funct = nullptr;

    waiting_type wtl{};
    const int result = special(&ch, 0, mutable_arg(""), SPECIAL_COMMAND, &wtl);

    EXPECT_EQ(result, 0)
        << "No registered room funct and no matching occupant spec-proc -- special() must "
           "return the tripwire-free 0 (\"nothing consumed the event\").";

    test_world.room().people = original_people;
    ch.in_room = NOWHERE;
}
