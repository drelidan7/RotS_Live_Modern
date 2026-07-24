#include "../spells.h"
#include "rots/core/character.h"
#include "rots/core/types.h"
#include "../utils.h"
#include "../interpre.h"
#include "damage_test_context.h"
#include "test_random_utils.h"
#include <gtest/gtest.h>

// spec_pro.cpp has no header of its own for these internals -- forward
// declare the symbols this suite exercises directly, mirroring the pattern
// mage_tests.cpp/fight_proc_tests.cpp use for other header-less product
// helpers.
//
// handle_pracs() is the per-iteration body the prac command's batched
// "N <skill>"/"all <skill>" loop (SPECIAL(guild), spec_pro.cpp) calls once per
// practice attempt: it increments ch->skills[request], decrements
// ch->specials2.spells_to_learn, recalculates skills/abilities, and clamps
// ch->knowledge[request] to the guildmaster's cap (returning true once the
// cap is hit, so the batching loop stops early instead of over-learning).
extern bool handle_pracs(char_data* host, char_data* ch, int request, int prog);

// SPECIAL(dragon) -- LS-1 Tranche C coverage rider (ls1-census.md Step 7:
// "the dragon save-next need[s] at least a characterization anchor"). Not
// declared in any header (SPECIAL()-family functions are address-taken into
// mob-spec tables, never called by name outside spec_pro.cpp itself), so
// forward-declare with the raw SPECIAL() signature (interpre.h).
extern int dragon(char_data* host, char_data* ch, int cmd, char* arg, int callflag,
    waiting_type* wtl);

// SPECIAL(healing_plant) -- same rider: the census also asks for coverage on
// "the converted counting walks / self-room reads that touch reachable
// logic," and healing_plant is the simplest live occupants()/room_of()
// self-room walk in this file (no save-next, no splice, no cursor).
extern int healing_plant(char_data* host, char_data* ch, int cmd, char* arg, int callflag,
    waiting_type* wtl);

// Real, compile-time-populated guild-teacher data (consts.cpp) -- guildmasters[0]
// ("ALL SKILLS") caps skill index 1 at knowledge 100, which this suite uses as
// its practice ceiling.
extern struct skill_teach_data guildmasters[MAX_SKILLS];

namespace {

// guildmasters[0] ("ALL SKILLS") teaches skill index 1 up to knowledge 100 --
// arbitrary within that guildmaster's table, chosen only because it's a
// nonzero cap so the clamp path is exercised.
constexpr int kGuildmasterIndex = 0;
constexpr int kSkillIndex = 1;

// A minimal host (guild teacher) / ch (practicing player) pair, wired the same
// way fight_proc_tests.cpp/mage_tests.cpp/battle_mage_handler_tests.cpp set up
// char_data for direct calls into product combat/skill helpers: profs must
// point somewhere (char_data::profs is a raw pointer; GET_PROF_COOF/
// GET_PROF_LEVEL dereference it for every non-PROF_WARRIOR, non-PROF_GENERAL
// skill recalc_skills() walks over, including entries unrelated to
// kSkillIndex), and skills/knowledge (owning std::vector<byte> on the live
// char_data, RAII T3) must be sized to MAX_SKILLS the way clear_char() would
// for a PC, since this fixture never calls clear_char().
struct GuildPracticeContext {
    char_data host {};
    char_data ch {};
    char_prof_data ch_profs {};

    GuildPracticeContext()
    {
        ch.profs = &ch_profs;
        ch.skills.assign(MAX_SKILLS, 0);
        ch.knowledge.assign(MAX_SKILLS, 0);
        ch.specials2.spells_to_learn = 5;

        // handle_pracs()'s act(..., TO_NOTVICT) call resolves its recipient
        // list via world[host->in_room].people (comm.cpp::act()) whenever
        // host->in_room != NOWHERE; this suite has no world/room fixture, so
        // pin host.in_room to NOWHERE to keep act() on its early "no
        // recipient" return instead of indexing an unallocated world[].
        host.in_room = NOWHERE;
    }
};

} // namespace

TEST(HandlePracs, IncrementsSkillAndDecrementsSpellsToLearnByOnePerCall) {
    GuildPracticeContext context;

    const bool capped = handle_pracs(&context.host, &context.ch, kSkillIndex, kGuildmasterIndex);

    EXPECT_EQ(context.ch.skills[kSkillIndex], 1);
    EXPECT_EQ(context.ch.specials2.spells_to_learn, 4);
    EXPECT_FALSE(capped) << "One practice session shouldn't reach the knowledge cap.";
}

TEST(HandlePracs, ClampsKnowledgeToGuildmasterCapAndSignalsStop) {
    GuildPracticeContext context;
    // recalc_skills() (called every handle_pracs() iteration) recomputes
    // ch->knowledge[request] from ch->skills[request] via a difficulty-scaled
    // formula, overwriting anything pre-set here -- so reach the guildmaster's
    // cap (100) the same way the live batched loop does: keep practicing
    // until recalc_skills() pushes computed knowledge past it. Give plenty of
    // headroom (spells_to_learn) and a generous iteration bound so this can't
    // spin forever if the cap were somehow unreachable.
    context.ch.specials2.spells_to_learn = 1000;

    bool capped = false;
    constexpr int kMaxIterations = 200;
    int iterations = 0;
    for (; iterations < kMaxIterations && !capped; ++iterations) {
        capped = handle_pracs(&context.host, &context.ch, kSkillIndex, kGuildmasterIndex);
    }

    ASSERT_TRUE(capped) << "Expected the knowledge cap to be reached within "
                         << kMaxIterations << " practice attempts.";
    EXPECT_EQ(context.ch.knowledge[kSkillIndex], guildmasters[kGuildmasterIndex].knowledge[kSkillIndex]);
}

TEST(HandlePracs, RepeatedCallsNeverDriveSpellsToLearnNegative) {
    GuildPracticeContext context;
    context.ch.specials2.spells_to_learn = 3;

    // Mirrors the batched-loop guard in SPECIAL(guild): the caller checks
    // spells_to_learn <= 0 BEFORE each handle_pracs() call, so spells_to_learn
    // itself should never go negative even across many practice attempts.
    for (int i = 0; i < 3; ++i) {
        ASSERT_GT(context.ch.specials2.spells_to_learn, 0);
        handle_pracs(&context.host, &context.ch, kSkillIndex, kGuildmasterIndex);
    }

    EXPECT_EQ(context.ch.specials2.spells_to_learn, 0);
}

namespace {

// dragon()/healing_plant() both walk room occupants via
// rots::entity::occupants(room_of(host)) post-conversion; DamageTestContext
// (damage_test_context.h) already wires an attacker+victim pair into
// world[room_number].people the same way the live occupant chain does, so it
// doubles as this rider's room-occupant fixture even though its name is
// damage-test-flavored.
struct DragonBreathContext : DamageTestContext {
    DragonBreathContext()
    {
        attacker.specials.position = POSITION_FIGHTING;
        attacker.player.level = 20; // mob_level = GET_LEVEL(host) / 2 == 10 -> dice(10, 6), always >= 10 dmg.
    }
};

} // namespace

TEST(SpecProDragon, DamagesEveryOtherRoomOccupantButNotItself) {
    DragonBreathContext context;
    clear_test_random_values();
    push_test_random_value(0.0); // number(0, 4) == 0 so the 20%-skip early-return isn't taken.

    const int attacker_hit_before = context.attacker.tmpabilities.hit;
    const int victim_hit_before = context.victim.tmpabilities.hit;

    dragon(&context.attacker, nullptr, 0, nullptr, 0, nullptr);

    EXPECT_EQ(context.attacker.tmpabilities.hit, attacker_hit_before)
        << "dragon() must skip the host itself (tmpch != host) while walking room_of(host)'s occupants.";
    EXPECT_LT(context.victim.tmpabilities.hit, victim_hit_before)
        << "dragon() should apply dice(mob_level, 6) dragonsbreath damage to the other room occupant "
        << "reached through the converted room_of(host)->people save-next walk.";

    clear_test_random_values();
}

TEST(SpecProDragon, DoesNothingWhenTheHostIsNotFighting) {
    DragonBreathContext context;
    context.attacker.specials.position = POSITION_STANDING;

    const int victim_hit_before = context.victim.tmpabilities.hit;

    EXPECT_EQ(dragon(&context.attacker, nullptr, 0, nullptr, 0, nullptr), 0);
    EXPECT_EQ(context.victim.tmpabilities.hit, victim_hit_before);
}

TEST(SpecProHealingPlant, HealsGoodOccupantsButSkipsTheHostItself) {
    DamageTestContext context;
    context.attacker.player.level = 20; // level = max(1, GET_LEVEL(host)/2) == 10 -> number(1, 10).
    context.victim.specials2.alignment = 500; // IS_GOOD(victim): GET_ALIGNMENT(ch) >= 100.
    context.victim.abilities.hit = 500;
    context.victim.tmpabilities.hit = 100;
    context.attacker.specials2.alignment = 500; // Good too, but must still be skipped (host == character).
    const int attacker_hit_before = context.attacker.tmpabilities.hit;

    clear_test_random_values();
    push_test_random_value(0.5); // number(1, 10) -> 1 + int(0.5 * 10) == 6.

    healing_plant(&context.attacker, nullptr, 0, nullptr, SPECIAL_SELF, nullptr);

    EXPECT_EQ(context.victim.tmpabilities.hit, 106)
        << "healing_plant() should heal a good-aligned occupant reached through the converted "
        << "occupants(room_of(host)) walk by number(1, level).";
    EXPECT_EQ(context.attacker.tmpabilities.hit, attacker_hit_before)
        << "healing_plant() must skip the host itself (host != character) even though it is also good-aligned.";

    clear_test_random_values();
}

TEST(SpecProHealingPlant, SkipsEvilOccupants) {
    DamageTestContext context;
    context.attacker.player.level = 20;
    context.victim.specials2.alignment = -500; // Evil: IS_GOOD() is false.
    context.victim.abilities.hit = 500;
    context.victim.tmpabilities.hit = 100;
    const int victim_hit_before = context.victim.tmpabilities.hit;

    healing_plant(&context.attacker, nullptr, 0, nullptr, SPECIAL_SELF, nullptr);

    EXPECT_EQ(context.victim.tmpabilities.hit, victim_hit_before)
        << "healing_plant() should leave an evil-aligned occupant untouched.";
}
