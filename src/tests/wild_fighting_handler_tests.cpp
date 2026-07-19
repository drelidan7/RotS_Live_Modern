// wild_fighting_handler_tests.cpp

// New test TU (combat-seed wave, Task 3b; plan
// docs/superpowers/plans/2026-07-19-combat-seed.md; brief
// .superpowers/sdd/task-3b-brief.md). Standing coverage-gap rule: CS Task 1's
// coverage citation (task-1-report.md Step 4) found player_spec::
// wild_fighting_handler (wild_fighting_handler.cpp) had ZERO direct test
// coverage -- gtest_main.cpp's register_wild_attack_speed_multiplier_hook()/
// register_attack_speed_multiplier_hook() calls only install the hook
// pointers, they never invoke the underlying behavior. This file constructs
// the real class on real char_data fixtures and exercises every public
// method's REAL body -- mirrors weapon_master_handler_tests.cpp's/
// battle_mage_handler_tests.cpp's established sibling-handler test patterns
// (WeaponMasterTestContext-style context struct, TestCase tables for
// threshold coverage, a *ProcTest fixture for RNG-driven cases).
//
// FIXTURE DESIGN: every WildFightingTestContext sets character.in_room =
// NOWHERE. wild_fighting_handler.cpp's real act()/send_to_char() output-seam
// sinks are wired for the whole test process (gtest_main.cpp's
// register_game_output_sinks(), installing comm.cpp's real act_impl()/
// send_to_char_impl() bodies) -- unlike act() calls gated behind a null
// character->desc (safe no-op for TO_CHAR, comm.cpp's act_impl()), a TO_ROOM
// act() call with in_room left at char_data{}'s default (0, not NOWHERE)
// would index the unbooted world[] array this test process never populates.
// NOWHERE makes every TO_ROOM branch a safe no-op instead (comm.cpp's
// act_impl(): `ch->in_room != NOWHERE` guards the world[] read), the same
// technique weapon_master_handler_tests.cpp's SwordProcRegainsEnergyWhenSlash
// ProcSucceeds test documents for the identical reason.
#include "../char_utils.h"
#include "../entity_hooks.h"
#include "../utils.h"
#include "../warrior_spec_handlers.h"
#include "rots/core/character.h"
#include "test_random_utils.h"

#include <gtest/gtest.h>

namespace {

// A character fixture ready for player_spec::wild_fighting_handler's
// constructor to read (spec via profs->specialization, tactics, and the
// max/current hit points the handler derives health_percentage from).
struct WildFightingTestContext {
    // The character under test.
    char_data character{};
    // Backing storage for character.profs; player_spec::get_specialization()
    // (entity_lifecycle.cpp) reads specialization from here, and returns
    // PS_None if profs is left null -- so every context always wires this up,
    // even for the "non-specialist" cases (specialization = PS_None).
    char_prof_data profs{};

    WildFightingTestContext(game_types::player_specs specialization, int tactics, int max_health,
                            int current_health) {
        character.profs = &profs;
        profs.specialization = static_cast<int>(specialization);
        character.specials.tactics = tactics;
        character.abilities.hit = max_health;
        character.tmpabilities.hit = current_health;
        character.player.level = LEVEL_MAX;
        // See the file header comment: keeps every TO_ROOM act() call in the
        // methods under test a safe no-op instead of indexing world[].
        character.in_room = NOWHERE;
    }
};

} // namespace

TEST(WildFightingHandler, ReturnsDefaultAttackSpeedMultiplierForNonSpecialists) {
    WildFightingTestContext context(game_types::PS_None, TACTICS_BERSERK, 100, 10);
    player_spec::wild_fighting_handler handler(&context.character);

    EXPECT_FLOAT_EQ(handler.get_attack_speed_multiplier(), 1.0f)
        << "Expected non-wild-fighting specialists to receive no attack speed bonus, even at low "
           "health.";
}

TEST(WildFightingHandler, ReturnsDefaultAttackSpeedMultiplierWhenTacticsIsNotBerserk) {
    WildFightingTestContext context(game_types::PS_WildFighting, TACTICS_AGGRESSIVE, 100, 10);
    player_spec::wild_fighting_handler handler(&context.character);

    EXPECT_FLOAT_EQ(handler.get_attack_speed_multiplier(), 1.0f)
        << "Expected the attack speed bonus to require berserk tactics, even at low health.";
}

TEST(WildFightingHandler, ReturnsDefaultAttackSpeedMultiplierWhenHealthAboveThreshold) {
    WildFightingTestContext context(game_types::PS_WildFighting, TACTICS_BERSERK, 100, 50);
    player_spec::wild_fighting_handler handler(&context.character);

    EXPECT_FLOAT_EQ(handler.get_attack_speed_multiplier(), 1.0f)
        << "Expected no attack speed bonus above the 45% health threshold.";
}

TEST(WildFightingHandler, GrantsScalingAttackSpeedBonusForBerserkersBelowHealthThreshold) {
    struct TestCase {
        int current_health;
        float expected_multiplier;
    };

    // max_health is fixed at 100 so current_health also reads as a percent;
    // formula (wild_fighting_handler.cpp): 1.0f + 1.0f - health_percentage - 0.4f.
    const TestCase cases[] = {
        {45, 1.15f}, // the doc comment's own "15% bonus at 45% health" example.
        {1, 1.59f},  // the doc comment's own "59% at 1% health" example.
        {0, 1.6f},   // the theoretical ceiling at 0% health.
    };

    for (const TestCase &test_case : cases) {
        WildFightingTestContext context(game_types::PS_WildFighting, TACTICS_BERSERK, 100,
                                        test_case.current_health);
        player_spec::wild_fighting_handler handler(&context.character);

        EXPECT_FLOAT_EQ(handler.get_attack_speed_multiplier(), test_case.expected_multiplier)
            << "Expected the scaling formula to hold at " << test_case.current_health
            << "% health.";
    }
}

TEST(WildFightingHandler, DispatchWildAttackSpeedMultiplierUsesTheRegisteredRealHandler) {
    // gtest_main.cpp's register_wild_attack_speed_multiplier_hook() installs
    // wild_fighting_handler.cpp's real wild_attack_speed_multiplier_hook_impl
    // for the whole test process -- this proves the dispatch path
    // (rots::entity::dispatch_wild_attack_speed_multiplier(), the one
    // char_utils.cpp's get_energy_regen() actually calls in production)
    // reaches the SAME real body as constructing the handler directly, not
    // just the null-hook tripwire default the world-seed-era hook tests
    // covered.
    WildFightingTestContext context(game_types::PS_WildFighting, TACTICS_BERSERK, 100, 10);
    player_spec::wild_fighting_handler handler(&context.character);

    EXPECT_FLOAT_EQ(rots::entity::dispatch_wild_attack_speed_multiplier(&context.character),
                    handler.get_attack_speed_multiplier())
        << "Expected the registered production hook to compute the same multiplier as a directly "
           "constructed handler.";
}

TEST(WildFightingHandler, ReportsBonusAttackSpeedOnlyWhenBerserkAndBelowHealthThreshold) {
    struct TestCase {
        game_types::player_specs specialization;
        int tactics;
        int current_health;
        bool expected;
        const char *description;
    };

    const TestCase cases[] = {
        {game_types::PS_None, TACTICS_BERSERK, 10, false, "non-specialists never report a bonus"},
        {game_types::PS_WildFighting, TACTICS_AGGRESSIVE, 10, false,
         "non-berserk tactics never report a bonus"},
        {game_types::PS_WildFighting, TACTICS_BERSERK, 50, false,
         "berserk above the threshold reports no bonus"},
        {game_types::PS_WildFighting, TACTICS_BERSERK, 45, true,
         "berserk at exactly the threshold reports a bonus"},
        {game_types::PS_WildFighting, TACTICS_BERSERK, 10, true,
         "berserk below the threshold reports a bonus"},
    };

    for (const TestCase &test_case : cases) {
        WildFightingTestContext context(test_case.specialization, test_case.tactics, 100,
                                        test_case.current_health);
        player_spec::wild_fighting_handler handler(&context.character);

        EXPECT_EQ(handler.has_bonus_attack_speed(), test_case.expected)
            << "Expected " << test_case.description << ".";
    }
}

TEST(WildFightingHandler,
     GrantsWildSwingBonusOnlyForBerserkTacticsBelowQuarterHealthRegardlessOfSpec) {
    struct TestCase {
        game_types::player_specs specialization;
        int tactics;
        int current_health;
        float expected_multiplier;
        const char *description;
    };

    const TestCase cases[] = {
        {game_types::PS_WildFighting, TACTICS_NORMAL, 10, 1.0f,
         "non-berserk tactics never grant a bonus"},
        {game_types::PS_WildFighting, TACTICS_BERSERK, 50, 1.0f,
         "berserk above quarter health grants no bonus"},
        {game_types::PS_WildFighting, TACTICS_BERSERK, 25, 1.33f,
         "berserk at exactly quarter health grants the bonus"},
        // get_wild_swing_damage_multiplier() (wild_fighting_handler.cpp) checks
        // only `tactics`, never `spec` -- unlike every other method on this
        // class. A non-specialist with berserk tactics still gets the bonus;
        // documenting this real, spec-independent behavior rather than the
        // gated behavior every sibling method has.
        {game_types::PS_None, TACTICS_BERSERK, 10, 1.33f,
         "berserk tactics grant the bonus even for non-specialists"},
    };

    for (const TestCase &test_case : cases) {
        WildFightingTestContext context(test_case.specialization, test_case.tactics, 100,
                                        test_case.current_health);
        player_spec::wild_fighting_handler handler(&context.character);

        EXPECT_FLOAT_EQ(handler.get_wild_swing_damage_multiplier(), test_case.expected_multiplier)
            << "Expected " << test_case.description << ".";
    }
}

TEST(WildFightingHandler, UpdateHealthIsNoOpForNonSpecialists) {
    // update_health() (wild_fighting_handler.cpp) returns immediately when
    // spec != PS_WildFighting, before touching health_percentage. Every
    // OTHER getter that reads health_percentage also gates on spec, so it
    // can't distinguish "field never touched" from "field touched but
    // gated" -- get_wild_swing_damage_multiplier() is the one method that
    // reads health_percentage-derived state without a spec gate (only
    // tactics), which makes it the one observable window onto whether
    // update_health() actually mutated anything here.
    WildFightingTestContext context(game_types::PS_None, TACTICS_BERSERK, 100,
                                    80); // 80% health, above the 25% bonus line.
    player_spec::wild_fighting_handler handler(&context.character);

    handler.update_health(
        5); // would be 5% health -- well under the 25% wild-swing bonus line, if applied.

    EXPECT_FLOAT_EQ(handler.get_wild_swing_damage_multiplier(), 1.0f)
        << "Expected update_health() to leave health_percentage untouched for non-specialists, so "
           "the "
           "wild-swing multiplier still reflects the constructor's original 80% health.";
}

TEST(WildFightingHandler, UpdateHealthRecalculatesHealthPercentageForSpecialists) {
    // 50/100 = 50% health, above both the 45% attack-speed threshold and the
    // 25% wild-swing threshold.
    WildFightingTestContext context(game_types::PS_WildFighting, TACTICS_BERSERK, 100, 50);
    player_spec::wild_fighting_handler handler(&context.character);
    ASSERT_FALSE(handler.has_bonus_attack_speed());
    ASSERT_FLOAT_EQ(handler.get_wild_swing_damage_multiplier(), 1.0f);

    // Crosses both thresholds in one call (50% -> 20%); this also exercises
    // on_enter_rage()'s real act()/send_to_char() broadcast path (its
    // messages aren't independently observable without a live descriptor --
    // see the file header comment -- but the call executing without
    // crashing IS what this asserts, via the fixture's NOWHERE/no-desc
    // safety net).
    handler.update_health(20);

    EXPECT_TRUE(handler.has_bonus_attack_speed())
        << "Expected the recalculated 20% health to cross the attack-speed bonus threshold.";
    EXPECT_FLOAT_EQ(handler.get_wild_swing_damage_multiplier(), 1.33f)
        << "Expected the recalculated 20% health to cross the wild-swing bonus threshold.";
}

TEST(WildFightingHandler, UpdateTacticsIsNoOpForNonSpecialists) {
    // Mirrors UpdateHealthIsNoOpForNonSpecialists: get_wild_swing_damage_
    // multiplier() reads `tactics` directly with no spec gate, so it is the
    // one observable window onto whether update_tactics() mutated the field
    // it guards for non-specialists.
    WildFightingTestContext context(game_types::PS_None, TACTICS_NORMAL, 100,
                                    10); // low health, tactics not yet berserk.

    player_spec::wild_fighting_handler handler(&context.character);
    handler.update_tactics(TACTICS_BERSERK);

    EXPECT_FLOAT_EQ(handler.get_wild_swing_damage_multiplier(), 1.0f)
        << "Expected update_tactics() to leave tactics untouched for non-specialists, so the "
           "wild-swing "
           "multiplier still reflects TACTICS_NORMAL rather than the requested TACTICS_BERSERK.";
}

TEST(WildFightingHandler, UpdateTacticsChangesTacticsForSpecialists) {
    WildFightingTestContext context(game_types::PS_WildFighting, TACTICS_NORMAL, 100,
                                    10); // low health throughout.
    player_spec::wild_fighting_handler handler(&context.character);
    ASSERT_FLOAT_EQ(handler.get_wild_swing_damage_multiplier(), 1.0f)
        << "Sanity check: TACTICS_NORMAL grants no wild-swing bonus, even at low health.";

    handler.update_tactics(TACTICS_BERSERK);

    EXPECT_FLOAT_EQ(handler.get_wild_swing_damage_multiplier(), 1.33f)
        << "Expected switching to berserk tactics at already-low health to grant the wild-swing "
           "bonus.";
}

TEST(WildFightingHandler, OnUnitKilledIsNoOpForNonBerserkSpecialists) {
    WildFightingTestContext context(game_types::PS_WildFighting, TACTICS_AGGRESSIVE, 100, 10);
    char_data victim{};
    victim.player.level = LEVEL_MAX;
    player_spec::wild_fighting_handler handler(&context.character);

    handler.on_unit_killed(&victim);

    EXPECT_EQ(context.character.tmpabilities.hit, 10)
        << "Expected on_unit_killed to leave current hit points untouched without berserk tactics.";
}

TEST(WildFightingHandler, OnUnitKilledIsNoOpWhenVictimLevelBelowThreshold) {
    // capped_level * 6 / 10 = 30 * 6 / 10 = 18; a level-1 victim never meets it.
    WildFightingTestContext context(game_types::PS_WildFighting, TACTICS_BERSERK, 100, 10);
    char_data victim{};
    victim.player.level = 1;
    player_spec::wild_fighting_handler handler(&context.character);

    handler.on_unit_killed(&victim);

    EXPECT_EQ(context.character.tmpabilities.hit, 10)
        << "Expected on_unit_killed to leave current hit points untouched when the victim is too "
           "low-level.";
}

TEST(WildFightingHandler, OnUnitKilledRestoresPartialHealthWhenVictimLevelMeetsThreshold) {
    // capped_level * 6 / 10 = 30 * 6 / 10 = 18; a level-18 victim exactly meets it (>=).
    WildFightingTestContext context(game_types::PS_WildFighting, TACTICS_BERSERK, 100, 10);
    char_data victim{};
    victim.player.level = 18;
    player_spec::wild_fighting_handler handler(&context.character);

    handler.on_unit_killed(&victim);

    // missing_health = 100 - 10 = 90; tmpabilities.hit += 90 * 0.1f = 9 -> 19.
    EXPECT_EQ(context.character.tmpabilities.hit, 19)
        << "Expected on_unit_killed to restore 10% of the missing health on a qualifying kill.";

    // on_unit_killed() also calls update_health(character->tmpabilities.hit)
    // internally with the NEW value (19/100 = 19%) -- confirming that call
    // happened (rather than update_health() never running, or running with a
    // stale value) via the same tactics-independent, spec-gated getter used
    // above: 19% is below both the attack-speed and wild-swing thresholds.
    EXPECT_TRUE(handler.has_bonus_attack_speed())
        << "Expected on_unit_killed's internal update_health() call to recalculate "
           "health_percentage "
           "from the restored hit points.";
}

class WildFightingRushTest : public ::testing::Test {
  protected:
    void TearDown() override { clear_test_random_values(); }
};

TEST_F(WildFightingRushTest, NonSpecialistsAlwaysKeepTheOriginalDamage) {
    WildFightingTestContext context(game_types::PS_None, TACTICS_BERSERK, 100, 50);
    player_spec::wild_fighting_handler handler(&context.character);

    // do_rush() returns starting_damage unchanged before ever calling
    // number() for a non-specialist -- no random value is queued, so this
    // also proves the RNG seam is never consumed on this branch.
    EXPECT_EQ(handler.do_rush(20), 20) << "Expected non-specialists to never rush forward.";
}

TEST_F(WildFightingRushTest, BerserkRushSucceedsAndAddsHalfDamageWhenRollIsWithinChance) {
    WildFightingTestContext context(game_types::PS_WildFighting, TACTICS_BERSERK, 100, 50);
    wild_fighting_data *spec_data = new wild_fighting_data();
    context.character.extra_specialization_data.current_spec_info =
        spec_data; // owned/deleted by the character's destructor.
    player_spec::wild_fighting_handler handler(&context.character);

    push_test_random_value(0.0); // berserk rush chance is 0.15; 0.0 is well within it.

    EXPECT_EQ(handler.do_rush(20), 30)
        << "Expected a successful rush to add half the starting damage (20 + 10).";
    EXPECT_EQ(spec_data->get_total_rush_damage(), 10u)
        << "Expected the rush damage to be tallied on the wild-fighting spec data.";
}

TEST_F(WildFightingRushTest, BerserkRushFailsAndKeepsOriginalDamageWhenRollExceedsChance) {
    WildFightingTestContext context(game_types::PS_WildFighting, TACTICS_BERSERK, 100, 50);
    wild_fighting_data *spec_data = new wild_fighting_data();
    context.character.extra_specialization_data.current_spec_info = spec_data;
    player_spec::wild_fighting_handler handler(&context.character);

    push_test_random_value(0.20); // berserk rush chance is 0.15; 0.20 exceeds it.

    EXPECT_EQ(handler.do_rush(20), 20)
        << "Expected a failed rush roll to leave the starting damage unchanged.";
    EXPECT_EQ(spec_data->get_total_rush_damage(), 0u)
        << "Expected no rush damage to be tallied on a failed roll.";
}

TEST_F(WildFightingRushTest, AggressiveTacticsRushChanceBoundaryIsInclusive) {
    // get_rush_chance() (wild_fighting_handler.cpp) returns 0.10 for
    // TACTICS_AGGRESSIVE, and do_rush()'s guard is `number() > rush_chance`
    // -- so a roll exactly AT the chance still rushes (not strictly less
    // than), and only a roll strictly above it fails.
    WildFightingTestContext context(game_types::PS_WildFighting, TACTICS_AGGRESSIVE, 100, 50);
    wild_fighting_data *spec_data = new wild_fighting_data();
    context.character.extra_specialization_data.current_spec_info = spec_data;
    player_spec::wild_fighting_handler handler(&context.character);

    push_test_random_value(0.10);
    EXPECT_EQ(handler.do_rush(20), 30)
        << "Expected a roll exactly at the rush chance to still succeed.";

    push_test_random_value(0.11);
    EXPECT_EQ(handler.do_rush(20), 20) << "Expected a roll just above the rush chance to fail.";
}

TEST_F(WildFightingRushTest, TacticsWithoutARushChanceNeverRush) {
    // get_rush_chance() only recognizes TACTICS_BERSERK/AGGRESSIVE/NORMAL;
    // any other tactics value (e.g. TACTICS_DEFENSIVE) falls through to its
    // final `return 0.0f;`.
    WildFightingTestContext context(game_types::PS_WildFighting, TACTICS_DEFENSIVE, 100, 50);
    player_spec::wild_fighting_handler handler(&context.character);

    push_test_random_value(0.5);

    EXPECT_EQ(handler.do_rush(20), 20)
        << "Expected tactics with a zero rush chance to never rush forward.";
}
