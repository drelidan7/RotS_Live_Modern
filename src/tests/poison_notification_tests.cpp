// poison_notification_tests.cpp

// New test TU (blocker-buster wave, Task 3; plan
// docs/superpowers/plans/2026-07-19-blocker-buster.md; brief
// .superpowers/sdd/task-3-brief.md; census section E,
// .superpowers/sdd/blocker-census.md). BINDING addendum 1's Step 1:
// characterizes the CURRENT (pre-inversion) mudscript poison path through
// the REAL script-execution machinery -- script.cpp's run_script() walking
// a SCRIPT_OBJ_FROM_CHAR command, exactly as script.cpp:1461 dispatches it
// -- reaching handler.cpp's obj_from_char() with a genuinely EQUIPPED,
// poison-bitvector item. obj_from_char()'s equipment-fallback branch (the
// object sits in ch->equipment[], not ch->carrying) calls unequip_char()
// (the app-tier wrapper, not equipment.cpp's detach_equipment() primitive)
// as of this test's addition; unequip_char()'s poison damage()/raw_kill()
// block fires whenever removing the item cures AFF_POISON -- the live
// mudscript counter-example placement-seam Task 3's STOP-CHECK found
// (script.cpp's SCRIPT_ASSIGN_EQ + SCRIPT_OBJ_FROM_CHAR, task-3-report.md).
//
// This test is added and verified GREEN against that CURRENT code before
// any inversion work on obj_from_char()/extract_obj() begins (this wave's
// own task-3-report.md records the exact timeline). It must keep passing
// UNCHANGED once containment.cpp's post-inversion obj_from_char() replaces
// the unequip_char() call with detach_equipment() plus the new
// poison-removal notification hook (entity_hooks.h) -- that is Step 3's
// identity proof, and the reason this file lives outside containment.cpp's
// own unit coverage: it exercises the PUBLIC, script-observable behavior,
// not either implementation's internals, so it cannot be satisfied by
// merely testing that some hook fired.

#include "../db.h"
#include "../handler.h"
#include "../protos.h"
#include "../script.h"
#include "../spells.h"
#include "../utils.h"
#include "rots/core/character.h"
#include "rots/core/object.h"
#include "test_world.h"

#include <gtest/gtest.h>

// run_script() (script.cpp) has no header declaration anywhere in the tree --
// every existing caller is script.cpp itself (trigger_char_damage() and its
// sibling trigger_* helpers). This is the first cross-TU caller, so it needs
// the same local-extern treatment the tree already gives raw_kill() (also
// declared nowhere centrally, forward-declared locally by each caller).
extern int run_script(struct info_script* info, struct script_data* position);

extern char_data* combat_list;
extern char_data* combat_next_dude;

namespace {

// A single self-damaging NPC fixture. The wrapper's real poison call is
// damage(ch, ch, 5, SPELL_POISON, 0) -- attacker and victim are the SAME
// char_data*, not merely equal contents -- so every one of damage()'s
// `victim != attacker` branches (set_fighting()/stop_riding()/group-leader
// teardown/gain_exp()/the PC-vs-PC incap hack) is structurally unreachable
// here, exactly as it is for the real self-poison call. specials.fighting
// is pre-set to `&character` (itself) so damage()'s own
// `victim->specials.fighting != attacker` branch -- which would otherwise
// invoke special(), a much larger mob/room-special dispatcher this fixture
// does not stand up -- is false from the first call; this collapses
// damage_test_context.h's two-character `attacker.specials.fighting =
// &victim` / `victim.specials.fighting = &attacker` wiring onto one object.
class PoisonRemovalScriptTest : public ::testing::Test {
protected:
    // Room 1, not ScopedTestWorld's canonical room 0 -- mirrors
    // damage_test_context.h's DamageTestContext, which documents why: room 0
    // is ScopedTestWorld's own name/description-owning slot.
    static constexpr int room_number = 1;
    static_assert(room_number < EXTENSION_SIZE - 1,
        "room_number must stay inside the range create_bulk(1) dummy-initializes");

    ScopedTestWorld test_world;
    char_data character {};
    char character_name[24] = "test_poison_wearer";
    char_data* original_people = nullptr;

    void SetUp() override
    {
        top_of_world = room_number;
        original_people = world[room_number].people;

        character.specials2.act = MOB_ISNPC;
        character.player.short_descr = character_name;
        character.player.race = RACE_HUMAN;
        character.player.level = 20;
        character.tmpabilities.con = 20;
        character.abilities.hit = 500;
        character.tmpabilities.hit = 500;
        character.tmpabilities.mana = 100;
        character.specials.position = POSITION_FIGHTING;
        character.specials.fighting = &character;
        character.in_room = room_number;
        character.next_in_room = nullptr;
        world[room_number].people = &character;
    }

    void TearDown() override
    {
        world[room_number].people = original_people;
        character.next_in_room = nullptr;
        character.specials.fighting = nullptr;
        character.in_room = NOWHERE;
        combat_list = nullptr;
        combat_next_dude = nullptr;
    }

    // Equips `item` (caller-owned -- this fixture never returns a
    // stack-local obj_data by value, which would leave character.equipment[]
    // pointing at a destroyed object) directly into `slot`, bypassing
    // equip_char()/attach_equipment() entirely: this test characterizes
    // REMOVAL, not attachment, and obj_from_char()'s equipment-fallback
    // branch only needs the post-equip state -- carried_by set, but the
    // object absent from ->carrying.
    void equip_item(obj_data& item, int slot, long bitvector)
    {
        item.obj_flags.bitvector = bitvector;
        item.carried_by = &character;
        character.equipment[slot] = &item;
        character.carrying = nullptr;
    }

    // Runs the real script machinery: a single SCRIPT_OBJ_FROM_CHAR command
    // (script.cpp:1461's case) whose param slots this call pre-populates
    // directly -- standing in for what a preceding SCRIPT_ASSIGN_EQ command
    // (script.cpp:817, the BINDING addendum's cited trigger) would have
    // populated -- chained to a SCRIPT_ABORT terminator so run_script()'s
    // command loop cannot walk off the end of a one-command list.
    void run_obj_from_char_script(obj_data& item)
    {
        info_script info {};
        info.ob[0] = &item;
        info.ch[0] = &character;

        script_data terminator {};
        terminator.command_type = SCRIPT_ABORT;

        script_data command {};
        command.command_type = SCRIPT_OBJ_FROM_CHAR;
        command.param[0] = SCRIPT_PARAM_OB1;
        command.param[1] = SCRIPT_PARAM_CH1;
        command.next = &terminator;

        run_script(&info, &command);
    }
};

} // namespace

TEST_F(PoisonRemovalScriptTest, RemovingAnEquippedPoisonItemThatCuresPoisonFiresTheWrapperDamageBlock)
{
    character.specials.affected_by |= AFF_POISON; // already poisoned by the worn item
    obj_data item {};
    equip_item(item, WEAR_BODY, AFF_POISON);

    run_obj_from_char_script(item);

    // obj_from_char()'s equipment-fallback branch found the item at
    // equipment[WEAR_BODY] (not in ->carrying), so it called unequip_char() --
    // detach_equipment()'s affect_modify(..., AFFECT_MODIFY_REMOVE, ...) loop
    // cleared AFF_POISON (obj_flags.bitvector's unconditional REMOVE_BIT), and
    // unequip_char()'s own poison block then fired damage(character, character,
    // 5, SPELL_POISON, 0) because was_poisoned was true and IS_AFFECTED(...,
    // AFF_POISON) had just gone false.
    EXPECT_FALSE(character.equipment[WEAR_BODY]) << "Expected the equipment-fallback branch to clear the slot.";
    EXPECT_EQ(item.carried_by, nullptr) << "Expected obj_from_char() to detach the item from its wearer.";
    EXPECT_FALSE(IS_AFFECTED(&character, AFF_POISON)) << "Expected removing the item to cure the poison affect.";
    EXPECT_EQ(GET_HIT(&character), 495)
        << "Expected the wrapper's poison block to apply exactly 5 points of SPELL_POISON damage "
           "(no resistance/vulnerability/shield modifiers are wired up in this fixture, so damage() "
           "applies the literal `dam` argument unmodified).";
}

TEST_F(PoisonRemovalScriptTest, RemovingAnEquippedNonPoisonItemDoesNotFireTheDamageBlock)
{
    // Negative control: the item carries no AFF_POISON bit and the wearer was
    // never poisoned, so unequip_char()'s `was_poisoned != 0 &&
    // !IS_AFFECTED(...)` guard is false by construction -- proving this
    // characterization is actually sensitive to the poison condition, not
    // merely to obj_from_char() reaching the equipment-fallback branch at all.
    obj_data item {};
    equip_item(item, WEAR_BODY, 0);

    run_obj_from_char_script(item);

    EXPECT_FALSE(character.equipment[WEAR_BODY]);
    EXPECT_EQ(item.carried_by, nullptr);
    EXPECT_FALSE(IS_AFFECTED(&character, AFF_POISON));
    EXPECT_EQ(GET_HIT(&character), 500) << "Expected no poison damage when the wearer was never poisoned.";
}
