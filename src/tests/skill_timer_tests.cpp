// skill_timer_tests.cpp

// New test TU (combat-seed wave, Task 3b; plan
// docs/superpowers/plans/2026-07-19-combat-seed.md; brief
// .superpowers/sdd/task-3b-brief.md). Standing coverage-gap rule: CS Task 1's
// coverage citation (task-1-report.md Step 4) found add_skill_timer/
// update_skill_timer/is_skill_allowed (skill_timer.cpp) had ZERO direct test
// coverage (5 real callers each -- act_offe.cpp/olog_hai.cpp's ACMDs, and
// comm.cpp's heartbeat, respectively -- none exercised by any existing test),
// and report_skill_status was only reached indirectly through an always-empty
// do_affections path (act_info_format_tests.cpp's ensure_skill_timer_created()
// tests never populate m_skill_timer). This file exercises all four public
// methods directly through the real game_timer::skill_timer singleton -- no
// mocks. add_global_cooldown is private and is exercised only as
// add_skill_timer's documented side effect, matching its access level.
//
// PROCESS-WIDE SINGLETON HYGIENE: skill_timer.h's game_timer::skill_timer
// derives from world_singleton<T>, whose create() stores its instance in a
// function-local static (singleton.h) -- constructed exactly once for the
// whole test binary by gtest_main.cpp's game_timer::skill_timer::create()
// call, and never destroyed or reset between tests. There is no public
// clear()/reset(): production never needs one (the singleton lives for the
// whole server process). Every test below begins with the SkillTimerTest
// fixture's SetUp(), which drains any leftover m_skill_timer entries from
// earlier tests via a generous bound of real update_skill_timer() calls
// before the test body runs, so each test observes an effectively-empty
// timer list. Task 1's grep (task-1-report.md Step 4) confirmed no OTHER test
// file in the suite calls add_skill_timer -- the ACMD entry points that do
// (do_defend/do_cleave/do_smash/do_overrun/do_frenzy/do_stomp) are never
// directly invoked by any existing test -- so this file is the only source of
// entries the singleton will ever see in the test process; the drain bound
// only has to outlast the small counters (<=7) THIS file's own tests use.
#include "../char_utils.h"
#include "../skill_timer.h"
#include "../spells.h"
#include "rots/core/character.h"

#include <format>
#include <gtest/gtest.h>
#include <string>

namespace {

// Each tick decrements positive counters and erases counters already at zero.
// Thirty ticks comfortably drain this file's counters (at most seven) before
// each test, without adding a production-only reset API.
constexpr int kSkillTimerDrainIterations = 30;

void drain_skill_timer(game_timer::skill_timer &timer) {
    for (int i = 0; i < kSkillTimerDrainIterations; ++i) {
        timer.update_skill_timer();
    }
}

// A minimal non-NPC char_data fixture carrying only the fields
// skill_timer.cpp's public API reads: specials2.idnum (utils::get_idnum())
// and specials2.act (utils::is_npc(), left 0/false by char_data{}'s default
// member initialization). idnum is caller-assigned per test so that even a
// mistuned drain bound could not make two tests' entries collide.
struct SkillTimerCharacter {
    // The character under test; specials2.idnum is the only field
    // skill_timer.cpp's non-NPC path reads.
    char_data character{};

    explicit SkillTimerCharacter(long idnum) { character.specials2.idnum = idnum; }
};

// Hands out a fresh idnum per call so each test's timer entries are
// unambiguously its own, independent of the drain bound above. Low
// sequential ids are safe within this file (the SetUp() drain plus this
// generator make every entry unambiguous), but a FUTURE test file that also
// touches the process-wide game_timer::skill_timer singleton should prefer
// a high sentinel id (>=90210001) instead, per act_info_format_tests.cpp's
// DoAffectionsFormatsNotAffectedLineWhenNoActiveEffects precedent -- that
// keeps a report_skill_timer()/report_skill_status() lookup from ever
// colliding with another suite's entries.
long next_player_id() {
    static long id = 1;
    return id++;
}

} // namespace

class SkillTimerTest : public ::testing::Test {
  protected:
    void SetUp() override { drain_skill_timer(game_timer::skill_timer::instance()); }
};

TEST_F(SkillTimerTest, AllowsSkillByDefaultWhenNoTimerActive) {
    SkillTimerCharacter pc(next_player_id());
    game_timer::skill_timer &timer = game_timer::skill_timer::instance();

    EXPECT_TRUE(timer.is_skill_allowed(pc.character, SKILL_DEFEND))
        << "Expected a player with no active timers to be allowed to use any skill.";
}

TEST_F(SkillTimerTest, AddSkillTimerBlocksTheSameSkill) {
    SkillTimerCharacter pc(next_player_id());
    game_timer::skill_timer &timer = game_timer::skill_timer::instance();

    timer.add_skill_timer(pc.character, SKILL_DEFEND, 5);

    EXPECT_FALSE(timer.is_skill_allowed(pc.character, SKILL_DEFEND))
        << "Expected the just-added skill to be blocked until its timer expires.";
}

TEST_F(SkillTimerTest, AddSkillTimerAlsoBlocksADifferentSkillViaGlobalCooldown) {
    SkillTimerCharacter pc(next_player_id());
    game_timer::skill_timer &timer = game_timer::skill_timer::instance();

    timer.add_skill_timer(pc.character, SKILL_DEFEND, 5);

    EXPECT_FALSE(timer.is_skill_allowed(pc.character, SKILL_CLEAVE))
        << "Expected add_skill_timer's implicit global-cooldown entry to also block an unrelated "
           "skill.";
}

TEST_F(SkillTimerTest, AddSkillTimerIsNoOpForNpcCharacters) {
    SkillTimerCharacter npc(next_player_id());
    npc.character.specials2.act |= MOB_ISNPC;
    char buffer[256] = {};
    game_timer::skill_timer &timer = game_timer::skill_timer::instance();

    timer.add_skill_timer(npc.character, SKILL_DEFEND, 5);

    // utils::get_idnum() collapses every NPC to id -1 (char_utils.cpp); an
    // add_skill_timer() that incorrectly pushed an entry for an NPC would
    // show up under that shared id, so report_skill_status(-1, ...) is the
    // one observable channel that can prove nothing was added. No PC test in
    // this file ever uses id -1 (next_player_id() starts at 1), so this
    // assertion cannot pick up cross-test residue.
    timer.report_skill_status(-1, buffer);

    EXPECT_STREQ(buffer, "")
        << "Expected add_skill_timer to no-op for NPCs (utils::is_npc() short-circuits before the "
           "timer list is touched), leaving no entry for report_skill_status to format.";
}

TEST_F(SkillTimerTest, IsSkillAllowedAlwaysTrueForNpcsRegardlessOfTimers) {
    SkillTimerCharacter npc(next_player_id());
    npc.character.specials2.act |= MOB_ISNPC;
    game_timer::skill_timer &timer = game_timer::skill_timer::instance();

    EXPECT_TRUE(timer.is_skill_allowed(npc.character, SKILL_DEFEND))
        << "Expected is_skill_allowed to bypass the timer list entirely for NPCs.";
}

TEST_F(SkillTimerTest, AddSkillTimerIsNoOpWhenSkillAlreadyOnCooldown) {
    SkillTimerCharacter pc(next_player_id());
    char buffer[256] = {};
    game_timer::skill_timer &timer = game_timer::skill_timer::instance();

    timer.add_skill_timer(pc.character, SKILL_DEFEND, 5);
    // is_skill_allowed() now reports false for SKILL_DEFEND, so this second
    // call's own is_skill_allowed() guard should reject it: no duplicate
    // entry, and the original counter (5) survives untouched by the rejected
    // counter (99).
    timer.add_skill_timer(pc.character, SKILL_DEFEND, 99);

    timer.report_skill_status(static_cast<int>(pc.character.specials2.idnum), buffer);

    const std::string expected =
        std::format("{:<30} {:<3} (seconds)\n\r", utils::get_skill_name(SKILL_DEFEND), 5);
    EXPECT_STREQ(buffer, expected.c_str())
        << "Expected the rejected second add_skill_timer call to leave the original counter (5) "
           "unchanged and add no duplicate entry.";
}

TEST_F(SkillTimerTest, ReportSkillStatusFormatsActiveTimersAndSkipsGlobalCooldown) {
    SkillTimerCharacter pc(next_player_id());
    char buffer[256] = {};
    game_timer::skill_timer &timer = game_timer::skill_timer::instance();

    timer.add_skill_timer(pc.character, SKILL_DEFEND, 7);
    const int written =
        timer.report_skill_status(static_cast<int>(pc.character.specials2.idnum), buffer);

    const std::string expected =
        std::format("{:<30} {:<3} (seconds)\n\r", utils::get_skill_name(SKILL_DEFEND), 7);
    EXPECT_EQ(written, 1) << "Expected report_skill_status to always report success.";
    EXPECT_STREQ(buffer, expected.c_str())
        << "Expected exactly one formatted line for the active skill timer, and no line for "
           "add_skill_timer's own global-cooldown entry (report_skill_status filters GLOBAL_SKILL "
           "out).";
}

TEST_F(SkillTimerTest, DecrementsBeforeErasingOnTheFollowingTick)
{
    SkillTimerCharacter player(next_player_id());
    char report[256] = { };
    auto& timer = game_timer::skill_timer::instance();
    const int player_id = static_cast<int>(player.character.specials2.idnum);
    timer.add_skill_timer(player.character, SKILL_DEFEND, 1);
    ASSERT_FALSE(timer.is_skill_allowed(player.character, SKILL_DEFEND));
    ASSERT_FALSE(timer.is_skill_allowed(player.character, SKILL_CLEAVE));

    // A counter decremented to zero stays present until the next tick.
    timer.update_skill_timer();
    timer.report_skill_status(player_id, report);
    EXPECT_STRNE(report, "");

    // Erasing the skill still lets the global cooldown decrement from one to zero.
    timer.update_skill_timer();
    report[0] = '\0';
    timer.report_skill_status(player_id, report);
    EXPECT_STREQ(report, "");
    EXPECT_FALSE(timer.is_skill_allowed(player.character, SKILL_DEFEND));
    EXPECT_FALSE(timer.is_skill_allowed(player.character, SKILL_CLEAVE));

    timer.update_skill_timer();
    EXPECT_TRUE(timer.is_skill_allowed(player.character, SKILL_DEFEND));
    EXPECT_TRUE(timer.is_skill_allowed(player.character, SKILL_CLEAVE));
}

TEST_F(SkillTimerTest, ErasesAdjacentExpiredEntriesInTheSameTick)
{
    SkillTimerCharacter first_player(next_player_id());
    SkillTimerCharacter second_player(next_player_id());
    auto& timer = game_timer::skill_timer::instance();
    timer.add_skill_timer(first_player.character, SKILL_DEFEND, 2);
    timer.add_skill_timer(second_player.character, SKILL_DEFEND, 2);
    timer.update_skill_timer();
    timer.update_skill_timer();
    EXPECT_FALSE(timer.is_skill_allowed(first_player.character, SKILL_DEFEND));
    timer.update_skill_timer();
    EXPECT_TRUE(timer.is_skill_allowed(first_player.character, SKILL_DEFEND));
    EXPECT_TRUE(timer.is_skill_allowed(second_player.character, SKILL_DEFEND));
}

TEST_F(SkillTimerTest, DecrementsTheTimerShiftedPastAnExpiredGlobalCooldown)
{
    SkillTimerCharacter first_player(next_player_id());
    SkillTimerCharacter second_player(next_player_id());
    auto& timer = game_timer::skill_timer::instance();
    timer.add_skill_timer(first_player.character, SKILL_DEFEND, 5);
    timer.add_skill_timer(second_player.character, SKILL_DEFEND, 5);
    timer.update_skill_timer();
    timer.update_skill_timer();
    timer.update_skill_timer();
    char report[256] = { };
    const int player_id = static_cast<int>(second_player.character.specials2.idnum);
    timer.report_skill_status(player_id, report);
    const std::string expected = std::format("{:<30} {:<3} (seconds)\n\r", utils::get_skill_name(SKILL_DEFEND), 2);
    EXPECT_STREQ(report, expected.c_str());
}
