// placement_tests.cpp

// New test TU (placement-seam wave, Task 6; plan
// docs/superpowers/plans/2026-07-19-placement-seam.md; brief
// .superpowers/sdd/task-6-brief.md). Standing coverage-gap rule: Task 3
// carved detach_char_from_room()/char_to_room() (placement.cpp) and
// attach_equipment()/detach_equipment() (equipment.cpp) as new lib-resident
// primitives behind the four-resolver world seam (entity_hooks.h) -- boot
// path code with zero direct prior coverage. This file is a new TU rather
// than growing entity_lifecycle_tests.cpp: that file's existing suite is
// scoped to the txt-block-pool hook pair (a single, unrelated hook family);
// this wave's candidates are a self-contained placement/equipment unit with
// their own RAII hook fixture (world-id resolvers, not txt blocks), so a
// dedicated file keeps each fixture's hook-restore hygiene scoped to the
// tests that actually swap that hook -- same reasoning object_utils_tests.cpp
// used to split off from entity_lifecycle_tests.cpp rather than append.
//
// CRITICAL HYGIENE (mirrors entity_lifecycle_tests.cpp's
// ScopedTxtBlockPoolHooks doc comment): gtest_main.cpp registers
// db_world.cpp's/zone_load.cpp's REAL room/room-total/zone/obj-index
// resolvers as entity_hooks.h's world-resolver hook set for the whole test
// binary (register_world_resolver_hooks(), called once in main()) -- but this
// test process never boots world data, so those real resolvers would dispatch
// into an empty world[]/zone_table. Every test below that needs a resolved
// room_data*/zone_data* swaps in test-owned fixtures via
// ScopedWorldResolverHooks, whose destructor unconditionally calls the real
// register_world_resolver_hooks() again before returning control to the rest
// of the suite -- so no later test in the binary ever observes a stub still
// installed.
//
// NPC-only char_data fixtures for the attach_equipment()/detach_equipment()
// candidates (see make_equipment_test_npc() below): both primitives call
// affect_total(ch) (entity_lifecycle.cpp) after their affect_modify() loop,
// and affect_total()'s AFFECT_TOTAL_REMOVE branch calls recalc_abilities()
// for a non-NPC character -- a much larger derived-stat engine (GET_RAW_SKILL/
// GET_RAW_KNOWLEDGE over ch->skills/ch->knowledge, ch->profs->prof_level,
// class_HP(), get_specialization()) that a bare fixture cannot safely drive
// without populating several unrelated tables (profs/skills/knowledge) purely
// to avoid a null-pointer read -- no assertion in this file needs that
// machinery to run. IS_NPC(ch) routes affect_total() around
// recalc_abilities() entirely (entity_lifecycle.cpp:1637's `if (!IS_NPC(...))`
// guard) while still exercising the real affect_modify()/affect_total()/
// affect_naked()/apply_gear_affects() bodies this wave's primitives call --
// this is the feasibility-gated, honest-fallback fixture scope the task
// brief calls for, not a mock: every function on the call path is the real
// production body.

#include "../db.h"
#include "../entity_hooks.h"
#include "../handler.h"
#include "../utils.h"
#include "../zone.h"
#include "test_placement.h"
#include "test_world.h"
#include "rots/core/character.h"
#include "rots/core/object.h"
#include "rots/core/room.h"
#include "rots/core/types.h"

#include <gtest/gtest.h>

#include <initializer_list>
#include <map>

// encumb_table[]/leg_encumb_table[] (consts.cpp, L1 core data table) -- same
// local extern-declaration pattern equipment.cpp itself uses (no shared
// header declares them); needed here so the encumbrance-delta tests can
// cross-check attach_equipment()'s/detach_equipment()'s arithmetic against
// the same table the primitive reads, rather than hand-duplicating its
// values.
extern sh_int encumb_table[MAX_WEAR];
extern sh_int leg_encumb_table[MAX_WEAR];

// find_eq_pos() (Cluster B wave Task 1; cb-task-1-brief.md Step 6;
// cb-census.md section 5.4 -- relocated here from act_obj2.cpp, coverage-
// gap rule: zero prior test coverage anywhere in the tree).
int find_eq_pos(struct char_data* ch, struct obj_data* obj, char* arg);

namespace {

// Test-owned room/zone pair a stub resolver hook set hands back for every
// room_by_id()/room_by_id_total()/zone_by_id() call, regardless of the
// requested id -- this wave's primitive tests only ever exercise a
// single-room, single-zone scenario, so per-id dispatch (an id -> pointer
// map) would add fixture complexity no test here needs; a future multi-room
// candidate can extend this class if that changes.
class StubWorldResolvers {
public:
    // Room every stub resolver call returns; null until a test assigns it
    // (room_by_id()/room_by_id_total() are only dispatched by tests that set
    // this).
    room_data* room = nullptr;
    // Zone every stub zone_by_id() call returns; null until a test assigns
    // it.
    zone_data* zone = nullptr;
    // Records the rnum most recently passed to test_room_by_id_total()
    // below -- RoomOfTest (LS-1 Wave Task 1) uses this to confirm
    // room_of(ch) forwards location_of(ch) as the resolver argument, not
    // just that the two calls happen to return the same single-room stub.
    int last_requested_rnum = NOWHERE;

    // Per-id dispatch, the extension this class's own comment above
    // anticipated ("a future multi-room candidate can extend this class if
    // that changes"). LS-3a T2c's relocate_all_occupants()/
    // relocate_all_contents() tests are that candidate: they are the first in
    // this file that need TWO distinct rooms resolvable at the same time, one
    // per id. Left EMPTY by every pre-existing fixture, in which case
    // resolve() falls straight back to `room` above -- so no existing test's
    // behavior changes.
    std::map<int, room_data*> rooms_by_id;

    // The single resolution point all three room trampolines below share.
    room_data* resolve(int rnum)
    {
        const auto entry = rooms_by_id.find(rnum);
        return entry != rooms_by_id.end() ? entry->second : room;
    }
};

// The fixture the file-scope trampoline functions below dispatch to; set by
// whichever ScopedWorldResolverHooks is currently alive. entity_hooks.h's
// hook typedefs are bare function pointers, not std::function, so the
// trampolines cannot capture a fixture reference directly -- this
// indirection stands in for that capture (mirrors entity_lifecycle_tests.cpp's
// g_active_pool).
StubWorldResolvers* g_active_resolvers = nullptr;

room_data* test_room_by_id(int rnum) { return g_active_resolvers->resolve(rnum); }

room_data* test_room_by_id_total(int rnum)
{
    g_active_resolvers->last_requested_rnum = rnum;
    return g_active_resolvers->resolve(rnum);
}

zone_data* test_zone_by_id(int /*znum*/) { return g_active_resolvers->zone; }

// RAII hook-restore hygiene (task-6-brief.md's binding fixture-design
// addendum: "mirror ScopedTxtBlockPoolHooks"). Swaps entity_hooks.h's
// room/room-total/zone resolver hooks to point at a test-owned
// StubWorldResolvers for this fixture's scope, and unconditionally restores
// db_world.cpp's/zone_load.cpp's real resolvers (register_world_resolver_hooks(),
// db.h) on destruction -- including when the test body fails an assertion
// and unwinds early -- so no later test in the binary ever observes a stub
// still installed. The obj-index resolver is untouched (no candidate in this
// file calls obj_index_by_id()), but register_world_resolver_hooks()
// re-registers all four together, matching its own all-or-nothing
// registration contract.
class ScopedWorldResolverHooks {
public:
    explicit ScopedWorldResolverHooks(StubWorldResolvers& resolvers)
        // Whatever g_active_resolvers held before this fixture ran -- always
        // nullptr in practice, since these tests never nest -- restored so a
        // dispatch that somehow raced past teardown cannot dereference a
        // dangling pointer instead of crashing loudly.
        : m_previous_active_resolvers(g_active_resolvers)
    {
        g_active_resolvers = &resolvers;
        rots::entity::set_room_resolver_hook(&test_room_by_id);
        rots::entity::set_room_total_resolver_hook(&test_room_by_id_total);
        rots::entity::set_zone_resolver_hook(&test_zone_by_id);
    }

    ~ScopedWorldResolverHooks()
    {
        register_world_resolver_hooks();
        g_active_resolvers = m_previous_active_resolvers;
    }

private:
    StubWorldResolvers* m_previous_active_resolvers;
};

// A room_data fixture ready for room_by_id_total()/room_by_id() to hand back.
// room_data's own default constructor (rots/core/room.h) only sets
// number/zone/level/name/description/affected -- people/contents/light are
// left indeterminate by that constructor, so this helper zeroes exactly the
// fields the placement/equipment primitives under test read or mutate.
room_data make_stub_room()
{
    room_data room;
    room.people = nullptr;
    room.contents = nullptr;
    room.light = 0;
    return room;
}

// A zone_data fixture with power counters zeroed. zone_data (zone.h) has no
// user-declared constructor, so brace-init value-initializes every field
// (white_power/dark_power/magi_power included) -- this helper exists mainly
// for call-site readability/parity with make_stub_room() above.
zone_data make_stub_zone() { return zone_data { }; }

// A minimal always-NPC char_data fixture for the attach_equipment()/
// detach_equipment() candidates -- see this file's top-of-file "NPC-only
// char_data fixtures" comment for why NPC-ness is this fixture's deliberate,
// feasibility-gated scope boundary.
char_data make_equipment_test_npc()
{
    char_data character { };
    character.specials2.act |= MOB_ISNPC;
    character.player.race = RACE_HUMAN;
    character.player.level = 20;
    return character;
}

// A weapon obj_data fixture: ITEM_WEAPON, the given weight/OB/parry deltas
// (obj_flags.value[0]/[1], attach_equipment()'s WEAPON arm reads these
// directly, not through get_ob_coef()/get_parry_coef()), and CAN_WEAR-able
// for every non-HOLD slot.
obj_data make_test_weapon(int weight, int ob_delta, int parry_delta)
{
    obj_data weapon { };
    weapon.obj_flags.type_flag = ITEM_WEAPON;
    weapon.obj_flags.weight = weight;
    weapon.obj_flags.value[0] = ob_delta;
    weapon.obj_flags.value[1] = parry_delta;
    return weapon;
}

} // namespace

// ---------------------------------------------------------------------------
// char_to_room() (placement.cpp) -- census row char_to_room:716, a clean MOVE
// (no combat/output weld). Candidates: occupant-chain linkage (append at
// tail, order preserved), room light-count bookkeeping, zone white/dark power
// bookkeeping for non-NPC good/evil-race characters -- task-6-brief.md
// Candidate (a).
// ---------------------------------------------------------------------------

TEST(CharToRoomTest, AppendsToEmptyRoomAndSetsOccupantLinkage)
{
    StubWorldResolvers resolvers;
    room_data room = make_stub_room();
    resolvers.room = &room;
    ScopedWorldResolverHooks hooks(resolvers);

    char_data character { };
    character.specials2.act |= MOB_ISNPC; // race-power branch not under test here

    char_to_room(&character, 42);

    EXPECT_EQ(room.people, &character);
    EXPECT_EQ(character.next_in_room, nullptr);
    EXPECT_EQ(character.in_room, 42);
}

TEST(CharToRoomTest, AppendsToTailPreservingExistingOccupantOrder)
{
    StubWorldResolvers resolvers;
    room_data room = make_stub_room();
    resolvers.room = &room;
    ScopedWorldResolverHooks hooks(resolvers);

    char_data first { };
    first.specials2.act |= MOB_ISNPC;
    char_data second { };
    second.specials2.act |= MOB_ISNPC;
    room.people = &first;
    first.next_in_room = &second;
    second.next_in_room = nullptr;

    char_data newcomer { };
    newcomer.specials2.act |= MOB_ISNPC;
    char_to_room(&newcomer, 7);

    // Head unchanged, and the walk-to-tail append lands after `second` --
    // the existing chain's relative order is undisturbed.
    EXPECT_EQ(room.people, &first);
    EXPECT_EQ(first.next_in_room, &second);
    EXPECT_EQ(second.next_in_room, &newcomer);
    EXPECT_EQ(newcomer.next_in_room, nullptr);
}

TEST(CharToRoomTest, IncrementsRoomLightForAnEquippedLitLightSource)
{
    StubWorldResolvers resolvers;
    room_data room = make_stub_room();
    room.light = 1; // some other occupant's light already counted
    resolvers.room = &room;
    ScopedWorldResolverHooks hooks(resolvers);

    char_data character { };
    obj_data light { };
    light.obj_flags.type_flag = ITEM_LIGHT;
    light.obj_flags.value[2] = 5; // fuel remaining
    light.obj_flags.value[3] = 1; // ON
    character.equipment[WEAR_LIGHT] = &light;

    char_to_room(&character, 1);

    EXPECT_EQ(room.light, 2);
}

TEST(CharToRoomTest, DoesNotIncrementRoomLightForAnUnlitLightSource)
{
    StubWorldResolvers resolvers;
    room_data room = make_stub_room();
    room.light = 0;
    resolvers.room = &room;
    ScopedWorldResolverHooks hooks(resolvers);

    char_data character { };
    obj_data light { };
    light.obj_flags.type_flag = ITEM_LIGHT;
    light.obj_flags.value[2] = 5; // has fuel...
    light.obj_flags.value[3] = 0; // ...but OFF
    character.equipment[WEAR_LIGHT] = &light;

    char_to_room(&character, 1);

    EXPECT_EQ(room.light, 0);
}

TEST(CharToRoomTest, IncreasesZoneWhitePowerForAGoodRaceCharacter)
{
    StubWorldResolvers resolvers;
    room_data room = make_stub_room();
    zone_data zone = make_stub_zone();
    zone.white_power = 100;
    zone.dark_power = 50;
    resolvers.room = &room;
    resolvers.zone = &zone;
    ScopedWorldResolverHooks hooks(resolvers);

    char_data character { }; // MOB_ISNPC not set -- a PC/good-race NPC-flagged-off character
    character.player.race = RACE_HUMAN;
    character.player.level = 20;

    char_to_room(&character, 1);

    const int expected_power = char_power(character.player.level);
    EXPECT_EQ(zone.white_power, 100 + expected_power);
    EXPECT_EQ(zone.dark_power, 50); // untouched
}

TEST(CharToRoomTest, IncreasesZoneDarkPowerForAnEvilRaceCharacter)
{
    StubWorldResolvers resolvers;
    room_data room = make_stub_room();
    zone_data zone = make_stub_zone();
    zone.white_power = 100;
    zone.dark_power = 50;
    resolvers.room = &room;
    resolvers.zone = &zone;
    ScopedWorldResolverHooks hooks(resolvers);

    char_data character { };
    character.player.race = RACE_URUK;
    character.player.level = 20;

    char_to_room(&character, 1);

    const int expected_power = char_power(character.player.level);
    EXPECT_EQ(zone.dark_power, 50 + expected_power);
    EXPECT_EQ(zone.white_power, 100); // untouched
}

TEST(CharToRoomTest, DoesNotChangeZonePowerForAnNpc)
{
    StubWorldResolvers resolvers;
    room_data room = make_stub_room();
    zone_data zone = make_stub_zone();
    zone.white_power = 100;
    zone.dark_power = 50;
    resolvers.room = &room;
    resolvers.zone = &zone;
    ScopedWorldResolverHooks hooks(resolvers);

    char_data character { };
    character.specials2.act |= MOB_ISNPC;
    character.player.race = RACE_HUMAN; // good race, but NPC -- must not matter
    character.player.level = 20;

    char_to_room(&character, 1);

    EXPECT_EQ(zone.white_power, 100);
    EXPECT_EQ(zone.dark_power, 50);
}

// ---------------------------------------------------------------------------
// detach_char_from_room() (placement.cpp) -- census row char_from_room:661,
// the SPLIT primitive half of char_from_room() (ADJUDICATE-2). Candidates:
// occupant-chain unlink (head/middle), light-count/zone-power reversal,
// in_room/next_in_room clearing, and the false-return early-exit paths'
// no-mutation guarantee -- task-6-brief.md Candidate (a) and BINDING addendum
// #3.
// ---------------------------------------------------------------------------

TEST(DetachCharFromRoomTest, ReturnsFalseAndMutatesNothingWhenAlreadyNowhere)
{
    // No ScopedWorldResolverHooks needed: the NOWHERE early return happens
    // before detach_char_from_room() calls room_by_id_total() at all, so the
    // real (unregistered-for-this-test-process... actually registered, but
    // untouched) resolver is never dispatched.
    char_data character { };
    character.in_room = NOWHERE;
    character.next_in_room = nullptr;

    const bool result = detach_char_from_room(&character);

    EXPECT_FALSE(result);
    EXPECT_EQ(character.in_room, NOWHERE);
    EXPECT_EQ(character.next_in_room, nullptr);
}

TEST(DetachCharFromRoomTest, RemovesHeadOfListAndClearsLinkage)
{
    StubWorldResolvers resolvers;
    room_data room = make_stub_room();
    resolvers.room = &room;
    ScopedWorldResolverHooks hooks(resolvers);

    char_data target { };
    char_data tail { };
    room.people = &target;
    target.next_in_room = &tail;
    tail.next_in_room = nullptr;
    target.in_room = 9;

    const bool result = detach_char_from_room(&target);

    EXPECT_TRUE(result);
    EXPECT_EQ(room.people, &tail);
    EXPECT_EQ(target.in_room, NOWHERE);
    EXPECT_EQ(target.next_in_room, nullptr);
}

TEST(DetachCharFromRoomTest, RemovesMiddleOfListPreservingNeighborLinks)
{
    StubWorldResolvers resolvers;
    room_data room = make_stub_room();
    resolvers.room = &room;
    ScopedWorldResolverHooks hooks(resolvers);

    char_data head { };
    char_data target { };
    char_data tail { };
    room.people = &head;
    head.next_in_room = &target;
    target.next_in_room = &tail;
    tail.next_in_room = nullptr;
    target.in_room = 3;

    const bool result = detach_char_from_room(&target);

    EXPECT_TRUE(result);
    EXPECT_EQ(room.people, &head); // head untouched
    EXPECT_EQ(head.next_in_room, &tail); // target bypassed
    EXPECT_EQ(tail.next_in_room, nullptr); // tail's own link untouched
    EXPECT_EQ(target.in_room, NOWHERE);
    EXPECT_EQ(target.next_in_room, nullptr);
}

TEST(DetachCharFromRoomTest, DecrementsRoomLightWhenTheDetachedCharacterHadALitLight)
{
    StubWorldResolvers resolvers;
    room_data room = make_stub_room();
    room.light = 2;
    resolvers.room = &room;
    ScopedWorldResolverHooks hooks(resolvers);

    char_data target { };
    room.people = &target;
    target.in_room = 1;
    obj_data light { };
    light.obj_flags.type_flag = ITEM_LIGHT;
    light.obj_flags.value[2] = 5;
    light.obj_flags.value[3] = 1; // ON
    target.equipment[WEAR_LIGHT] = &light;

    ASSERT_TRUE(detach_char_from_room(&target));
    EXPECT_EQ(room.light, 1);
}

TEST(DetachCharFromRoomTest, DecrementsZoneDarkPowerForAnEvilRaceCharacter)
{
    StubWorldResolvers resolvers;
    room_data room = make_stub_room();
    zone_data zone = make_stub_zone();
    zone.dark_power = 100;
    resolvers.room = &room;
    resolvers.zone = &zone;
    ScopedWorldResolverHooks hooks(resolvers);

    char_data target { };
    target.player.race = RACE_URUK;
    target.player.level = 20;
    room.people = &target;
    target.in_room = 1;

    ASSERT_TRUE(detach_char_from_room(&target));

    const int expected_power = char_power(target.player.level);
    EXPECT_EQ(zone.dark_power, 100 - expected_power);
}

TEST(DetachCharFromRoomTest, DefensiveMissingNodeReturnsFalseWithoutListOrLocationMutation)
{
    StubWorldResolvers resolvers;
    room_data room = make_stub_room();
    resolvers.room = &room;
    ScopedWorldResolverHooks hooks(resolvers);

    // `other` is the room's only real occupant; `target` claims to be in this
    // room (in_room matches) but is NOT reachable by walking room.people's
    // next_in_room chain -- the defensive `if (!i) return false;` path
    // (placement.cpp) that has no equivalent in the head-of-list branch.
    char_data other { };
    room.people = &other;
    other.next_in_room = nullptr;

    char_data target { };
    target.in_room = 5;
    target.next_in_room = nullptr;

    const bool result = detach_char_from_room(&target);

    EXPECT_FALSE(result);
    EXPECT_EQ(room.people, &other);
    EXPECT_EQ(other.next_in_room, nullptr);
    // The high-value "false means nothing happened" guarantee for the
    // list/location fields specifically -- unlike the NOWHERE path above,
    // this defensive path does NOT roll back everything (see the sibling
    // test immediately below); it is specifically the occupant-chain link
    // and ch->in_room/next_in_room that stay untouched.
    EXPECT_EQ(target.in_room, 5);
    EXPECT_EQ(target.next_in_room, nullptr);
}

TEST(DetachCharFromRoomTest, DefensiveMissingNodePathStillDecrementsRoomLightPreExistingQuirk)
{
    // Characterization test, not a "should": detach_char_from_room()'s
    // light-decrement loop (placement.cpp) runs BEFORE the occupant-chain
    // search that can hit the defensive `if (!i) return false;` path -- so a
    // detached-but-unlinked character carrying a lit light still decrements
    // room->light even though the function reports false and leaves the
    // occupant chain/ch->in_room alone (see the sibling test above). This is
    // inherited, byte-preserved behavior from the pre-split char_from_room()
    // (not something this wave's SPLIT introduced -- the light loop and the
    // list-search were already sequential, unconditional-then-conditional,
    // in the original), pinned here so a future cleanup that reorders them
    // does so as a deliberate, reviewed behavior change rather than an
    // unnoticed regression.
    StubWorldResolvers resolvers;
    room_data room = make_stub_room();
    room.light = 3;
    resolvers.room = &room;
    ScopedWorldResolverHooks hooks(resolvers);

    char_data other { };
    room.people = &other;
    other.next_in_room = nullptr;

    char_data target { };
    target.in_room = 5;
    obj_data light { };
    light.obj_flags.type_flag = ITEM_LIGHT;
    light.obj_flags.value[2] = 5;
    light.obj_flags.value[3] = 1; // ON
    target.equipment[WEAR_LIGHT] = &light;

    const bool result = detach_char_from_room(&target);

    EXPECT_FALSE(result);
    EXPECT_EQ(room.light, 2); // decremented despite the false return
    EXPECT_EQ(target.in_room, 5); // location itself still untouched
}

// ---------------------------------------------------------------------------
// room_of(ch) (placement.cpp; declared in handler.h beside location_of()/
// room_by_id_total()) -- LS-1 Wave Task 1, the census-justified API
// completion (.superpowers/sdd/ls1-census.md Step 5: ~161 counted self-room
// `world[X->in_room]` read sites the wave's T2 batches convert onto this
// call). Consumer-free as landed -- no call site converts in this task, that
// is T2's job; these two tests are the API's own TDD contract proof:
// room_of(ch) == room_by_id_total(location_of(ch)), for both a placed
// character and a NOWHERE/out-of-range one (never null -- the fallback
// room_by_id_total() returns, not room_by_id()'s nullptr).
// ---------------------------------------------------------------------------

TEST(RoomOfTest, ReturnsTheSamePointerAsTheNestedFormForAPlacedCharacter)
{
    StubWorldResolvers resolvers;
    room_data room = make_stub_room();
    resolvers.room = &room;
    ScopedWorldResolverHooks hooks(resolvers);

    char_data character { };
    character.in_room = 42;

    room_data* const nested = room_by_id_total(location_of(&character));
    room_data* const via_room_of = room_of(&character);

    ASSERT_NE(via_room_of, nullptr);
    EXPECT_EQ(via_room_of, nested);
    EXPECT_EQ(via_room_of, &room);
    // Confirms room_of() actually forwarded location_of(ch) to the resolver
    // rather than happening to match through the stub's single-room return.
    EXPECT_EQ(resolvers.last_requested_rnum, 42);
}

TEST(RoomOfTest, ReturnsTheFallbackPointerForANowhereCharacter)
{
    StubWorldResolvers resolvers;
    // The stub's one room stands in for room_by_id_total()'s graceful
    // out-of-range fallback (see entity_hooks.h's two-variant contract
    // comment) -- the real resolver never returns null for any rnum,
    // including NOWHERE, and room_of() must preserve that.
    room_data room = make_stub_room();
    resolvers.room = &room;
    ScopedWorldResolverHooks hooks(resolvers);

    char_data character { };
    character.in_room = NOWHERE;

    room_data* const nested = room_by_id_total(location_of(&character));
    room_data* const via_room_of = room_of(&character);

    ASSERT_NE(via_room_of, nullptr);
    EXPECT_EQ(via_room_of, nested);
    EXPECT_EQ(resolvers.last_requested_rnum, NOWHERE);
}

// ---------------------------------------------------------------------------
// set_location(ch, rnum) / is_in_room(ch, rnum) (placement.cpp; declared in
// handler.h beside location_of()/room_of()) -- LS-3a Wave T1, ruling R-A3.
// Both have had ZERO callers and ZERO tests anywhere in the tree since the
// placement-seam wave landed them, and T2 is about to give set_location()
// 35+ first callers, so these are the API's own TDD contract proof BEFORE
// any conversion rides on it (the occupants_from() shape from LS-2 T1).
//
// The contract being pinned (R-A1, controller-verified exhaustively over all
// 40 T2 candidate sites): set_location() is a BARE FIELD WRITE -- literally
// `ch->in_room = rnum;` and nothing else. No occupant-chain linkage, no room
// light bookkeeping, no zone white/dark-power bookkeeping, and -- unlike
// room_of()/room_by_id()/room_by_id_total()/zone_by_id() -- no dispatch
// through an entity_hooks.h resolver at all. That hook-free property is the
// R-A1 corollary making set_location() the ONE location API safe on a
// rots_convert-reachable path, where the world resolvers' unregistered
// default is a tripwire abort() no link check catches. It is also why every
// bare `ch->in_room = X` that T2 converts is a byte-identical substitution
// rather than a double-apply hazard: the bookkeeping movers char_to_room()
// and detach_char_from_room() assign the field DIRECTLY (placement.cpp) and
// never route through this wrapper.
//
// Storage is asserted against the raw ch->in_room field rather than through
// location_of(), so a defect in the writer cannot be masked by a matching
// defect in the reader; separate round-trip tests then pin the two against
// each other. Raw field access is this TU's standing convention -- it is the
// representation owner's own test file (see the CharToRoomTest/RoomOfTest
// sections above).
// ---------------------------------------------------------------------------

TEST(SetLocationTest, StoresAPositiveRoomIdExactly)
{
    char_data character { };
    character.in_room = 3;

    set_location(&character, 42);

    EXPECT_EQ(character.in_room, 42);
}

TEST(SetLocationTest, StoresZeroExactly)
{
    // Room 0 is a real, routinely occupied room (it is the single-room
    // fixture world every suite in this binary shares), so a zero id must
    // survive as a value and not be treated as "unset".
    char_data character { };
    character.in_room = 42;

    set_location(&character, 0);

    EXPECT_EQ(character.in_room, 0);
}

TEST(SetLocationTest, StoresNowhereExactly)
{
    // NOWHERE is the absence sentinel (-1) every T2 site that clears a
    // location writes; detach_char_from_room() stamps the same value.
    char_data character { };
    character.in_room = 42;

    set_location(&character, NOWHERE);

    EXPECT_EQ(character.in_room, NOWHERE);
    EXPECT_EQ(character.in_room, -1);
}

TEST(SetLocationTest, OverwritesThePreviousLocationRatherThanAccumulating)
{
    char_data character { };

    set_location(&character, 5);
    set_location(&character, 9);

    EXPECT_EQ(character.in_room, 9);
}

TEST(SetLocationTest, TouchesNothingButTheLocationField)
{
    StubWorldResolvers resolvers;
    room_data room = make_stub_room();
    room.light = 3;
    zone_data zone = make_stub_zone();
    zone.white_power = 100;
    zone.dark_power = 50;
    resolvers.room = &room;
    resolvers.zone = &zone;
    ScopedWorldResolverHooks hooks(resolvers);

    // A fully linked, non-NPC good-race occupant chain -- exactly the state
    // char_to_room()/detach_char_from_room() maintain -- so any stray
    // bookkeeping hiding inside set_location() would be visible here as a
    // chain, light, or zone-power delta.
    char_data head { };
    head.player.race = RACE_HUMAN;
    head.player.level = 20;
    char_data mover { };
    mover.player.race = RACE_HUMAN;
    mover.player.level = 20;
    room.people = &head;
    head.next_in_room = &mover;
    mover.next_in_room = nullptr;
    head.in_room = 7;
    mover.in_room = 7;

    set_location(&mover, 9);

    EXPECT_EQ(mover.in_room, 9);
    // Occupant chain: untouched in both directions -- the mover is still
    // linked into the room it no longer claims to be in (that torn state is
    // precisely what makes this a bare write and NOT a mover).
    EXPECT_EQ(room.people, &head);
    EXPECT_EQ(head.next_in_room, &mover);
    EXPECT_EQ(mover.next_in_room, nullptr);
    // The neighbour's own location: untouched.
    EXPECT_EQ(head.in_room, 7);
    // Room light and zone power: untouched (char_to_room()/
    // detach_char_from_room() own both; this wrapper owns neither).
    EXPECT_EQ(room.light, 3);
    EXPECT_EQ(zone.white_power, 100);
    EXPECT_EQ(zone.dark_power, 50);
    // And no world resolver was dispatched at all: last_requested_rnum still
    // holds StubWorldResolvers' untouched default, where a single
    // room_by_id_total() call inside set_location() would have replaced it
    // with 9. This is the hook-free property R-A1's rots_convert corollary
    // depends on.
    EXPECT_EQ(resolvers.last_requested_rnum, NOWHERE);
}

TEST(SetLocationTest, IsImmediatelyObservedByLocationOfAndIsInRoom)
{
    char_data character { };

    set_location(&character, 17);

    EXPECT_EQ(location_of(&character), 17);
    EXPECT_TRUE(is_in_room(&character, 17));
}

TEST(SetLocationTest, NowhereRoundTripsThroughLocationOfAndIsInRoom)
{
    char_data character { };
    character.in_room = 4;

    set_location(&character, NOWHERE);

    EXPECT_EQ(location_of(&character), NOWHERE);
    EXPECT_TRUE(is_in_room(&character, NOWHERE));
    EXPECT_FALSE(is_in_room(&character, 4));
}

TEST(IsInRoomTest, TrueForTheRoomTheCharacterIsIn)
{
    char_data character { };
    character.in_room = 12;

    EXPECT_TRUE(is_in_room(&character, 12));
}

TEST(IsInRoomTest, FalseForADifferentRoom)
{
    char_data character { };
    character.in_room = 12;

    EXPECT_FALSE(is_in_room(&character, 13));
    EXPECT_FALSE(is_in_room(&character, 11));
    EXPECT_FALSE(is_in_room(&character, 0));
}

TEST(IsInRoomTest, FalseForARealRoomWhenTheCharacterIsNowhere)
{
    char_data character { };
    character.in_room = NOWHERE;

    EXPECT_FALSE(is_in_room(&character, 0));
    EXPECT_FALSE(is_in_room(&character, 12));
}

TEST(IsInRoomTest, TrueForNowhereWhenTheCharacterIsNowhere)
{
    // `is_in_room(ch, NOWHERE)` is the absence test spelled positively; it
    // must discriminate on the sentinel like any other id, not special-case
    // it away.
    char_data character { };
    character.in_room = NOWHERE;

    EXPECT_TRUE(is_in_room(&character, NOWHERE));
}

TEST(IsInRoomTest, MatchesExactlyOneRoomIdAcrossAProbedRange)
{
    // Equality, not a range/sign/truthiness test: over a probe sweep that
    // spans the absence sentinel, room 0, and the character's own room,
    // exactly one id may answer true.
    char_data character { };
    character.in_room = 5;

    int matches = 0;
    for (int rnum = NOWHERE; rnum <= 10; ++rnum) {
        if (is_in_room(&character, rnum)) {
            ++matches;
            EXPECT_EQ(rnum, 5);
        }
    }

    EXPECT_EQ(matches, 1);
}

TEST(IsInRoomTest, ReadsThroughAConstCharacterPointer)
{
    // location_of()/is_in_room() both take `const char_data*` (handler.h)
    // while set_location() takes a mutable one; binding the readers through
    // a const pointer here is the compile-time proof of that split, so a
    // caller holding only a const view can still ask where a character is
    // without being handed the ability to move them.
    char_data character { };
    set_location(&character, 12);
    const char_data* const observer = &character;

    EXPECT_EQ(location_of(observer), 12);
    EXPECT_TRUE(is_in_room(observer, 12));
    EXPECT_FALSE(is_in_room(observer, 13));
}

// ---------------------------------------------------------------------------
// attach_equipment() (equipment.cpp) -- census row equip_char:815, the SPLIT
// primitive half of equip_char(). Candidates: slot assignment,
// encumbrance/weight deltas, per-arm stat mutation (ARMOR/WEAPON/SHIELD/
// LIGHT), affect_modify()/affect_total() invocation, the HOLD early-return
// outcome mapping, and the round-2 Critical's regression class (too-heavy
// verdict must use PRE-affect strength) -- task-6-brief.md Candidate (b) and
// BINDING addendum #2.
// ---------------------------------------------------------------------------

TEST(AttachEquipmentTest, HoldSlotEarlyReturnAppliesEncumbranceButSkipsStatsAndAffects)
{
    char_data character = make_equipment_test_npc();
    obj_data item { };
    item.obj_flags.type_flag = ITEM_TRASH; // arbitrary non-weapon type
    item.obj_flags.weight = 10;
    item.obj_flags.value[2] = 4;
    item.obj_flags.wear_flags = 0; // NOT CAN_WEAR(ITEM_HOLD) -- triggers the guard
    item.affected[0].location = APPLY_OB;
    item.affected[0].modifier = 7;

    const EquipAttachOutcome outcome = attach_equipment(&character, &item, HOLD);

    EXPECT_EQ(outcome, EquipAttachOutcome::HOLD_EARLY_RETURN);
    // Slot assignment and encumb/weight math happen unconditionally, BEFORE
    // the guard (equipment.cpp's original-structure-preserving ordering).
    EXPECT_EQ(character.equipment[HOLD], &item);
    EXPECT_EQ(item.carried_by, &character);
    EXPECT_NE(character.specials.encumb_weight, 0);
    // But the type-dispatch stat mutation and the affect_modify()/
    // affect_total() loop never ran: OB is untouched by the APPLY_OB affect
    // this item carries, and points.OB/dodge/parry are all still their
    // zero-initialized defaults.
    EXPECT_EQ(character.points.OB, 0);
    EXPECT_EQ(character.points.dodge, 0);
    EXPECT_EQ(character.points.parry, 0);
}

TEST(AttachEquipmentTest, ArmorSlotIncreasesDodgeAndReturnsOther)
{
    char_data character = make_equipment_test_npc();
    obj_data armor { };
    armor.obj_flags.type_flag = ITEM_ARMOR;
    armor.obj_flags.weight = 10;
    armor.obj_flags.value[3] = 6; // dodge bonus

    const EquipAttachOutcome outcome = attach_equipment(&character, &armor, WEAR_BODY);

    EXPECT_EQ(outcome, EquipAttachOutcome::OTHER);
    EXPECT_EQ(character.points.dodge, 6);
    EXPECT_EQ(character.equipment[WEAR_BODY], &armor);
}

TEST(AttachEquipmentTest, ShieldSlotIncreasesDodgeAndParryAndReturnsOther)
{
    char_data character = make_equipment_test_npc();
    obj_data shield { };
    shield.obj_flags.type_flag = ITEM_SHIELD;
    shield.obj_flags.weight = 10;
    shield.obj_flags.value[0] = 3; // dodge bonus (SHIELD arm reads value[0]/value[1], unlike WEAPON)
    shield.obj_flags.value[1] = 4; // parry bonus

    const EquipAttachOutcome outcome = attach_equipment(&character, &shield, WEAR_SHIELD);

    EXPECT_EQ(outcome, EquipAttachOutcome::OTHER);
    EXPECT_EQ(character.points.dodge, 3);
    EXPECT_EQ(character.points.parry, 4);
}

TEST(AttachEquipmentTest, WeaponWithinCapacityReturnsWeaponOutcomeAndAppliesObParry)
{
    char_data character = make_equipment_test_npc();
    character.tmpabilities.str = 10; // GET_BAL_STR == 10 (<= max_race_str, all races: 22)
    obj_data weapon = make_test_weapon(/*weight=*/100, /*ob_delta=*/5, /*parry_delta=*/2);
    // 100 <= bal_str(10)*50 (500) -- comfortably within one-hand capacity.

    const EquipAttachOutcome outcome = attach_equipment(&character, &weapon, WIELD);

    EXPECT_EQ(outcome, EquipAttachOutcome::WEAPON);
    EXPECT_EQ(character.points.OB, 5);
    EXPECT_EQ(character.points.parry, 2);
}

TEST(AttachEquipmentTest, WeaponOverOneHandThresholdReturnsTooHeavyOneHandOutcome)
{
    char_data character = make_equipment_test_npc();
    character.tmpabilities.str = 10; // bal_str = 10 -> one-hand threshold = 500
    obj_data weapon = make_test_weapon(/*weight=*/600, 0, 0);

    const EquipAttachOutcome outcome = attach_equipment(&character, &weapon, WIELD);

    EXPECT_EQ(outcome, EquipAttachOutcome::WEAPON_TOO_HEAVY_ONE_HAND);
}

TEST(AttachEquipmentTest, WeaponOverTwoHandThresholdReturnsTooHeavyForYouOutcome)
{
    // WEAPON_TOO_HEAVY_FOR_YOU is only reachable when the one-hand condition
    // does NOT hold (equipment.cpp: `if (weight > bal*50 && !twohanded) ...
    // else if (weight > bal*100) ...`) -- for a non-twohanded character, any
    // weapon over the one-hand threshold is ALWAYS reported
    // WEAPON_TOO_HEAVY_ONE_HAND first, however much it also exceeds the
    // two-hand threshold (see WeaponOverOneHandThresholdReturnsTooHeavyOneHandOutcome
    // above). Reaching FOR_YOU therefore requires IS_TWOHANDED(ch) to already
    // be set (bypassing the one-hand arm) AND the weight to still exceed the
    // two-hand threshold.
    char_data character = make_equipment_test_npc();
    character.tmpabilities.str = 10; // bal_str = 10 -> two-hand threshold = 1000
    character.specials.affected_by |= AFF_TWOHANDED;
    obj_data weapon = make_test_weapon(/*weight=*/1100, 0, 0);

    const EquipAttachOutcome outcome = attach_equipment(&character, &weapon, WIELD);

    EXPECT_EQ(outcome, EquipAttachOutcome::WEAPON_TOO_HEAVY_FOR_YOU);
}

TEST(AttachEquipmentTest, TwoHandedBypassesOnlyTheOneHandThreshold)
{
    char_data character = make_equipment_test_npc();
    character.tmpabilities.str = 10; // one-hand threshold 500, two-hand threshold 1000
    character.specials.affected_by |= AFF_TWOHANDED;
    obj_data weapon = make_test_weapon(/*weight=*/600, 0, 0); // > 500, but twohanded exempts it

    const EquipAttachOutcome outcome = attach_equipment(&character, &weapon, WIELD);

    EXPECT_EQ(outcome, EquipAttachOutcome::WEAPON);
}

TEST(AttachEquipmentTest, TooHeavyVerdictUsesPreAffectStrengthNotPostAffectRegression)
{
    // The round-2 Critical's regression class (task-6-brief.md BINDING
    // addendum #2 / equipment.cpp's own CRITICAL FIX comment): a weapon
    // carrying an APPLY_STR affect must NOT have that affect change its OWN
    // too-heavy verdict. bal_str starts at 10 (threshold 500); this weapon
    // weighs 600 (too heavy at bal_str 10) but carries APPLY_STR +50, which
    // -- if wrongly applied BEFORE the check -- would raise the character's
    // raw str to 60 (GET_BAL_STR caps this at 41 via max_race_str[RACE_HUMAN]'s
    // unbalancing formula; threshold 2050) and make 600 NOT too heavy. A regressed
    // attach_equipment() that evaluates the check after affect_modify()
    // would report WEAPON here instead of WEAPON_TOO_HEAVY_ONE_HAND.
    char_data character = make_equipment_test_npc();
    character.tmpabilities.str = 10;
    obj_data weapon = make_test_weapon(/*weight=*/600, 0, 0);
    weapon.affected[0].location = APPLY_STR;
    weapon.affected[0].modifier = 50;

    const EquipAttachOutcome outcome = attach_equipment(&character, &weapon, WIELD);

    EXPECT_EQ(outcome, EquipAttachOutcome::WEAPON_TOO_HEAVY_ONE_HAND);
    // Confirm the affect DID run (after the decision) -- proves this is a
    // pre-vs-post ORDERING assertion, not merely a broken affect application.
    EXPECT_EQ(character.tmpabilities.str, 60);
}

TEST(AttachEquipmentTest, InvokesAffectModifyAndAffectTotalForTheItemsOwnAffects)
{
    char_data character = make_equipment_test_npc();
    obj_data armor { };
    armor.obj_flags.type_flag = ITEM_ARMOR;
    armor.obj_flags.weight = 5;
    armor.affected[0].location = APPLY_OB;
    armor.affected[0].modifier = 9;

    attach_equipment(&character, &armor, WEAR_BODY);

    // The ARMOR arm itself never touches points.OB (only dodge, asserted in
    // ArmorSlotIncreasesDodgeAndReturnsOther above) -- this delta can only
    // come from the affect_modify() loop applying the item's own affected[]
    // entry, confirming that loop (and affect_total(), which it precedes)
    // actually ran.
    EXPECT_EQ(character.points.OB, 9);
}

TEST(AttachEquipmentTest, LightSlotTurnsOnAndIncrementsRoomLightWhenCharacterIsInARoom)
{
    StubWorldResolvers resolvers;
    room_data room = make_stub_room();
    room.light = 0;
    resolvers.room = &room;
    ScopedWorldResolverHooks hooks(resolvers);

    char_data character = make_equipment_test_npc();
    character.in_room = 1;
    obj_data lamp { };
    lamp.obj_flags.type_flag = ITEM_LIGHT;
    lamp.obj_flags.weight = 2;
    lamp.obj_flags.value[2] = 10; // fuel
    lamp.obj_flags.value[3] = 0; // starts OFF

    attach_equipment(&character, &lamp, WEAR_LIGHT);

    EXPECT_EQ(lamp.obj_flags.value[3], 1); // turned on
    EXPECT_EQ(room.light, 1);
}

TEST(AttachEquipmentTest, LightSlotHasNoRoomEffectWhenCharacterIsNowhere)
{
    // No ScopedWorldResolverHooks installed -- the ch->in_room != NOWHERE
    // guard (equipment.cpp) must short-circuit before room_by_id_total() is
    // ever dispatched, so this test deliberately leaves the real
    // (world-data-less) resolver in place to prove the guard, not a stub,
    // is what prevents a dereference.
    char_data character = make_equipment_test_npc();
    character.in_room = NOWHERE;
    obj_data lamp { };
    lamp.obj_flags.type_flag = ITEM_LIGHT;
    lamp.obj_flags.weight = 2;
    lamp.obj_flags.value[2] = 10;
    lamp.obj_flags.value[3] = 0;

    const EquipAttachOutcome outcome = attach_equipment(&character, &lamp, WEAR_LIGHT);

    EXPECT_EQ(outcome, EquipAttachOutcome::OTHER);
    EXPECT_EQ(lamp.obj_flags.value[3], 0); // untouched -- guard skipped the whole LIGHT body
}

TEST(AttachEquipmentTest, EncumbranceAndWeightDeltasMatchTheEncumbTables)
{
    char_data character = make_equipment_test_npc();
    obj_data item { };
    item.obj_flags.type_flag = ITEM_ARMOR;
    item.obj_flags.weight = 8;
    item.obj_flags.value[2] = 3; // per-slot encumb multiplicand

    attach_equipment(&character, &item, WEAR_BODY);

    const int expected_encumb = item.obj_flags.value[2] * encumb_table[WEAR_BODY];
    const int expected_leg_encumb = item.obj_flags.value[2] * leg_encumb_table[WEAR_BODY];
    const int expected_encumb_weight = encumb_table[WEAR_BODY] != 0
        ? item.obj_flags.weight * encumb_table[WEAR_BODY]
        : item.obj_flags.weight / 2;

    EXPECT_EQ(character.points.encumb, expected_encumb);
    EXPECT_EQ(character.specials2.leg_encumb, expected_leg_encumb);
    EXPECT_EQ(character.specials.encumb_weight, expected_encumb_weight);
    EXPECT_EQ(character.specials.worn_weight, item.obj_flags.weight);
    EXPECT_EQ(character.specials.carry_weight, item.obj_flags.weight);
}

// ---------------------------------------------------------------------------
// detach_equipment() (equipment.cpp) -- census row unequip_char:919, the
// SPLIT primitive half of unequip_char(). Candidates: the zero-object guard,
// slot clearing, encumbrance/weight reversal, per-arm stat reversal, the
// light-off decrement, and affect_modify()/affect_total() invocation --
// task-6-brief.md Candidate (b).
// ---------------------------------------------------------------------------

TEST(DetachEquipmentTest, ZeroObjectGuardReturnsNullWithoutMutation)
{
    char_data character = make_equipment_test_npc();
    // character.equipment[WEAR_BODY] is null by construction (char_data{}).

    obj_data* result = detach_equipment(&character, WEAR_BODY);

    EXPECT_EQ(result, nullptr);
    EXPECT_EQ(character.points.encumb, 0);
    EXPECT_EQ(character.specials.encumb_weight, 0);
}

TEST(DetachEquipmentTest, HoldSlotEarlyReturnClearsSlotButSkipsStatsAndAffects)
{
    char_data character = make_equipment_test_npc();
    obj_data item { };
    item.obj_flags.type_flag = ITEM_TRASH;
    item.obj_flags.weight = 10;
    item.obj_flags.value[2] = 4;
    item.obj_flags.wear_flags = 0; // NOT CAN_WEAR(ITEM_HOLD)
    item.affected[0].location = APPLY_OB;
    item.affected[0].modifier = 7;
    character.equipment[HOLD] = &item;
    character.specials.encumb_weight = 100; // nonzero baseline so the decrement is observable

    obj_data* result = detach_equipment(&character, HOLD);

    EXPECT_EQ(result, &item);
    // Slot clear + encumb/weight reversal happen unconditionally, BEFORE the
    // guard (mirrors attach_equipment()'s HOLD_EARLY_RETURN ordering).
    EXPECT_EQ(character.equipment[HOLD], nullptr);
    // encumb_table[HOLD] is 0 (consts.cpp) -- HOLD is the one slot whose
    // points.encumb delta is always zero regardless of the item, so this
    // assertion is on specials.encumb_weight instead (which HOLD's `else
    // GET_ENCUMB_WEIGHT -= weight / 2;` branch always touches).
    EXPECT_LT(character.specials.encumb_weight, 100);
    EXPECT_EQ(character.points.encumb, 0);
    // But no affect_modify()/affect_total() ran: the item's APPLY_OB affect
    // was never applied in the first place (this is a bare detach, not a
    // reversal of a prior attach), so OB stays at its zero default,
    // confirming the guard skipped the affect-removal loop rather than
    // removing an affect that was never there.
    EXPECT_EQ(character.points.OB, 0);
}

TEST(DetachEquipmentTest, ArmorSlotReversesDodgeAndClearsSlot)
{
    char_data character = make_equipment_test_npc();
    obj_data armor { };
    armor.obj_flags.type_flag = ITEM_ARMOR;
    armor.obj_flags.weight = 10;
    armor.obj_flags.value[3] = 6;
    character.equipment[WEAR_BODY] = &armor;
    character.points.dodge = 6; // as if attach_equipment() had already run

    obj_data* result = detach_equipment(&character, WEAR_BODY);

    EXPECT_EQ(result, &armor);
    EXPECT_EQ(character.points.dodge, 0);
    EXPECT_EQ(character.equipment[WEAR_BODY], nullptr);
}

TEST(DetachEquipmentTest, WeaponSlotReversesObAndParry)
{
    char_data character = make_equipment_test_npc();
    obj_data weapon = make_test_weapon(/*weight=*/50, /*ob_delta=*/5, /*parry_delta=*/2);
    character.equipment[WIELD] = &weapon;
    character.points.OB = 5;
    character.points.parry = 2;

    obj_data* result = detach_equipment(&character, WIELD);

    EXPECT_EQ(result, &weapon);
    EXPECT_EQ(character.points.OB, 0);
    EXPECT_EQ(character.points.parry, 0);
}

TEST(DetachEquipmentTest, LightSlotTurnsOffAndDecrementsRoomLightWhenItWasOn)
{
    StubWorldResolvers resolvers;
    room_data room = make_stub_room();
    room.light = 1;
    resolvers.room = &room;
    ScopedWorldResolverHooks hooks(resolvers);

    char_data character = make_equipment_test_npc();
    character.in_room = 1;
    obj_data lamp { };
    lamp.obj_flags.type_flag = ITEM_LIGHT;
    lamp.obj_flags.value[2] = 10;
    lamp.obj_flags.value[3] = 1; // ON
    character.equipment[WEAR_LIGHT] = &lamp;

    obj_data* result = detach_equipment(&character, WEAR_LIGHT);

    EXPECT_EQ(result, &lamp);
    EXPECT_EQ(lamp.obj_flags.value[3], 0);
    EXPECT_EQ(room.light, 0);
}

TEST(DetachEquipmentTest, LightSlotDoesNotDecrementRoomLightWhenAlreadyOff)
{
    StubWorldResolvers resolvers;
    room_data room = make_stub_room();
    room.light = 0;
    resolvers.room = &room;
    ScopedWorldResolverHooks hooks(resolvers);

    char_data character = make_equipment_test_npc();
    character.in_room = 1;
    obj_data lamp { };
    lamp.obj_flags.type_flag = ITEM_LIGHT;
    lamp.obj_flags.value[2] = 10;
    lamp.obj_flags.value[3] = 0; // already OFF
    character.equipment[WEAR_LIGHT] = &lamp;

    detach_equipment(&character, WEAR_LIGHT);

    EXPECT_EQ(room.light, 0);
}

TEST(DetachEquipmentTest, InvokesAffectModifyAndAffectTotalForTheItemsOwnAffects)
{
    char_data character = make_equipment_test_npc();
    obj_data armor { };
    armor.obj_flags.type_flag = ITEM_ARMOR;
    armor.affected[0].location = APPLY_OB;
    armor.affected[0].modifier = 9;
    character.equipment[WEAR_BODY] = &armor;
    character.points.OB = 9; // as if attach_equipment() had already applied it

    detach_equipment(&character, WEAR_BODY);

    // Symmetric with AttachEquipmentTest.InvokesAffectModifyAndAffectTotal...
    // above: the ARMOR arm never touches points.OB itself, so this reversal
    // to 0 can only come from the affect_modify()-with-REMOVE loop actually
    // running.
    EXPECT_EQ(character.points.OB, 0);
}

TEST(DetachEquipmentTest, AttachThenDetachRoundTripsEncumbranceAndWeightToZero)
{
    // Round-trip symmetry check: attach_equipment() followed by
    // detach_equipment() on the same item/slot must leave every
    // encumbrance/weight counter exactly where it started (zero, for a fresh
    // fixture) -- a cheap, independent cross-check on top of the two
    // primitives' individually-asserted +=/-= arithmetic above.
    char_data character = make_equipment_test_npc();
    obj_data item { };
    item.obj_flags.type_flag = ITEM_ARMOR;
    item.obj_flags.weight = 8;
    item.obj_flags.value[2] = 3;
    item.obj_flags.value[3] = 4;

    attach_equipment(&character, &item, WEAR_BODY);
    ASSERT_NE(character.points.encumb, 0);
    ASSERT_EQ(character.points.dodge, 4);

    obj_data* result = detach_equipment(&character, WEAR_BODY);

    EXPECT_EQ(result, &item);
    EXPECT_EQ(character.points.encumb, 0);
    EXPECT_EQ(character.specials2.leg_encumb, 0);
    EXPECT_EQ(character.specials.encumb_weight, 0);
    EXPECT_EQ(character.specials.worn_weight, 0);
    EXPECT_EQ(character.specials.carry_weight, 0);
    EXPECT_EQ(character.points.dodge, 0);
    EXPECT_EQ(character.equipment[WEAR_BODY], nullptr);
}

// ---------------------------------------------------------------------------
// find_eq_pos() coverage riders (Cluster B wave Task 1; cb-task-1-brief.md
// Step 6; cb-census.md section 5.4). Untested live code before this
// relocation.
// ---------------------------------------------------------------------------

TEST(Equipment, FindEqPosAutoDetectsWearableSlotWhenArgIsEmpty)
{
    char_data ch {};
    obj_data obj {};
    obj.obj_flags.wear_flags = ITEM_WEAR_HEAD;

    EXPECT_EQ(find_eq_pos(&ch, &obj, nullptr), WEAR_HEAD);
}

TEST(Equipment, FindEqPosResolvesAnExplicitBodyPartKeyword)
{
    char_data ch {};
    obj_data obj {};
    obj.obj_flags.wear_flags = ITEM_WEAR_HEAD;
    char argument[] = "head";

    EXPECT_EQ(find_eq_pos(&ch, &obj, argument), WEAR_HEAD);
}

// ---------------------------------------------------------------------------
// rots::entity::occupants() (LS-1 Wave Task 1b; .superpowers/sdd/
// ls1-task-1b-report.md). occupant_range/occupants() previously lived only
// inside placement.cpp (TU-local, no header declaration -- see
// ls1-task-2-report.md's "wave-level infrastructure gap" finding) so no
// other TU could call it despite the census's conversion recipe naming it
// as the target for every next_in_room walk. This task moves the class into
// handler.h (still namespace rots::entity) and adds a const overload
// (occupants(const room_data*), yielding const char_data* -- the tranche-A
// review's Finding for char_utils_combat.cpp's get_engaged_characters(),
// which walks a const room_data& and was left raw for want of exactly this
// overload). These tests are the API's own TDD contract proof, consumer-
// free otherwise: an empty range for a null room (both overloads), and the
// const overload yielding the identical pointer sequence as the non-const
// overload over the same chain -- lazy ++ semantics unchanged either way.
// ---------------------------------------------------------------------------

TEST(OccupantsTest, YieldsEmptyRangeForNullRoom)
{
    int count = 0;
    for (char_data* occ : rots::entity::occupants(static_cast<room_data*>(nullptr))) {
        (void)occ;
        ++count;
    }

    EXPECT_EQ(count, 0);
}

TEST(OccupantsTest, ConstOverloadYieldsEmptyRangeForNullRoom)
{
    int count = 0;
    for (const char_data* occ : rots::entity::occupants(static_cast<const room_data*>(nullptr))) {
        (void)occ;
        ++count;
    }

    EXPECT_EQ(count, 0);
}

TEST(OccupantsTest, WalksTheOccupantChainInOrder)
{
    room_data room = make_stub_room();
    char_data first { };
    char_data second { };
    char_data third { };
    first.next_in_room = &second;
    second.next_in_room = &third;
    third.next_in_room = nullptr;
    room.people = &first;

    std::vector<char_data*> collected;
    for (char_data* occ : rots::entity::occupants(&room)) {
        collected.push_back(occ);
    }

    EXPECT_EQ(collected, (std::vector<char_data*> { &first, &second, &third }));
}

TEST(OccupantsTest, ConstOverloadYieldsTheSameSequenceAsTheNonConstOverload)
{
    room_data room = make_stub_room();
    char_data first { };
    char_data second { };
    char_data third { };
    first.next_in_room = &second;
    second.next_in_room = &third;
    third.next_in_room = nullptr;
    room.people = &first;

    std::vector<char_data*> non_const_walk;
    for (char_data* occ : rots::entity::occupants(&room)) {
        non_const_walk.push_back(occ);
    }

    const room_data& const_room = room;
    std::vector<const char_data*> const_walk;
    for (const char_data* occ : rots::entity::occupants(&const_room)) {
        const_walk.push_back(occ);
    }

    ASSERT_EQ(const_walk.size(), non_const_walk.size());
    for (size_t i = 0; i < const_walk.size(); ++i) {
        EXPECT_EQ(const_walk[i], non_const_walk[i]);
    }
}

// ---------------------------------------------------------------------------
// rots::entity::occupants_from() (LS-2 Wave Task 1; .superpowers/sdd/
// ls2-census.md R8). Closes A1's list_char_to_char() (act_info.cpp:1043)
// and A2's act_impl() (comm.cpp:2710) gap with one shared API rather than
// minting a two-site allow-reason: a range-for view over the next_in_room
// chain starting AT an arbitrary char_data* head -- head is the FIRST
// element yielded, reproducing the legacy `for (i = head; i; i =
// i->next_in_room)` idiom verbatim (as opposed to occupants(room), which
// always starts at room->people). Reuses occupant_range's existing
// iterator unchanged via a second constructor that seeds first_ directly
// from head; no new iterator, no duplicated advance/dereference/
// inequality logic. Consumer-free as landed -- T2's later batches are the
// first callers.
// ---------------------------------------------------------------------------

TEST(OccupantsFromTest, YieldsEmptyRangeForNullHead)
{
    int count = 0;
    for (char_data* occ : rots::entity::occupants_from(static_cast<char_data*>(nullptr))) {
        (void)occ;
        ++count;
    }

    EXPECT_EQ(count, 0);
}

TEST(OccupantsFromTest, WalksTheChainFromHeadInOrderHeadIncluded)
{
    char_data first { };
    char_data second { };
    char_data third { };
    first.next_in_room = &second;
    second.next_in_room = &third;
    third.next_in_room = nullptr;

    std::vector<char_data*> collected;
    for (char_data* occ : rots::entity::occupants_from(&first)) {
        collected.push_back(occ);
    }

    EXPECT_EQ(collected, (std::vector<char_data*> { &first, &second, &third }));
}

TEST(OccupantsFromTest, SingleElementChainYieldsOnlyHead)
{
    char_data only { };
    only.next_in_room = nullptr;

    std::vector<char_data*> collected;
    for (char_data* occ : rots::entity::occupants_from(&only)) {
        collected.push_back(occ);
    }

    EXPECT_EQ(collected, (std::vector<char_data*> { &only }));
}

TEST(OccupantsFromTest, StartingMidChainVisitsOnlyThatElementAndItsSuccessors)
{
    char_data first { };
    char_data second { };
    char_data third { };
    first.next_in_room = &second;
    second.next_in_room = &third;
    third.next_in_room = nullptr;

    // Mirrors act_impl()'s TO_ROOM/TO_NOTVICT shape: the head passed in is
    // already mid-chain (not necessarily room->people), so the range must
    // visit exactly that element and its successors -- never "first".
    std::vector<char_data*> collected;
    for (char_data* occ : rots::entity::occupants_from(&second)) {
        collected.push_back(occ);
    }

    EXPECT_EQ(collected, (std::vector<char_data*> { &second, &third }));
}

TEST(OccupantsFromTest, EarlyReturnFromWithinTheRangeForIsSafe)
{
    char_data first { };
    char_data second { };
    first.next_in_room = &second;
    second.next_in_room = nullptr;

    // Mirrors act_impl()'s TO_VICT/TO_CHAR shape: the function returns
    // from inside the loop body after handling exactly one element,
    // abandoning the range-for before it reaches end() -- legal, and must
    // not observe or touch anything past the returned-from element.
    auto find_first = [](char_data* head) -> char_data* {
        for (char_data* occ : rots::entity::occupants_from(head)) {
            return occ;
        }
        return nullptr;
    };

    EXPECT_EQ(find_first(&first), &first);
}

// ---------------------------------------------------------------------------
// rots::entity::first_occupant() (LS-3a Wave T1; ruling R-C6, the wave's ONE
// census-justified new API). Returns a room's first occupant, or nullptr when
// the room is empty -- the shape seven production act()-ANCHOR sites want,
// where the code needs *any* occupant to hang a room-wide message on rather
// than a walk: db_world.cpp:1563/1572/1579/1586/1594 (open/close/lock/unlock/
// break door messages) and limits.cpp:814/815 (object decay). Each of those
// reads room->people straight into a local and then null-tests it, which
// occupants(room) cannot express without a one-iteration loop. Consumer-free
// as landed -- T2 converts the seven sites; nothing calls this yet.
//
// An empty()/size() on occupant_range was ruled YAGNI in the same breath
// (three sites, all already spelled `begin() != end()`), so this is the only
// addition to the occupant surface this wave.
//
// Contract mirrors occupant_range's own: a null room yields "no occupant"
// rather than a dereference, and the read is LIVE -- the head is re-read on
// every call, never cached.
// ---------------------------------------------------------------------------

TEST(FirstOccupantTest, ReturnsNullptrForAnEmptyRoom)
{
    room_data room = make_stub_room();

    EXPECT_EQ(rots::entity::first_occupant(&room), nullptr);
}

TEST(FirstOccupantTest, ReturnsNullptrForANullRoom)
{
    // Matches occupant_range(room_data*)'s null-room contract (an empty
    // range there, "no occupant" here) rather than dereferencing.
    EXPECT_EQ(rots::entity::first_occupant(static_cast<room_data*>(nullptr)), nullptr);
}

TEST(FirstOccupantTest, ReturnsTheOnlyOccupantOfASingleOccupantRoom)
{
    room_data room = make_stub_room();
    char_data only { };
    only.next_in_room = nullptr;
    room.people = &only;

    EXPECT_EQ(rots::entity::first_occupant(&room), &only);
}

TEST(FirstOccupantTest, ReturnsTheHeadOfAMultiOccupantChainNotTheTail)
{
    room_data room = make_stub_room();
    char_data head { };
    char_data middle { };
    char_data tail { };
    room.people = &head;
    head.next_in_room = &middle;
    middle.next_in_room = &tail;
    tail.next_in_room = nullptr;

    EXPECT_EQ(rots::entity::first_occupant(&room), &head);
}

TEST(FirstOccupantTest, AgreesWithTheFirstElementOccupantsYields)
{
    // Ordering consistency with the sibling API: whichever occupant a walk
    // would visit first is the one this returns, so a converted act() anchor
    // addresses the same character the room's chain has always named.
    room_data room = make_stub_room();
    char_data head { };
    char_data second { };
    room.people = &head;
    head.next_in_room = &second;
    second.next_in_room = nullptr;

    char_data* walked_first = nullptr;
    for (char_data* occ : rots::entity::occupants(&room)) {
        walked_first = occ;
        break;
    }

    EXPECT_EQ(rots::entity::first_occupant(&room), walked_first);
    EXPECT_EQ(walked_first, &head);
}

TEST(FirstOccupantTest, ReReadsTheHeadRatherThanCachingIt)
{
    // The seven production anchors read room->people at the moment they need
    // it, after arbitrary intervening work; a head-prepend between two calls
    // must therefore be visible to the second.
    room_data room = make_stub_room();
    char_data original { };
    room.people = &original;
    original.next_in_room = nullptr;

    ASSERT_EQ(rots::entity::first_occupant(&room), &original);

    char_data newcomer { };
    newcomer.next_in_room = &original;
    room.people = &newcomer;

    EXPECT_EQ(rots::entity::first_occupant(&room), &newcomer);
}

TEST(FirstOccupantTest, ConstOverloadYieldsTheSameOccupantAsTheNonConstOverload)
{
    // const counterpart, mirroring occupants(const room_data*): a caller
    // holding only a const room gets a const char_data* back, so it cannot
    // mutate an occupant through this accessor. Overload resolution prefers
    // the non-const form for a non-const argument.
    room_data room = make_stub_room();
    char_data head { };
    char_data second { };
    room.people = &head;
    head.next_in_room = &second;
    second.next_in_room = nullptr;

    const room_data* const const_room = &room;
    const char_data* const via_const = rots::entity::first_occupant(const_room);

    EXPECT_EQ(via_const, &head);
    EXPECT_EQ(via_const, rots::entity::first_occupant(&room));
}

TEST(FirstOccupantTest, ConstOverloadReturnsNullptrForAnEmptyOrNullRoom)
{
    room_data room = make_stub_room();
    const room_data* const const_room = &room;

    EXPECT_EQ(rots::entity::first_occupant(const_room), nullptr);
    EXPECT_EQ(rots::entity::first_occupant(static_cast<const room_data*>(nullptr)), nullptr);
}

// ---------------------------------------------------------------------------
// src/tests/test_placement.h -- the shared location fixtures (LS-3a Wave T1;
// rulings R-B1/R-B3). Both helpers land consumer-free apart from these tests:
// T3's 23-file test-tier migration is what collapses onto them, and T2's new
// tests are the first production-side users of ScopedZoneTableOwner.
//
// ScopedRoomOccupants publishes an ordered occupant chain directly, WITHOUT
// char_to_room() (R-B3): char_to_room() would segfault on a non-NPC for want
// of a zone table, and it appends at the tail where all 22 fixtures this
// replaces publish at the head. ScopedZoneTableOwner is the other side of
// that coin -- the fixture a test needs precisely WHEN it wants the real
// char_to_room()/detach_char_from_room() bookkeeping with a PC.
// ---------------------------------------------------------------------------

TEST(ScopedRoomOccupantsTest, PublishesTheGivenCharactersInTheOrderGiven)
{
    room_data room = make_stub_room();
    char_data first { };
    char_data second { };
    char_data third { };

    ScopedRoomOccupants occupants(&room, 4, { &first, &second, &third });

    // The published chain is what the Stage-1 walk APIs see, in the order the
    // constructor was given -- first argument is the head.
    std::vector<char_data*> collected;
    for (char_data* occ : rots::entity::occupants(&room)) {
        collected.push_back(occ);
    }

    EXPECT_EQ(collected, (std::vector<char_data*> { &first, &second, &third }));
    EXPECT_EQ(rots::entity::first_occupant(&room), &first);
}

TEST(ScopedRoomOccupantsTest, StampsTheGivenRoomIdOnEveryPublishedCharacter)
{
    room_data room = make_stub_room();
    char_data first { };
    char_data second { };
    first.in_room = NOWHERE;
    second.in_room = 99; // a stale location the fixture must overwrite

    ScopedRoomOccupants occupants(&room, 4, { &first, &second });

    EXPECT_TRUE(is_in_room(&first, 4));
    EXPECT_TRUE(is_in_room(&second, 4));
}

TEST(ScopedRoomOccupantsTest, TerminatesThePublishedChainEvenIfTheCharacterCameInLinked)
{
    room_data room = make_stub_room();
    char_data only { };
    char_data stale { };
    only.next_in_room = &stale; // left over from an earlier scope

    ScopedRoomOccupants occupants(&room, 1, { &only });

    EXPECT_EQ(room.people, &only);
    EXPECT_EQ(only.next_in_room, nullptr);
}

TEST(ScopedRoomOccupantsTest, PublishesAnEmptyChainForAnEmptyOccupantSet)
{
    room_data room = make_stub_room();
    char_data resident { };
    room.people = &resident;
    resident.next_in_room = nullptr;

    {
        ScopedRoomOccupants occupants(&room, 1, { });

        EXPECT_EQ(rots::entity::first_occupant(&room), nullptr);
    }

    EXPECT_EQ(room.people, &resident);
}

TEST(ScopedRoomOccupantsTest, RestoresAPreExistingOccupantChainOnDestruction)
{
    room_data room = make_stub_room();
    char_data resident_head { };
    char_data resident_tail { };
    room.people = &resident_head;
    resident_head.next_in_room = &resident_tail;
    resident_tail.next_in_room = nullptr;
    resident_head.in_room = 1;
    resident_tail.in_room = 1;

    char_data visitor { };
    {
        ScopedRoomOccupants occupants(&room, 1, { &visitor });
        ASSERT_EQ(room.people, &visitor);
    }

    // The displaced chain comes back verbatim -- head, internal linkage, and
    // terminator -- and the residents' own locations were never touched: this
    // fixture shadows a chain, it does not relocate anybody.
    EXPECT_EQ(room.people, &resident_head);
    EXPECT_EQ(resident_head.next_in_room, &resident_tail);
    EXPECT_EQ(resident_tail.next_in_room, nullptr);
    EXPECT_EQ(location_of(&resident_head), 1);
    EXPECT_EQ(location_of(&resident_tail), 1);
}

TEST(ScopedRoomOccupantsTest, UnlinksAndClearsEveryManagedCharacterOnDestruction)
{
    room_data room = make_stub_room();
    char_data first { };
    char_data second { };

    {
        ScopedRoomOccupants occupants(&room, 4, { &first, &second });
        ASSERT_EQ(first.next_in_room, &second);
    }

    // THE FIXTURE-HYGIENE RULE: nothing this fixture published may outlive the
    // scope, either linked into the room's chain or still claiming to be in
    // the room. ctest is structurally blind to that class of leak -- every
    // test runs in its own process -- so the helper has to close it by
    // construction, not by each caller remembering.
    EXPECT_EQ(room.people, nullptr);
    EXPECT_EQ(first.next_in_room, nullptr);
    EXPECT_EQ(second.next_in_room, nullptr);
    EXPECT_EQ(location_of(&first), NOWHERE);
    EXPECT_EQ(location_of(&second), NOWHERE);
}

TEST(ScopedRoomOccupantsTest, PublishesANonNpcWithALitLightWithoutZoneTableOrLightBookkeeping)
{
    // Ruling R-B3, both halves in one test. This character is a non-NPC
    // carrying a lit light source, and NO zone table is installed: char_to_room()
    // would dereference zone_by_id(0) -- nullptr here -- for the zone-power
    // block, and would increment room.light. The fixture does neither, and it
    // publishes at the HEAD where char_to_room() appends at the tail (the
    // resident below would still be room.people after a char_to_room() call).
    room_data room = make_stub_room();
    room.light = 0;

    char_data resident { };
    resident.specials2.act |= MOB_ISNPC;
    room.people = &resident;
    resident.next_in_room = nullptr;

    char_data player { };
    player.player.race = RACE_HUMAN;
    player.player.level = 20;
    obj_data lamp { };
    lamp.obj_flags.type_flag = ITEM_LIGHT;
    lamp.obj_flags.value[2] = 5; // fuel remaining
    lamp.obj_flags.value[3] = 1; // ON
    player.equipment[WEAR_LIGHT] = &lamp;

    ScopedRoomOccupants occupants(&room, 3, { &player });

    EXPECT_EQ(room.people, &player);
    EXPECT_EQ(room.light, 0);
    EXPECT_TRUE(is_in_room(&player, 3));
}

TEST(ScopedZoneTableOwnerTest, MakesZoneByIdResolveTheOwnedZone)
{
    ScopedZoneTableOwner zone_guard;

    ASSERT_NE(zone_by_id(0), nullptr);
    EXPECT_EQ(zone_by_id(0), &zone_guard.zone());
    EXPECT_EQ(zone_by_id(0)->number, 0);
    EXPECT_EQ(zone_guard.zone().white_power, 0);
    EXPECT_EQ(zone_guard.zone().dark_power, 0);
}

TEST(ScopedZoneTableOwnerTest, AddressesExactlyOneZoneBecauseTopOfZoneTableIsACount)
{
    ScopedZoneTableOwner zone_guard;

    // zone_by_id_impl() bounds-checks with `znum >= top_of_zone_table`, i.e.
    // it reads the global as a COUNT -- the opposite of top_of_world's
    // inclusive convention next door. 1 makes exactly zone 0 addressable; a 0
    // would resolve nothing at all, which is the trap this fixture exists to
    // keep out of every caller.
    EXPECT_EQ(top_of_zone_table, 1);
    EXPECT_NE(zone_by_id(0), nullptr);
    EXPECT_EQ(zone_by_id(1), nullptr);
    EXPECT_EQ(zone_by_id(-1), nullptr);
}

TEST(ScopedZoneTableOwnerTest, RestoresThePreviousZoneTableOnDestruction)
{
    zone_data* const table_before = zone_table;
    const int top_before = top_of_zone_table;

    {
        ScopedZoneTableOwner zone_guard;
        ASSERT_NE(zone_table, table_before);
        ASSERT_EQ(top_of_zone_table, 1);
    }

    EXPECT_EQ(zone_table, table_before);
    EXPECT_EQ(top_of_zone_table, top_before);
}

TEST(ScopedZoneTableOwnerTest, LetsCharToRoomAccountZonePowerForANonNpcWithoutCrashing)
{
    // The marquee case: the real primitives, the real world[] and zone_table,
    // and a PC. Without the zone-table owner this test segfaults inside
    // char_to_room()'s `zone_by_id(r->zone)->white_power += tmp` -- that is the
    // whole reason the fixture exists, and why T2's new char_to_room() tests
    // are required to stand one up.
    ScopedTestWorld world_guard;
    ScopedZoneTableOwner zone_guard;

    world_guard.room().zone = 0;

    char_data player { };
    player.player.race = RACE_HUMAN; // RACE_GOOD -> white_power
    player.player.level = 20;
    ASSERT_FALSE(IS_NPC(&player));

    const int expected_power = char_power(player.player.level);
    ASSERT_GT(expected_power, 0);

    char_to_room(&player, 0);

    EXPECT_TRUE(is_in_room(&player, 0));
    EXPECT_EQ(rots::entity::first_occupant(&world_guard.room()), &player);
    EXPECT_EQ(zone_guard.zone().white_power, expected_power);
    EXPECT_EQ(zone_guard.zone().dark_power, 0);

    // `player` is a stack char_data that a production call linked into the
    // process-global world[0].people, so it comes back out through the real
    // primitive (THE FIXTURE-HYGIENE RULE) -- which doubles as proof that the
    // zone accounting this fixture makes reachable is symmetric.
    EXPECT_TRUE(detach_char_from_room(&player));
    EXPECT_EQ(zone_guard.zone().white_power, 0);
    EXPECT_EQ(location_of(&player), NOWHERE);
    EXPECT_EQ(rots::entity::first_occupant(&world_guard.room()), nullptr);
}

// ---------------------------------------------------------------------------
// relocate_all_occupants() / relocate_all_contents() -- LS-3a T2c, ruling R-A6
// (.superpowers/sdd/ls3a-global-constraints.md; census A section 5.2).
//
// These two primitives encapsulate the bulk room-to-room splice that was
// hand-rolled inside spec_pro.cpp's ferry_captain(): the ONLY manual char-chain
// splice left in production, and the one block whose semantics were wholly
// representation-dependent. Moving it into the representation owners is what
// lets LS-3b re-point ONE function instead of rewriting a hand-rolled loop.
//
// THE CONTRACT THESE TESTS PIN IS THE ONE COMMIT 1 CHARACTERIZED against the
// real ferry_captain() (SpecProFerryCaptain.*, spec_pro_tests.cpp) -- items 1-6
// of that suite's own contract list, restated here per half:
//   1. combined chain order is SOURCE-then-DESTINATION;
//   2. it is published at the destination, source head nulled;
//   3. every member is re-stamped to the destination -- including the
//      destination's own pre-existing members, for whom it is a no-op;
//   4. objects follow the identical rule through room_data::contents /
//      obj_data::next_content / obj_data::in_room;
//   5. an empty source degrades to "destination chain untouched, source stays
//      empty";
//   6. NO light bookkeeping and NO zone white/dark-power bookkeeping -- these
//      are deliberately NOT char_to_room()/detach_char_from_room().
// The seventh (the same-room early return) is NEW: see its test below.
//
// Every room these tests hand the resolvers is a stack room_data, and every
// character/object is a stack fixture, so nothing here links anything into a
// process-global chain -- THE FIXTURE-HYGIENE RULE is satisfied structurally
// rather than by teardown.
// ---------------------------------------------------------------------------

namespace {

// The two room ids the per-id stub resolver dispatch above is keyed on. Values
// are arbitrary and deliberately non-adjacent and non-zero: the primitives pass
// their `to_room` argument straight through as the re-stamped location, so a
// body that stamped an index it derived some other way would still look right
// against 0/1.
constexpr int kSourceRoomId = 11;
constexpr int kDestinationRoomId = 27;

// A char_data with only the fields these primitives read or write set. The
// primitives touch nothing else (no equipment walk, no affect handling, no
// level/race read -- that is contract item 6's whole point), so a fuller
// fixture would assert nothing extra.
struct RelocationCharacters {
    char_data first {};
    char_data second {};
    char_data third {};
    char_data resident {}; // starts in the DESTINATION room.
};

struct RelocationObjects {
    obj_data first {};
    obj_data second {};
    obj_data crate {}; // starts in the DESTINATION room.
};

// Publishes `occupants` as `room`'s chain, head-first in the order given, each
// stamped with `room_id`. The ScopedRoomOccupants shape (test_placement.h)
// without the RAII half: these rooms are stack fixtures, so there is nothing
// process-global to restore.
void publish_chain(room_data& room, int room_id, std::initializer_list<char_data*> occupants)
{
    char_data* previous = nullptr;
    for (char_data* occupant : occupants) {
        if (previous == nullptr) {
            room.people = occupant; // LS1-ALLOW: representation-impl (test fixture -- publishing the chain head)
        } else {
            previous->next_in_room = occupant; // LS1-ALLOW: representation-impl (test fixture -- linking the chain in the order given)
        }
        set_location(occupant, room_id);
        previous = occupant;
    }
    if (previous == nullptr) {
        room.people = nullptr; // LS1-ALLOW: representation-impl (test fixture -- an empty occupant set publishes an empty chain)
    } else {
        previous->next_in_room = nullptr; // LS1-ALLOW: representation-impl (test fixture -- terminating the published chain)
    }
}

void publish_contents(room_data& room, int room_id, std::initializer_list<obj_data*> objects)
{
    obj_data* previous = nullptr;
    for (obj_data* object : objects) {
        if (previous == nullptr) {
            room.contents = object;
        } else {
            previous->next_content = object;
        }
        object->in_room = room_id; // LS1-ALLOW: obj-location (test fixture -- obj_data::in_room, not a character location)
        previous = object;
    }
    if (previous == nullptr) {
        room.contents = nullptr;
    } else {
        previous->next_content = nullptr;
    }
}

// Source + destination rooms wired into the stub resolvers under the two ids
// above, plus a zone every zone_by_id() call resolves to. The zone exists only
// so contract item 6 is measurable: neither primitive calls zone_by_id(), and
// a test asserting counters that could not move otherwise would prove nothing.
struct RelocationContext {
    room_data source = make_stub_room();
    room_data destination = make_stub_room();
    zone_data zone = make_stub_zone();
    StubWorldResolvers resolvers;

    RelocationContext()
    {
        resolvers.room = &source; // fallback for any unmapped id.
        resolvers.zone = &zone;
        resolvers.rooms_by_id[kSourceRoomId] = &source;
        resolvers.rooms_by_id[kDestinationRoomId] = &destination;
        source.light = 3;
        destination.light = 5;
    }
};

} // namespace

TEST(RelocateAllOccupants, MovesTheSourceChainAheadOfTheDestinationsExistingOccupants) {
    RelocationContext context;
    RelocationCharacters characters;
    ScopedWorldResolverHooks hooks(context.resolvers);

    publish_chain(context.source, kSourceRoomId,
        { &characters.first, &characters.second, &characters.third });
    publish_chain(context.destination, kDestinationRoomId, { &characters.resident });

    relocate_all_occupants(kSourceRoomId, kDestinationRoomId);

    // Contract items 1/2.
    EXPECT_EQ(context.destination.people, &characters.first);
    EXPECT_EQ(characters.first.next_in_room, &characters.second);
    EXPECT_EQ(characters.second.next_in_room, &characters.third);
    EXPECT_EQ(characters.third.next_in_room, &characters.resident)
        << "MERGE ORDER IS THE CONTRACT: the arriving chain keeps its relative order and the "
           "destination's pre-existing occupants are appended AFTER it, matching what "
           "SpecProFerryCaptain.SplicesTheSourceCabinChainAheadOfTheDestinationsAndRestampsEveryMember "
           "characterized against the real ferry_captain().";
    EXPECT_EQ(characters.resident.next_in_room, nullptr);
    EXPECT_EQ(context.source.people, nullptr);
}

TEST(RelocateAllOccupants, RestampsEveryMergedMemberIncludingTheDestinationsOwn) {
    RelocationContext context;
    RelocationCharacters characters;
    ScopedWorldResolverHooks hooks(context.resolvers);

    publish_chain(context.source, kSourceRoomId,
        { &characters.first, &characters.second, &characters.third });
    publish_chain(context.destination, kDestinationRoomId, { &characters.resident });

    relocate_all_occupants(kSourceRoomId, kDestinationRoomId);

    // Contract item 3. The resident's stamp is a no-op, but the walk covers it,
    // and the ferry's own splice covered it the same way.
    EXPECT_EQ(location_of(&characters.first), kDestinationRoomId);
    EXPECT_EQ(location_of(&characters.second), kDestinationRoomId);
    EXPECT_EQ(location_of(&characters.third), kDestinationRoomId);
    EXPECT_EQ(location_of(&characters.resident), kDestinationRoomId);
}

TEST(RelocateAllOccupants, MovesTheWholeChainIntoAnEmptyDestinationAndKeepsItsTerminator) {
    RelocationContext context;
    RelocationCharacters characters;
    ScopedWorldResolverHooks hooks(context.resolvers);

    publish_chain(context.source, kSourceRoomId, { &characters.first, &characters.second });
    publish_chain(context.destination, kDestinationRoomId, { });

    relocate_all_occupants(kSourceRoomId, kDestinationRoomId);

    EXPECT_EQ(context.destination.people, &characters.first);
    EXPECT_EQ(characters.first.next_in_room, &characters.second);
    EXPECT_EQ(characters.second.next_in_room, nullptr)
        << "With nothing already at the destination the arriving tail keeps its null terminator "
           "rather than being linked onto a pre-existing head.";
    EXPECT_EQ(context.source.people, nullptr);
    EXPECT_EQ(location_of(&characters.second), kDestinationRoomId);
}

TEST(RelocateAllOccupants, LeavesTheDestinationChainIntactWhenTheSourceIsEmpty) {
    RelocationContext context;
    RelocationCharacters characters;
    ScopedWorldResolverHooks hooks(context.resolvers);

    publish_chain(context.source, kSourceRoomId, { });
    publish_chain(context.destination, kDestinationRoomId, { &characters.resident });

    relocate_all_occupants(kSourceRoomId, kDestinationRoomId);

    // Contract item 5, the `if (tmpch)` ELSE arm -- the arm
    // SpecProFerryCaptain.LeavesADestinationCabinIntactWhenTheSourceCabinsChainIsEmpty
    // had to construct a deliberately unlinked captain to reach through the
    // real SPECIAL, and which this primitive test reaches directly.
    EXPECT_EQ(context.destination.people, &characters.resident);
    EXPECT_EQ(characters.resident.next_in_room, nullptr);
    EXPECT_EQ(location_of(&characters.resident), kDestinationRoomId);
    EXPECT_EQ(context.source.people, nullptr);
}

TEST(RelocateAllOccupants, LeavesTheRoomUntouchedWhenSourceAndDestinationAreTheSameRoom) {
    RelocationContext context;
    RelocationCharacters characters;
    ScopedWorldResolverHooks hooks(context.resolvers);

    publish_chain(context.source, kSourceRoomId, { &characters.first, &characters.second });

    relocate_all_occupants(kSourceRoomId, kSourceRoomId);

    // NEW behavior, not characterized behavior, and deliberately unreachable
    // from the only production caller: ferry_captain() keeps its own
    // `if (new_room != old_room)` guard, exactly as before. Without this early
    // return the splice would link the chain's own tail back onto its own head
    // and then null the room -- a cycle plus a lost chain. Making the primitive
    // total closes that footgun inside the representation owner instead of
    // leaving it for LS-3b to trip over.
    EXPECT_EQ(context.source.people, &characters.first);
    EXPECT_EQ(characters.first.next_in_room, &characters.second);
    EXPECT_EQ(characters.second.next_in_room, nullptr);
    EXPECT_EQ(location_of(&characters.first), kSourceRoomId);
}

TEST(RelocateAllOccupants, PerformsNoLightOrZonePowerBookkeeping) {
    RelocationContext context;
    RelocationCharacters characters;
    ScopedWorldResolverHooks hooks(context.resolvers);

    // Non-NPC with a RACE_GOOD race (GET_RACE in [1, 9], utils.h): this is what
    // char_to_room()/detach_char_from_room() would credit to a zone.
    characters.first.player.race = 1;
    characters.second.player.race = 1;
    publish_chain(context.source, kSourceRoomId, { &characters.first, &characters.second });
    publish_chain(context.destination, kDestinationRoomId, { });

    relocate_all_occupants(kSourceRoomId, kDestinationRoomId);

    // Contract item 6 -- the single most load-bearing difference between this
    // primitive and the real char_to_room()/detach_char_from_room() pair.
    EXPECT_EQ(context.source.light, 3);
    EXPECT_EQ(context.destination.light, 5);
    EXPECT_EQ(context.zone.white_power, 0);
    EXPECT_EQ(context.zone.dark_power, 0);
}

TEST(RelocateAllContents, MovesTheSourceContentsAheadOfTheDestinationsExisting) {
    RelocationContext context;
    RelocationObjects objects;
    ScopedWorldResolverHooks hooks(context.resolvers);

    publish_contents(context.source, kSourceRoomId, { &objects.first, &objects.second });
    publish_contents(context.destination, kDestinationRoomId, { &objects.crate });

    relocate_all_contents(kSourceRoomId, kDestinationRoomId);

    // Contract items 1/2/4 -- the object half, same merge order.
    EXPECT_EQ(context.destination.contents, &objects.first);
    EXPECT_EQ(objects.first.next_content, &objects.second);
    EXPECT_EQ(objects.second.next_content, &objects.crate);
    EXPECT_EQ(objects.crate.next_content, nullptr);
    EXPECT_EQ(context.source.contents, nullptr);
}

TEST(RelocateAllContents, RestampsEveryMergedObjectIncludingTheDestinationsOwn) {
    RelocationContext context;
    RelocationObjects objects;
    ScopedWorldResolverHooks hooks(context.resolvers);

    publish_contents(context.source, kSourceRoomId, { &objects.first, &objects.second });
    publish_contents(context.destination, kDestinationRoomId, { &objects.crate });

    relocate_all_contents(kSourceRoomId, kDestinationRoomId);

    // Contract items 3/4. obj_data::in_room is the object-location field, a
    // different representation from char_data::in_room -- read raw here.
    EXPECT_EQ(objects.first.in_room, kDestinationRoomId); // LS1-ALLOW: obj-location
    EXPECT_EQ(objects.second.in_room, kDestinationRoomId); // LS1-ALLOW: obj-location
    EXPECT_EQ(objects.crate.in_room, kDestinationRoomId); // LS1-ALLOW: obj-location
}

TEST(RelocateAllContents, MovesTheWholeChainIntoAnEmptyDestinationAndKeepsItsTerminator) {
    RelocationContext context;
    RelocationObjects objects;
    ScopedWorldResolverHooks hooks(context.resolvers);

    publish_contents(context.source, kSourceRoomId, { &objects.first, &objects.second });
    publish_contents(context.destination, kDestinationRoomId, { });

    relocate_all_contents(kSourceRoomId, kDestinationRoomId);

    EXPECT_EQ(context.destination.contents, &objects.first);
    EXPECT_EQ(objects.second.next_content, nullptr);
    EXPECT_EQ(context.source.contents, nullptr);
    EXPECT_EQ(objects.second.in_room, kDestinationRoomId); // LS1-ALLOW: obj-location
}

TEST(RelocateAllContents, LeavesTheDestinationChainIntactWhenTheSourceIsEmpty) {
    RelocationContext context;
    RelocationObjects objects;
    ScopedWorldResolverHooks hooks(context.resolvers);

    publish_contents(context.source, kSourceRoomId, { });
    publish_contents(context.destination, kDestinationRoomId, { &objects.crate });

    relocate_all_contents(kSourceRoomId, kDestinationRoomId);

    // Contract item 5, the `if (tmpobj)` ELSE arm.
    EXPECT_EQ(context.destination.contents, &objects.crate);
    EXPECT_EQ(objects.crate.next_content, nullptr);
    EXPECT_EQ(objects.crate.in_room, kDestinationRoomId); // LS1-ALLOW: obj-location
    EXPECT_EQ(context.source.contents, nullptr);
}

TEST(RelocateAllContents, LeavesTheRoomUntouchedWhenSourceAndDestinationAreTheSameRoom) {
    RelocationContext context;
    RelocationObjects objects;
    ScopedWorldResolverHooks hooks(context.resolvers);

    publish_contents(context.source, kSourceRoomId, { &objects.first, &objects.second });

    relocate_all_contents(kSourceRoomId, kSourceRoomId);

    // Same NEW same-room early return as relocate_all_occupants() above, and
    // equally unreachable from ferry_captain(), which keeps its own guard.
    EXPECT_EQ(context.source.contents, &objects.first);
    EXPECT_EQ(objects.first.next_content, &objects.second);
    EXPECT_EQ(objects.second.next_content, nullptr);
    EXPECT_EQ(objects.first.in_room, kSourceRoomId); // LS1-ALLOW: obj-location
}

TEST(RelocateAllContents, PerformsNoLightBookkeeping) {
    RelocationContext context;
    RelocationObjects objects;
    ScopedWorldResolverHooks hooks(context.resolvers);

    // A lit ITEM_LIGHT: obj_to_room()/obj_from_room() (this file's own
    // containment.cpp neighbours) would move a room's .light for exactly this
    // object. The bulk move deliberately does not, which is contract item 6's
    // object half.
    objects.first.obj_flags.type_flag = ITEM_LIGHT;
    objects.first.obj_flags.value[2] = 1;
    objects.first.obj_flags.value[3] = 1;
    publish_contents(context.source, kSourceRoomId, { &objects.first });
    publish_contents(context.destination, kDestinationRoomId, { });

    relocate_all_contents(kSourceRoomId, kDestinationRoomId);

    EXPECT_EQ(context.source.light, 3);
    EXPECT_EQ(context.destination.light, 5);
}
