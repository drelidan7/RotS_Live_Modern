// src/tests/caster_snapshot_tests.cpp
#include "../handler.h"
#include "../utils.h"
#include "rots/core/caster_snapshot.h"
#include "rots/core/character.h"
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
