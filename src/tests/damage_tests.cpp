#include "../spells.h"
#include "../utils.h"
#include "damage_test_context.h"
#include "test_random_utils.h"
#include <gtest/gtest.h>

int damage(char_data* attacker, char_data* victim, int dam, int attacktype, int hit_location);

extern char_data* combat_list;
extern char_data* combat_next_dude;

class DamageMethodTest : public ::testing::Test {
  protected:
    void TearDown() override
    {
        clear_test_random_values();
        combat_list = nullptr;
        combat_next_dude = nullptr;
    }
};

TEST_F(DamageMethodTest, ClampsNonAmbushOverflowDamageBeforeApplyingIt) {
    DamageTestContext context;

    EXPECT_EQ(damage(&context.attacker, &context.victim, 250, TYPE_HIT, 3), 0)
        << "Expected overflow-sized damage to be clamped and applied without killing the high-health test victim.";
    EXPECT_EQ(context.victim.tmpabilities.hit, 300)
        << "Expected damage() to clamp non-ambush hits above 200 before subtracting them from the victim's hit points.";
}

TEST_F(DamageMethodTest, AppliesBeorningPhysicalDamageReductionBeforeFinalDamageCapture) {
    DamageTestContext context;
    context.victim.player.race = RACE_BEORNING;

    damage(&context.attacker, &context.victim, 10, TYPE_HIT, 0);

    EXPECT_EQ(context.victim.tmpabilities.hit, 491)
        << "Expected beorning victims to reduce incoming physical damage before damage() subtracts it from hit points.";
}

TEST_F(DamageMethodTest, AppliesWildResistanceWhenThePhysicalResistanceRollAllowsIt) {
    DamageTestContext context;
    context.victim.specials.resistance = (1 << PLRSPEC_WILD);

    push_test_random_value(0.50);

    damage(&context.attacker, &context.victim, 30, TYPE_HIT, 0);

    EXPECT_EQ(context.victim.tmpabilities.hit, 480)
        << "Expected wild resistance to reduce physical damage to two thirds when the one-in-three bypass roll does not clear it.";
}

TEST_F(DamageMethodTest, AppliesWildVulnerabilityToIncreasePhysicalDamage) {
    DamageTestContext context;
    context.victim.specials.vulnerability = (1 << PLRSPEC_WILD);

    push_test_random_value(0.50);

    damage(&context.attacker, &context.victim, 30, TYPE_HIT, 0);

    EXPECT_EQ(context.victim.tmpabilities.hit, 455)
        << "Expected wild vulnerability to increase incoming physical damage by half before damage() subtracts it from hit points.";
}

TEST_F(DamageMethodTest, ShieldAbsorptionConsumesManaAndReducesDamage) {
    DamageTestContext context;
    context.add_victim_affect(context.victim_primary_affect, SPELL_SHIELD, 5);

    // damage() (fight.cpp) draws from number() twice before this test's
    // assertions are reached: once unconditionally at the physical-resist
    // check ("number(0, 2) == 0 && IS_PHYSICAL(attacktype)" -- the roll
    // itself always happens even though its *result* is a no-op here since
    // attacktype is a spell, not a physical hit), and again at the shield
    // absorption rounding check this test actually means to control. With
    // only one push_test_random_value() call, the first (irrelevant) draw
    // silently consumed it, leaving the rounding roll to fall through to
    // the real PRNG -- passing only by chance, and failing under
    // --gtest_shuffle whenever some other test left the shared PRNG stream
    // at a position that rolls a 0 here instead of a nonzero value (Task 8
    // 64-bit hazard audit: order-dependent test-isolation rot, root-caused
    // via a `--gtest_shuffle` repro). Push one value per draw so both are
    // pinned regardless of what ran before this test in the process.
    push_test_random_value(0.0); // number(0, 2) physical-resist roll -- result is a no-op for a spell attacktype.
    push_test_random_value(0.5); // number(0, mage_level) shield rounding roll -- nonzero so the "spend 16 mana" rounding-up case below is exercised.

    damage(&context.attacker, &context.victim, 50, SPELL_MAGIC_MISSILE, 2);

    EXPECT_EQ(context.victim.tmpabilities.hit, 470)
        << "Expected shield to absorb 40 percent of the incoming spell damage before the remaining damage is applied.";
    EXPECT_EQ(context.victim.tmpabilities.mana, 84)
        << "Expected the current shield absorption math to spend 16 mana after its rounding step for this absorbed-damage case.";
    EXPECT_EQ(context.victim_primary_affect.duration, 2)
        << "Expected shield duration to be shortened to the near-expiry value once it absorbs damage and still has mana remaining.";
}
