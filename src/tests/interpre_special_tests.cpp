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
#include "rots/core/object.h"
#include "rots/core/types.h"
#include "test_placement.h"
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
// obj_index likewise has no header declaration anywhere in the tree (see the
// mob_index comment above) -- forward-declared for the same reason, needed by
// this file's F8 remote_mode coverage rider (ScopedRecordingObjIndex below).
extern struct index_data *obj_index;

namespace {

struct RecordedCall {
    bool called = false;
};

RecordedCall g_room_funct_call;
RecordedCall g_mob_spec_call;

RecordedCall g_obj_spec_call;

// Carried-item spec-proc stub: records that it fired. Used to prove F8's
// remote_mode gate (interpre.cpp special(), the `if (in_room !=
// location_of(ch))` computation) actually skips the caller's own
// equipment/inventory dispatch -- not merely that nothing else fired.
SPECIAL(recording_obj_spec) {
    g_obj_spec_call.called = true;
    return 1;
}

// Swaps the process-global obj_index for a single fabricated entry whose
// func is this file's recording stub, restoring the prior table on scope
// exit -- mirrors ScopedRecordingMobIndex above, object-side.
class ScopedRecordingObjIndex {
  public:
    ScopedRecordingObjIndex() : previous_(obj_index) {
        obj_index = new index_data[1]{};
        obj_index[0].virt = 200;
        obj_index[0].func = &recording_obj_spec;
    }

    ~ScopedRecordingObjIndex() {
        delete[] obj_index;
        obj_index = previous_;
    }

    ScopedRecordingObjIndex(const ScopedRecordingObjIndex &) = delete;
    ScopedRecordingObjIndex &operator=(const ScopedRecordingObjIndex &) = delete;

  private:
    index_data *previous_;
};

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

    // ch at the head, the spec-bearing mob behind it -- exactly the
    // head-first order this fixture used to publish by hand, and the order
    // rots::entity::occupants(room_by_id_total(in_room)) walks them in
    // (LS-3a T3, test_placement.h). Declared LAST so it unwinds first, before
    // the characters it manages and before the ScopedTestWorld whose room it
    // points into. Its constructor stamps both locations through
    // set_location(), so this fixture no longer writes in_room or
    // next_in_room anywhere.
    ScopedRoomOccupants occupants{&test_world.room(), 0, {&ch, &mob_occupant}};

    SpecialDispatchContext() {
        g_room_funct_call = RecordedCall{};
        g_mob_spec_call = RecordedCall{};

        test_world.room().funct = &recording_room_funct;

        mob_occupant.specials2.act = MOB_ISNPC | MOB_SPEC;
        mob_occupant.nr = 0; // indexes mob_index_scope's single fabricated entry
    }

    // The room's funct reset stays here -- it is not location state, and this
    // suite's fixtures are deliberately restore-everything (it is where the
    // pre-batch-0 cross-suite SIGSEGV lived). The chain-head restore, the two
    // unlinks and the two NOWHERE de-locations are now `occupants`, which
    // unwinds immediately after this body runs.
    ~SpecialDispatchContext() {
        test_world.room().funct = nullptr;
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
    ScopedRoomOccupants occupants{&test_world.room(), 0, {&ch}};
    test_world.room().funct = nullptr;

    waiting_type wtl{};
    const int result = special(&ch, 0, mutable_arg(""), SPECIAL_COMMAND, &wtl);

    EXPECT_EQ(result, 0)
        << "No registered room funct and no matching occupant spec-proc -- special() must "
           "return the tripwire-free 0 (\"nothing consumed the event\").";
}

// ---------------------------------------------------------------------------
// F8 coverage riders (ls3b-global-constraints.md T7; ls3b-census-review.md
// F8): interpre.cpp's special() tests a character's ABSENCE twice against
// the (possibly-substituted) in_room value -- the `remote_mode` computation
// at :1253 and the early `return FALSE` at :1263 -- and neither had a
// dedicated test anywhere in the tree before this rider (the suite above
// only drives special() for a character who HAS a location). Both are
// RETAINED-signature behaviors (interpre.h's own T7 comment), so these pin
// the body's meaning, not a code change.
// ---------------------------------------------------------------------------

TEST(InterpreSpecial, RemoteModeSkipsTheCallersEquipmentAndInventoryForAnAbsentCharacterGivenAnExplicitRoom) {
    // F8's FIRST absence-derived behavior: `if (in_room != location_of(ch))
    // remote_mode = 1;` runs BEFORE the NOWHERE-default substitution, so an
    // explicit, non-default room (act_comm.cpp's do_yell zone loop is the
    // one production caller that ever passes one) for an absent character
    // sets remote_mode = 1 -- which then gates OFF the caller's own
    // equipment/inventory spec-proc dispatch (interpre.cpp:1336-1348). Room
    // funct and room occupants stay empty so only the equipment/inventory
    // gate can produce a call.
    ScopedTestWorld test_world;
    ScopedRecordingObjIndex obj_index_scope;
    char_data ch{};
    set_location(&ch, NOWHERE);
    test_world.room().funct = nullptr;

    obj_data carried{};
    carried.item_number = 0;
    carried.next_content = nullptr;
    ch.carrying = &carried;

    g_obj_spec_call = RecordedCall{};

    waiting_type wtl{};
    const int result = special(&ch, 0, mutable_arg(""), SPECIAL_COMMAND, &wtl, 0);

    EXPECT_FALSE(g_obj_spec_call.called)
        << "Expected remote_mode = 1 (explicit room 0 != the absent character's NOWHERE "
           "location) to skip the inventory spec-proc dispatch entirely.";
    EXPECT_EQ(result, 0);

    ch.carrying = nullptr;
}

TEST(InterpreSpecial, AbsentCharacterEarlyReturnsFalseWithoutDispatchingAnything) {
    // F8's SECOND absence-derived behavior: called with the default
    // (unspecified) room for a character with no location at all, special()
    // substitutes in_room = location_of(ch) = NOWHERE and then takes the
    // `if (in_room == NOWHERE) return FALSE;` arm at :1263 -- BEFORE ever
    // resolving room_by_id_total(in_room) or dispatching to the room funct,
    // any occupant, or any object. The room funct is wired to a stub that
    // WOULD fire if the early return did not intercept first.
    ScopedTestWorld test_world;
    char_data ch{};
    set_location(&ch, NOWHERE);
    test_world.room().funct = &recording_room_funct;
    g_room_funct_call = RecordedCall{};

    waiting_type wtl{};
    const int result = special(&ch, 0, mutable_arg(""), SPECIAL_COMMAND, &wtl);

    EXPECT_EQ(result, 0)
        << "Expected an absent character with no explicit room to take the early NOWHERE "
           "return, not fall through to any dispatch.";
    EXPECT_FALSE(g_room_funct_call.called)
        << "The early return must fire strictly before the room-funct dispatch is attempted.";

    test_world.room().funct = nullptr;
}
