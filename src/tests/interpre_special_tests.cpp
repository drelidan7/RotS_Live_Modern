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
//
// RR Wave R3 Task 1b (owner ruling R3-O-1) adds the DispatchInvariant suites
// at the end of this file: one unplaced/placed pair for each of interpre.cpp's
// three registered dispatch entry points -- command_interpreter (the ACMD
// table dispatch), activate_char_special and activate_obj_special (the two
// SPECIAL fn-ptr invokers). Each pair drives the real entry point with an
// actor at NOWHERE and asserts the dispatched body did NOT run, then drives
// the identical fixture with the actor placed in a ScopedTestWorld room and
// asserts it DID. The stubs and index fixtures those tests use are the ones
// this file already owns.

#include "../db.h"
#include "../interpre.h"
#include "../utils.h"
#include "rots/core/character.h"
#include "rots/core/descriptor.h"
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
// activate_obj_special() has no header declaration anywhere in the tree (its
// sibling activate_char_special() is declared in interpre.h:139, this one is
// not) -- forward-declared here for RR Wave R3 Task 1b's dispatch-invariant
// pair below, mirroring act_othe_tests.cpp's `ACMD(do_knock);` precedent for
// the same gap.
int activate_obj_special(struct obj_data *host, struct char_data *ch, int cmd, char *arg,
                         int callflag, struct waiting_type *wtl);
// interpre.cpp's command table is populated at boot by assign_command_pointers();
// nothing in gtest_main.cpp calls it, so the CommandInterpreterDispatchInvariant
// pair below drives it itself (color_tests.cpp:375 established the precedent --
// the call is idempotent and writes only the process-global cmd_info[]).
void assign_command_pointers(void);

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

// RR Wave R3 Task 1d (ruling R3-C-7). A room spec-proc that MOVES the actor
// out of the world and then declines the event (returns 0, so special() and
// command_interpreter both carry on). It stands in for the whole class of
// things special() can fan out to between command_interpreter's entry guard
// and its ACMD dispatch -- room functs, mob spec procs, object spec procs,
// and anything they call in turn -- none of which promises to leave `ch`
// placed on return. Nothing in the tree registers a funct like this today;
// it exists to make the pre-dispatch tripwire's job observable.
SPECIAL(relocating_room_funct) {
    g_room_funct_call.called = true;
    set_location(ch, NOWHERE);
    return 0;
}

// Mirrors act_othe_tests.cpp:45 / comm_act_tests.cpp (duplicated, not shared --
// each copy lives in a different TU's anonymous namespace). Needed by the
// Task 1c dominance test below, which reads what command_interpreter did or
// did not send back to the actor.
void reset_capturing_descriptor(descriptor_data& descriptor, char_data* character) {
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = 0; // CON_PLYNG
    descriptor.character = character;
}

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

// ---------------------------------------------------------------------------
// RR Wave R3 Task 1b dispatch-invariant guards (owner ruling R3-O-1;
// docs/superpowers/specs/2026-08-21-rr3-combat-design.md section 2). Each pair
// below drives one of interpre.cpp's three registered dispatch entry points
// twice with an otherwise identical fixture, differing ONLY in whether the
// ACTOR the ledger's dispatch-entry registry names has a location. Unplaced ->
// the dispatched body must not run; placed -> it must. Every guard is expected
// to be unreachable on a live path (dispatch census P7), so these tests are the
// only thing in the tree that reaches them.
// ---------------------------------------------------------------------------

TEST(CommandInterpreterDispatchInvariant, RefusesToDispatchAnAcmdForAnUnplacedActor) {
    // command_interpreter checks position, never placement, and the two
    // special() calls it makes before dispatching contribute FALSE to
    // may_not_perform when they reject an absent character -- so before the
    // guard this reached the real do_stand body (dispatch census M-1).
    ScopedTestWorld test_world;
    assign_command_pointers();

    char_data ch{};
    set_location(&ch, NOWHERE);
    ch.specials.position = POSITION_SITTING;

    char line[] = "stand";
    command_interpreter(&ch, line, nullptr);

    EXPECT_EQ(GET_POS(&ch), POSITION_SITTING)
        << "Expected the dispatch-invariant guard to refuse the ACMD dispatch for an actor with "
           "no location -- do_stand must never have run.";
}

TEST(CommandInterpreterDispatchInvariant, DispatchesTheAcmdForAPlacedActor) {
    ScopedTestWorld test_world;
    assign_command_pointers();

    char_data ch{};
    set_location(&ch, 0);
    ch.specials.position = POSITION_SITTING;

    char line[] = "stand";
    command_interpreter(&ch, line, nullptr);

    EXPECT_EQ(GET_POS(&ch), POSITION_STANDING)
        << "Expected the identical fixture with a PLACED actor to reach the real do_stand body "
           "and stand the character up -- the guard must not block a normal dispatch.";
}

TEST(CommandInterpreterDispatchInvariant, RefusesBeforeTheTargetParserResolvesTheActorsRoom) {
    // RR Wave R3 Task 1c (T3d finding O-2). The pair above proves the guard
    // stops the ACMD DISPATCH; this one proves it also dominates
    // `target_parser(ch, ...)`, which runs ~40 lines earlier in the same block
    // and resolves rooms from the same actor. Task 1b's placement did not:
    // `target_from_word` reached `get_obj_in_list(word, room_of(ch)->contents)`
    // (visibility.cpp:1123) with an unplaced `ch`, silently degrading to
    // world[0] (db_world.cpp:2082's negative-index arm), while the guard sat
    // downstream doing nothing about it.
    //
    // `pull` (cmd 178, interpre.cpp's COMMANDO(178, ...)) is the discriminator
    // because its ENTIRE first-target mask is TAR_OBJ_ROOM. With that one bit
    // set and a non-empty argument, `target_from_word` has no branch that can
    // return before the room read -- so reaching `report_wrong_target`'s
    // TAR_OBJ_ROOM arm ("Nothing here by that name.") is a WITNESS that
    // `room_of(ch)->contents` was consulted, not merely that the parse ran.
    // Room 0 holds no object, so the parse fails and nothing is dispatched in
    // either half; the only difference between the two runs is whether the
    // room was read at all.
    ScopedTestWorld test_world;
    assign_command_pointers();

    char_data ch{};
    descriptor_data descriptor{};
    reset_capturing_descriptor(descriptor, &ch);
    ch.desc = &descriptor;
    ch.specials.position = POSITION_STANDING;

    set_location(&ch, NOWHERE);
    char unplaced_line[] = "pull lever";
    command_interpreter(&ch, unplaced_line, nullptr);

    EXPECT_STREQ(descriptor.output, "")
        << "Expected the dispatch-invariant guard to refuse BEFORE target_parser ran, so "
           "target_from_word never resolved room_of(ch) and never reported a wrong target.";

    reset_capturing_descriptor(descriptor, &ch);
    set_location(&ch, 0);
    char placed_line[] = "pull lever";
    command_interpreter(&ch, placed_line, nullptr);

    EXPECT_STREQ(descriptor.output, "Nothing here by that name.\n\r")
        << "Expected the identical fixture with a PLACED actor to run target_parser, read "
           "room_of(ch)->contents, find no `lever` there, and say so -- the guard must not block "
           "a normal target parse.";

    set_location(&ch, NOWHERE);
    ch.desc = nullptr;
}

TEST(CommandInterpreterDispatchInvariant, RefusesTheAcmdWhenASpecProcRelocatedTheActorAfterTheEntryGuard) {
    // RR Wave R3 Task 1d (coordinator ruling R3-C-7), the ADJACENCY test. The
    // three tests above all enter command_interpreter with the actor already
    // unplaced, so every one of them is satisfied by the entry guard at
    // interpre.cpp:1119 alone. This one enters PLACED -- the entry guard
    // passes, exactly as it does on every live command -- and then has the
    // room's spec-proc unplace the actor from inside special()'s fan-out,
    // which runs between that guard and the ACMD dispatch. Only the second
    // tripwire, the one sitting immediately before the dispatch, can refuse
    // here.
    //
    // Discriminator: with the pre-dispatch guard deleted and :1119 left in
    // place, the first half below reaches the real do_stand body with an
    // actor at NOWHERE and this test goes RED, while the other three stay
    // green. That is the property R3-C-7 asks for and nothing else in this
    // file tests.
    ScopedTestWorld test_world;
    assign_command_pointers();

    char_data ch{};
    set_location(&ch, 0);
    ch.specials.position = POSITION_SITTING;

    g_room_funct_call = RecordedCall{};
    test_world.room().funct = &relocating_room_funct;

    char relocated_line[] = "stand";
    command_interpreter(&ch, relocated_line, nullptr);

    EXPECT_TRUE(g_room_funct_call.called)
        << "The fixture is only meaningful if special()'s room-funct dispatch actually ran "
           "between the entry guard and the ACMD dispatch.";
    EXPECT_EQ(location_of(&ch), NOWHERE)
        << "The stub must really have unplaced the actor -- otherwise the pre-dispatch guard "
           "has nothing to refuse and this test proves nothing.";
    EXPECT_EQ(GET_POS(&ch), POSITION_SITTING)
        << "Expected the pre-dispatch dispatch-invariant guard (interpre.cpp:1175) to refuse "
           "the ACMD dispatch for an actor a spec-proc unplaced AFTER the entry guard at :1119 "
           "already passed -- do_stand must never have run.";

    // The control: the identical fixture whose spec-proc leaves the actor
    // where it is dispatches normally. The difference between the two halves
    // is the relocation, not the presence of a room funct.
    set_location(&ch, 0);
    ch.specials.position = POSITION_SITTING;
    g_room_funct_call = RecordedCall{};
    test_world.room().funct = &recording_room_funct;

    char placed_line[] = "stand";
    command_interpreter(&ch, placed_line, nullptr);

    EXPECT_TRUE(g_room_funct_call.called);
    EXPECT_EQ(GET_POS(&ch), POSITION_STANDING)
        << "Expected a non-relocating spec-proc to leave the dispatch alone -- the guard must "
           "not block a normal command.";

    test_world.room().funct = nullptr;
    set_location(&ch, NOWHERE);
}

TEST(ActivateCharSpecialDispatchInvariant, RefusesToDispatchForAnUnplacedActor) {
    // The registry's actor for this entry point is `victim` (parameter 2), NOT
    // `character` (parameter 1, the spec-proc HOST): special() calls it as
    // activate_char_special(tmpch, ch, ...) at interpre.cpp:1298/:1324. The
    // host below is therefore placed in both tests; only the victim moves.
    ScopedTestWorld test_world;
    ScopedRecordingMobIndex mob_index_scope;

    char_data host{};
    host.specials2.act = MOB_ISNPC | MOB_SPEC;
    host.nr = 0; // indexes mob_index_scope's single fabricated entry
    set_location(&host, 0);

    char_data victim{};
    set_location(&victim, NOWHERE);

    g_mob_spec_call = RecordedCall{};
    waiting_type wtl{};
    const int result =
        activate_char_special(&host, &victim, 0, mutable_arg(""), SPECIAL_SELF, &wtl, 0);

    EXPECT_FALSE(g_mob_spec_call.called)
        << "Expected the dispatch-invariant guard to refuse the SPECIAL dispatch when the ACTOR "
           "(victim) has no location.";
    EXPECT_EQ(result, 0) << "A refused dispatch reports 'nothing consumed the event'.";
}

TEST(ActivateCharSpecialDispatchInvariant, DispatchesForAPlacedActor) {
    ScopedTestWorld test_world;
    ScopedRecordingMobIndex mob_index_scope;

    char_data host{};
    host.specials2.act = MOB_ISNPC | MOB_SPEC;
    host.nr = 0;
    set_location(&host, 0);

    char_data victim{};
    set_location(&victim, 0);

    g_mob_spec_call = RecordedCall{};
    waiting_type wtl{};
    const int result =
        activate_char_special(&host, &victim, 0, mutable_arg(""), SPECIAL_SELF, &wtl, 0);

    EXPECT_TRUE(g_mob_spec_call.called)
        << "Expected the identical fixture with a PLACED actor to reach the host's registered "
           "spec-proc.";
    EXPECT_EQ(result, 1) << "The recording stub consumes the event, so the call must return 1.";
}

TEST(ActivateObjSpecialDispatchInvariant, RefusesToDispatchForAnUnplacedActor) {
    ScopedTestWorld test_world;
    ScopedRecordingObjIndex obj_index_scope;

    obj_data host{};
    host.item_number = 0; // indexes obj_index_scope's single fabricated entry

    char_data actor{};
    set_location(&actor, NOWHERE);

    g_obj_spec_call = RecordedCall{};
    waiting_type wtl{};
    const int result =
        activate_obj_special(&host, &actor, 0, mutable_arg(""), SPECIAL_SELF, &wtl);

    EXPECT_FALSE(g_obj_spec_call.called)
        << "Expected the dispatch-invariant guard to refuse the object-SPECIAL dispatch when the "
           "ACTOR (ch) has no location -- `host` is the object, not the actor.";
    EXPECT_EQ(result, 0);
}

TEST(ActivateObjSpecialDispatchInvariant, DispatchesForAPlacedActor) {
    ScopedTestWorld test_world;
    ScopedRecordingObjIndex obj_index_scope;

    obj_data host{};
    host.item_number = 0;

    char_data actor{};
    set_location(&actor, 0);

    g_obj_spec_call = RecordedCall{};
    waiting_type wtl{};
    const int result =
        activate_obj_special(&host, &actor, 0, mutable_arg(""), SPECIAL_SELF, &wtl);

    EXPECT_TRUE(g_obj_spec_call.called)
        << "Expected the identical fixture with a PLACED actor to reach the object's registered "
           "spec-proc.";
    EXPECT_EQ(result, 1) << "The recording stub consumes the event, so the call must return 1.";
}
