#include "../spells.h"
#include "rots/core/character.h"
#include "rots/core/types.h"
#include "../utils.h"
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
