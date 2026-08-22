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
    set_char_exists(ch.abs_number);
    const caster_snapshot snap = caster_snapshot::capture(ch);
    EXPECT_EQ(snap.resolve(), &ch);
    remove_char_exists(ch.abs_number);
    EXPECT_EQ(snap.resolve(), nullptr) << "an extracted caster resolves to nobody";
    set_char_exists(ch.abs_number);
    ch.abs_number = 7904; // the slot was recycled by a different character
    EXPECT_EQ(snap.resolve(), nullptr);
    remove_char_exists(7903);
}

TEST(CasterSnapshot, NoneIsNeverResolvable) {
    const caster_snapshot none = caster_snapshot::none();
    EXPECT_TRUE(none.is_none());
    EXPECT_EQ(none.resolve(), nullptr);
}
