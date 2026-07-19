// placement_tests.cpp

// New test TU (placement-seam wave, Task 6; plan
// docs/superpowers/plans/2026-07-19-placement-seam.md; brief
// .superpowers/sdd/task-6-brief.md). Standing coverage-gap rule: Task 3
// carved detach_char_from_room()/char_to_room() (placement.cpp) and
// attach_equipment()/detach_equipment() (equipment.cpp) as new lib-resident
// primitives behind the three-resolver world seam (entity_hooks.h) -- boot
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
#include "rots/core/character.h"
#include "rots/core/object.h"
#include "rots/core/room.h"
#include "rots/core/types.h"

#include <gtest/gtest.h>

// encumb_table[]/leg_encumb_table[] (consts.cpp, L1 core data table) -- same
// local extern-declaration pattern equipment.cpp itself uses (no shared
// header declares them); needed here so the encumbrance-delta tests can
// cross-check attach_equipment()'s/detach_equipment()'s arithmetic against
// the same table the primitive reads, rather than hand-duplicating its
// values.
extern sh_int encumb_table[MAX_WEAR];
extern sh_int leg_encumb_table[MAX_WEAR];

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
};

// The fixture the file-scope trampoline functions below dispatch to; set by
// whichever ScopedWorldResolverHooks is currently alive. entity_hooks.h's
// hook typedefs are bare function pointers, not std::function, so the
// trampolines cannot capture a fixture reference directly -- this
// indirection stands in for that capture (mirrors entity_lifecycle_tests.cpp's
// g_active_pool).
StubWorldResolvers* g_active_resolvers = nullptr;

room_data* test_room_by_id(int /*rnum*/) { return g_active_resolvers->room; }

room_data* test_room_by_id_total(int /*rnum*/) { return g_active_resolvers->room; }

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
// re-registers all three together, matching its own all-or-nothing
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
    // -- if wrongly applied BEFORE the check -- would raise bal_str to 60
    // (threshold 3000) and make 600 NOT too heavy. A regressed
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
