// src/tests/caster_snapshot_tests.cpp
#include "../char_utils.h"
#include "../handler.h"
#include "../spells.h"
#include "../utils.h"
#include "rots/core/caster_snapshot.h"
#include "rots/core/character.h"
#include "test_random_utils.h"
#include <gtest/gtest.h>

namespace {
void make_mage(char_data& ch, char_prof_data& profs) {
    ch.profs = &profs;
    profs.prof_level[PROF_MAGE] = 25;
    profs.prof_level[PROF_CLERIC] = 3;
    profs.specialization = static_cast<int>(game_types::PS_Fire);
    ch.player.level = 30;
    ch.player.race = RACE_HUMAN;
    ch.tmpabilities.intel = 21;
    ch.tmpabilities.wil = 17;
    ch.points.spell_power = 4;
    ch.points.spell_pen = 2;
    ch.points.willpower = 9;
    ch.abs_number = 7903; // < 8001, the char_exists table size
}
} // namespace

TEST(CasterSnapshot, CaptureCopiesEveryFormulaInput) {
    char_data ch {};
    char_prof_data profs {};
    make_mage(ch, profs);
    const caster_snapshot snap = caster_snapshot::capture(ch);
    EXPECT_EQ(snap.abs_number, 7903);
    EXPECT_EQ(snap.identity_ptr, &ch);
    EXPECT_EQ(snap.mage_prof_level, 25);
    EXPECT_EQ(snap.cleric_prof_level, 3);
    EXPECT_EQ(snap.intel, 21);
    EXPECT_EQ(snap.wil, 17);
    EXPECT_EQ(snap.spell_power, 4);
    EXPECT_EQ(snap.spell_pen, 2);
    EXPECT_EQ(snap.willpower, 9);
    EXPECT_EQ(snap.specialization, game_types::PS_Fire);
    EXPECT_EQ(snap.race, RACE_HUMAN);
    EXPECT_FALSE(snap.is_npc);
    EXPECT_FALSE(snap.is_none());
}

TEST(CasterSnapshot, LaterStatChangesDoNotReachTheSnapshot) {
    char_data ch {};
    char_prof_data profs {};
    make_mage(ch, profs);
    const caster_snapshot snap = caster_snapshot::capture(ch);
    ch.tmpabilities.intel = 3; // the death penalty shape
    profs.prof_level[PROF_MAGE] = 1;
    EXPECT_EQ(snap.intel, 21);
    EXPECT_EQ(snap.mage_prof_level, 25);
}

TEST(CasterSnapshot, ResolveRequiresTheSameRegisteredCharacter) {
    char_data ch {};
    char_prof_data profs {};
    make_mage(ch, profs);
    set_char_exists(ch.abs_number, &ch);
    const caster_snapshot snap = caster_snapshot::capture(ch);
    EXPECT_EQ(snap.resolve(), &ch);
    remove_char_exists(ch.abs_number);
    EXPECT_EQ(snap.resolve(), nullptr) << "an extracted caster resolves to nobody";

    // The slot is recycled: a DIFFERENT character is registered under the
    // same abs_number. resolve() must recognize the mismatch without ever
    // dereferencing snap's own (now-stale) identity_ptr -- it only compares
    // that pointer value against char_by_abs_number()'s report of the
    // CURRENT owner, so this stays safe even when the old pointer is
    // dangling (a freed char_data, in real usage).
    char_data other {};
    char_prof_data other_profs {};
    make_mage(other, other_profs); // make_mage always lands on abs_number 7903
    set_char_exists(other.abs_number, &other);
    EXPECT_EQ(snap.resolve(), nullptr);
    remove_char_exists(7903);
}

TEST(CasterSnapshot, SameCharacterAsRequiresPointerAndNumberToMatch) {
    char_data ch {};
    char_prof_data profs {};
    make_mage(ch, profs);
    const caster_snapshot snap = caster_snapshot::capture(ch);
    EXPECT_TRUE(snap.same_character_as(ch));

    char_data other {};
    char_prof_data other_profs {};
    make_mage(other, other_profs); // a different char_data at the same abs_number
    EXPECT_FALSE(snap.same_character_as(other));

    ch.abs_number = 7904; // same pointer, but abs_number changed since capture
    EXPECT_FALSE(snap.same_character_as(ch));

    const caster_snapshot none = caster_snapshot::none();
    EXPECT_FALSE(none.same_character_as(ch));
}

TEST(CasterSnapshot, NoneIsNeverResolvable) {
    const caster_snapshot none = caster_snapshot::none();
    EXPECT_TRUE(none.is_none());
    EXPECT_EQ(none.resolve(), nullptr);
}


// ---------------------------------------------------------------------------
// TASK-021 Task 2: every combat formula helper now has a caster_snapshot form,
// and the live const char_data* form is a one-line forwarder onto it.
//
// NOTE ON WHAT PROVES WHAT. The live/snapshot equivalence test immediately
// below documents the forwarding contract, but it cannot fail while the live
// forms really are forwarders -- both sides run the same body. The tests after
// it are the ones with teeth: each varies ONE snapshot field and pins the
// effect, so a snapshot form that reads the wrong field (or drops one the
// formula needs) goes red. Together they cover every field the nine helpers
// consume.
// ---------------------------------------------------------------------------

TEST(CasterSnapshot, FormulaHelpersAgreeBetweenLiveAndSnapshotForms) {
    char_data ch {};
    char_prof_data profs {};
    make_mage(ch, profs);
    ch.specials2.perception = 50; // non-zero, so saves_poison's offence term is live
    char_data victim {};
    char_prof_data victim_profs {};
    make_mage(victim, victim_profs);
    victim_profs.specialization = static_cast<int>(game_types::PS_Cold);
    victim.specials2.perception = 30;
    victim.tmpabilities.con = 10;
    const caster_snapshot snap = caster_snapshot::capture(ch);

    for (int i = 0; i < 4; ++i) push_test_random_value(0.5);
    const int live_level = get_mage_caster_level(&ch);
    for (int i = 0; i < 4; ++i) push_test_random_value(0.5);
    EXPECT_EQ(get_mage_caster_level(snap), live_level);

    for (int i = 0; i < 8; ++i) push_test_random_value(0.5);
    const int live_power = get_magic_power(&ch);
    for (int i = 0; i < 8; ++i) push_test_random_value(0.5);
    EXPECT_EQ(get_magic_power(snap), live_power);

    EXPECT_EQ(get_saving_throw_dc(snap), get_saving_throw_dc(&ch));
    EXPECT_EQ(get_save_bonus(snap, victim, game_types::PS_Fire, game_types::PS_Cold),
              get_save_bonus(ch, victim, game_types::PS_Fire, game_types::PS_Cold));
    EXPECT_EQ(should_apply_spell_penetration(snap), should_apply_spell_penetration(&ch));
    EXPECT_DOUBLE_EQ(get_spell_pen_value(snap), get_spell_pen_value(&ch));
    EXPECT_EQ(other_side(snap, &victim), other_side(&ch, &victim));
    EXPECT_EQ(is_friendly_taget(snap, &victim), is_friendly_taget(&ch, &victim));

    for (int i = 0; i < 4; ++i) push_test_random_value(0.5);
    const int live_mystic = get_mystic_caster_level(&ch);
    for (int i = 0; i < 4; ++i) push_test_random_value(0.5);
    EXPECT_EQ(get_mystic_caster_level(snap), live_mystic);

    for (int i = 0; i < 4; ++i) push_test_random_value(0.5);
    const char live_poison = saves_poison(&victim, &ch);
    for (int i = 0; i < 4; ++i) push_test_random_value(0.5);
    EXPECT_EQ(saves_poison(&victim, snap), live_poison);

    for (int i = 0; i < 4; ++i) push_test_random_value(0.5);
    const bool live_saved = new_saves_spell(&ch, &victim, 3);
    for (int i = 0; i < 4; ++i) push_test_random_value(0.5);
    EXPECT_EQ(new_saves_spell(snap, &victim, 3), live_saved);
    clear_test_random_values();
}

TEST(CasterSnapshot, MageCasterLevelStillRollsTheIntelRemainderPerCall) {
    char_data ch {};
    char_prof_data profs {};
    make_mage(ch, profs);
    ch.tmpabilities.intel = 23; // intel_factor 4, remainder roll number(0, 4)
    const caster_snapshot snap = caster_snapshot::capture(ch);
    push_test_random_value(0.0); // roll 0 -> no bump
    const int low = get_mage_caster_level(snap);
    push_test_random_value(0.99); // roll > 0 -> bump
    const int high = get_mage_caster_level(snap);
    clear_test_random_values();
    EXPECT_EQ(high, low + 1) << "the per-call remainder roll must survive the snapshot";
}

TEST(CasterSnapshot, MageCasterLevelReadsTheCapturedMageLevelAndIntel) {
    char_data ch {};
    char_prof_data profs {};
    make_mage(ch, profs); // PROF_MAGE 25, intel 21 -> intel_factor 4
    const caster_snapshot snap = caster_snapshot::capture(ch);
    push_test_random_value(0.0);
    EXPECT_EQ(get_mage_caster_level(snap), 29);

    caster_snapshot deeper = snap;
    deeper.mage_prof_level = 30;
    push_test_random_value(0.0);
    EXPECT_EQ(get_mage_caster_level(deeper), 34);

    caster_snapshot brighter = snap;
    brighter.intel = 40; // intel_factor 8
    push_test_random_value(0.0);
    EXPECT_EQ(get_mage_caster_level(brighter), 33);

    caster_snapshot willed = snap;
    willed.wil = 90; // the mage formula must not read will
    push_test_random_value(0.0);
    EXPECT_EQ(get_mage_caster_level(willed), 29);
    clear_test_random_values();
}

TEST(CasterSnapshot, MagicPowerReadsTheCapturedRaceLevelAndBattleMageInputs) {
    char_data ch {};
    char_prof_data profs {};
    make_mage(ch, profs);
    const caster_snapshot snap = caster_snapshot::capture(ch);

    // The race caps the level modifier through max_race_prof_level(): a common
    // orc's PROF_MAGE ceiling is 20, everyone else's is 30.
    caster_snapshot orcish = snap;
    orcish.race = RACE_ORC;
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    const int human_power = get_magic_power(snap);
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    const int orc_power = get_magic_power(orcish);
    EXPECT_EQ(human_power - orc_power, (30 * snap.level_a / 30) - (20 * snap.level_a / 30));

    // spell_power passes straight through for a non-battle-mage.
    caster_snapshot powered = snap;
    powered.spell_power += 7;
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    EXPECT_EQ(get_magic_power(powered), human_power + 7);
    clear_test_random_values();
}

TEST(CasterSnapshot, BattleMageBonusesSurviveTheSnapshot) {
    // The battle-mage spell-power/spell-pen bonus reads the specialization,
    // the tactics setting and the mage level -- all three must ride in the
    // snapshot, or a stored caster silently loses the bonus.
    char_data ch {};
    char_prof_data profs {};
    make_mage(ch, profs);
    profs.specialization = static_cast<int>(game_types::PS_BattleMage);
    ch.specials.tactics = TACTICS_AGGRESSIVE;
    ch.points.spell_power = 60;
    const caster_snapshot snap = caster_snapshot::capture(ch);
    EXPECT_EQ(snap.specialization, game_types::PS_BattleMage);
    EXPECT_EQ(snap.tactics, TACTICS_AGGRESSIVE);

    caster_snapshot no_tactics = snap;
    no_tactics.tactics = 0;
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    const int with_tactics = get_magic_power(snap);
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    const int without_tactics = get_magic_power(no_tactics);
    EXPECT_EQ(with_tactics - without_tactics, TACTICS_AGGRESSIVE / 2)
        << "the battle-mage spell-power bonus must see the captured tactics";
    EXPECT_EQ(get_saving_throw_dc(snap) - get_saving_throw_dc(no_tactics), TACTICS_AGGRESSIVE / 2)
        << "the battle-mage spell-pen bonus must see the captured tactics";

    caster_snapshot plain = snap;
    plain.specialization = game_types::PS_Fire;
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    EXPECT_EQ(get_magic_power(plain), without_tactics - (snap.mage_prof_level / 12))
        << "a non-battle-mage gets neither half of the bonus";
    clear_test_random_values();
}

TEST(CasterSnapshot, SavingThrowDcReadsIntelMageLevelAndSpellPenNotWill) {
    char_data ch {};
    char_prof_data profs {};
    make_mage(ch, profs); // PROF_MAGE 25, intel 21, spell_pen 2
    const caster_snapshot snap = caster_snapshot::capture(ch);
    const int base = get_saving_throw_dc(snap);
    EXPECT_EQ(base, 10 + 25 / 3 + (21 - 8) / 4 + 2);

    caster_snapshot brighter = snap;
    brighter.intel += 4;
    EXPECT_EQ(get_saving_throw_dc(brighter), base + 1);

    caster_snapshot willed = snap;
    willed.wil += 40;
    EXPECT_EQ(get_saving_throw_dc(willed), base) << "the save DC reads intel, never will";

    caster_snapshot deeper = snap;
    deeper.mage_prof_level += 3;
    EXPECT_EQ(get_saving_throw_dc(deeper), base + 1);

    caster_snapshot penetrating = snap;
    penetrating.spell_pen += 5;
    EXPECT_EQ(get_saving_throw_dc(penetrating), base + 5);
}

TEST(CasterSnapshot, MysticCasterLevelReadsClericLevelAndWillNotIntel) {
    char_data ch {};
    char_prof_data profs {};
    make_mage(ch, profs); // PROF_CLERIC 3, wil 17 -> will_factor 3
    const caster_snapshot snap = caster_snapshot::capture(ch);
    push_test_random_value(0.0);
    EXPECT_EQ(get_mystic_caster_level(snap), 6);

    caster_snapshot deeper = snap;
    deeper.cleric_prof_level = 13;
    push_test_random_value(0.0);
    EXPECT_EQ(get_mystic_caster_level(deeper), 16);

    caster_snapshot willed = snap;
    willed.wil = 40; // will_factor 8
    push_test_random_value(0.0);
    EXPECT_EQ(get_mystic_caster_level(willed), 11);

    caster_snapshot brighter = snap;
    brighter.intel = 90; // the mystic formula must not read intel
    push_test_random_value(0.0);
    EXPECT_EQ(get_mystic_caster_level(brighter), 6);
    clear_test_random_values();
}

TEST(CasterSnapshot, PoisonSaveOffenceReadsCapturedWillpowerAndPerception) {
    char_data victim {};
    char_prof_data victim_profs {};
    make_mage(victim, victim_profs);
    victim.tmpabilities.con = 10; // defense = 10*5 + 9*3 = 77, so number(38, 77) == 58
    char_data ch {};
    char_prof_data profs {};
    make_mage(ch, profs);
    ch.specials2.perception = 50;
    const caster_snapshot snap = caster_snapshot::capture(ch);

    caster_snapshot blind = snap;
    blind.perception = 0; // offence 0 -> the victim always saves
    clear_test_random_values();
    for (int i = 0; i < 4; ++i) push_test_random_value(0.5);
    EXPECT_NE(saves_poison(&victim, blind), 0);

    caster_snapshot potent = snap;
    potent.perception = 100;
    potent.willpower = 100; // offence 800 -> the victim never saves
    clear_test_random_values();
    for (int i = 0; i < 4; ++i) push_test_random_value(0.5);
    EXPECT_EQ(saves_poison(&victim, potent), 0)
        << "the poison offence term must scale with the captured perception";

    caster_snapshot spineless = potent;
    spineless.willpower = 0; // offence 0 again -> the victim saves
    clear_test_random_values();
    for (int i = 0; i < 4; ++i) push_test_random_value(0.5);
    EXPECT_NE(saves_poison(&victim, spineless), 0)
        << "the poison offence term must scale with the captured willpower";
    clear_test_random_values();
}

TEST(CasterSnapshot, SpellPenetrationReadsTheCapturedNpcAndMasterFields) {
    char_data ch {};
    char_prof_data profs {};
    make_mage(ch, profs); // PROF_MAGE 25
    const caster_snapshot snap = caster_snapshot::capture(ch);
    EXPECT_TRUE(should_apply_spell_penetration(snap));
    EXPECT_DOUBLE_EQ(get_spell_pen_value(snap), 5.0);

    caster_snapshot charmed_pet = snap;
    charmed_pet.is_npc = true;
    charmed_pet.is_charmed = true;
    charmed_pet.master_mage_prof_level = 30;
    EXPECT_DOUBLE_EQ(get_spell_pen_value(charmed_pet), (25 + 30 / 3) / 5.0);

    caster_snapshot uncharmed = charmed_pet;
    uncharmed.is_charmed = false;
    EXPECT_DOUBLE_EQ(get_spell_pen_value(uncharmed), 5.0)
        << "an uncharmed NPC gets no master bonus";

    caster_snapshot plain_mob = snap;
    plain_mob.is_pc_for_spell_pen = false;
    EXPECT_FALSE(should_apply_spell_penetration(plain_mob));
}

TEST(CasterSnapshot, SaveBonusReadsTheCapturedSpecialization) {
    char_data ch {};
    char_prof_data profs {};
    make_mage(ch, profs); // PS_Fire
    char_data victim {};
    char_prof_data victim_profs {};
    make_mage(victim, victim_profs);
    victim_profs.specialization = static_cast<int>(game_types::PS_Cold);
    const caster_snapshot snap = caster_snapshot::capture(ch);

    // Caster on the primary spec (-2) and victim on the opposing spec (-2).
    EXPECT_EQ(get_save_bonus(snap, victim, game_types::PS_Fire, game_types::PS_Cold), -4);

    caster_snapshot cold = snap;
    cold.specialization = game_types::PS_Cold; // caster now on the opposing spec (+2)
    EXPECT_EQ(get_save_bonus(cold, victim, game_types::PS_Fire, game_types::PS_Cold), 0);

    caster_snapshot arcane = snap;
    arcane.specialization = game_types::PS_Arcane; // arcane counts as the primary spec
    EXPECT_EQ(get_save_bonus(arcane, victim, game_types::PS_Fire, game_types::PS_Cold), -4);
}

TEST(CasterSnapshot, RaceWarSideAndFriendlyFireReadTheCapturedRace) {
    char_data ch {};
    char_prof_data profs {};
    make_mage(ch, profs); // RACE_HUMAN -- a free-peoples race
    char_data orc {};
    char_prof_data orc_profs {};
    make_mage(orc, orc_profs);
    orc.player.race = RACE_URUK; // a Shadow race
    const caster_snapshot snap = caster_snapshot::capture(ch);

    EXPECT_EQ(other_side(snap, &orc), 1);
    EXPECT_FALSE(is_friendly_taget(snap, &orc));

    caster_snapshot evil = snap;
    evil.race = RACE_URUK;
    EXPECT_EQ(other_side(evil, &orc), 0)
        << "the race-war side must come from the captured race";
    EXPECT_TRUE(is_friendly_taget(evil, &orc));

    caster_snapshot charmed_mob = snap;
    charmed_mob.is_npc = true;
    charmed_mob.is_charmed = false;
    EXPECT_EQ(other_side(charmed_mob, &orc), 0)
        << "an uncharmed NPC is on nobody's side";

    // The self test has to short-circuit BEFORE the master recursion: give the
    // caster a master on the far side of the race war and it is still friendly
    // to itself, which is false the moment the identity check is lost.
    ch.master = &orc;
    EXPECT_TRUE(is_friendly_taget(snap, &ch)) << "a caster is always friendly to itself";
    EXPECT_FALSE(is_friendly_taget(snap, &orc))
        << "the master recursion still decides for everyone else";
    ch.master = nullptr;
}
