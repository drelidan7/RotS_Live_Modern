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

// Every real update_skill_timer() call decrements each still-positive
// counter by one and erases any entry whose counter has reached zero -- but
// its erase-during-iterate loop (skill_timer.cpp) skips whichever entry
// shifts into an erased slot for that one call, so a single entry can take
// up to roughly 2x its counter value to fully drain. This file's own tests
// never push a counter above 7 (report_skill_status) or the fixed
// GLOBAL_COOLDOWN_COUNTER (2), so a fixed bound of 30 calls -- run before
// every test -- reliably empties whatever the PREVIOUS test in this file
// left behind, without needing a production reset() this class doesn't have.
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

TEST_F(SkillTimerTest, UpdateSkillTimerDecrementsThenErasesEntriesOneCallAtATime) {
    // Documents skill_timer.cpp's real update_skill_timer() semantics. Two
    // things worth pinning down: (1) its erase-during-iterate quirk --
    // erasing element i shifts element i+1 into its place, but the for-loop's
    // index still advances to i+1, so whichever entry shifts into the erased
    // slot is skipped for that one call and is only processed on the NEXT
    // call; and (2) is_skill_allowed()'s global-cooldown entry blocks EVERY
    // skill for a player, not just the one add_skill_timer() was called for
    // -- so is_skill_allowed() alone can't distinguish "the specific skill
    // entry was erased" from "the global-cooldown entry is still present".
    // Only report_skill_status() (which filters GLOBAL_SKILL entries out)
    // can observe the skill-specific entry's erasure independently of the
    // global cooldown. With the SkillTimerTest fixture draining every
    // earlier entry in SetUp(), this test's two entries (the SKILL_DEFEND
    // timer and its global-cooldown sibling, pushed adjacently by the single
    // add_skill_timer() call below) are the only two in the vector, so their
    // exact call-by-call fate is fully deterministic.
    SkillTimerCharacter pc(next_player_id());
    char buffer[256] = {};
    game_timer::skill_timer &timer = game_timer::skill_timer::instance();
    const int player_id = static_cast<int>(pc.character.specials2.idnum);

    timer.add_skill_timer(pc.character, SKILL_DEFEND, 1);
    ASSERT_FALSE(timer.is_skill_allowed(pc.character, SKILL_DEFEND));
    ASSERT_FALSE(timer.is_skill_allowed(pc.character, SKILL_CLEAVE))
        << "Sanity check: the implicit global-cooldown entry (counter=2) starts out blocking too.";

    // Call 1: both entries have counter > 0, so both are decremented in
    // place (skill: 1 -> 0, global: 2 -> 1); neither is erased yet.
    timer.update_skill_timer();
    buffer[0] = '\0';
    timer.report_skill_status(player_id, buffer);
    EXPECT_STRNE(buffer, "")
        << "A counter that reached 0 still has a live entry until a LATER call erases it.";

    // Call 2: the skill entry (counter=0) is erased -- report_skill_status
    // no longer has a line for it -- but the global-cooldown entry that
    // shifted into its slot is skipped this call (the erase-during-iterate
    // quirk above), so it still blocks every skill.
    timer.update_skill_timer();
    buffer[0] = '\0';
    timer.report_skill_status(player_id, buffer);
    EXPECT_STREQ(buffer, "")
        << "Expected the skill-specific entry to be erased on the second call.";
    EXPECT_FALSE(timer.is_skill_allowed(pc.character, SKILL_DEFEND))
        << "Expected the still-present global-cooldown entry to keep blocking every skill, even "
           "though the specific SKILL_DEFEND entry is already gone.";
    EXPECT_FALSE(timer.is_skill_allowed(pc.character, SKILL_CLEAVE));

    // Call 3: the global entry, now at index 0, is finally visited again and
    // decremented (1 -> 0); still present, so it still blocks.
    timer.update_skill_timer();
    EXPECT_FALSE(timer.is_skill_allowed(pc.character, SKILL_CLEAVE));

    // Call 4: counter is now 0, so this call erases it -- every skill
    // (including SKILL_DEFEND, whose own entry was already gone since call
    // 2) is allowed again from here.
    timer.update_skill_timer();
    EXPECT_TRUE(timer.is_skill_allowed(pc.character, SKILL_DEFEND));
    EXPECT_TRUE(timer.is_skill_allowed(pc.character, SKILL_CLEAVE))
        << "Expected the global-cooldown entry to finally expire on the fourth call.";
}
