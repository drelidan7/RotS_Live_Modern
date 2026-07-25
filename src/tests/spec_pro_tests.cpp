#include "../spells.h"
#include "rots/core/character.h"
#include "rots/core/types.h"
#include "../utils.h"
#include "../interpre.h"
#include "../combat_hooks.h"
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

// SPECIAL(mob_ranger) / SPECIAL(mob_ranger_new) / SPECIAL(vampire_huntress) /
// SPECIAL(vampire_killer) -- LS-2 T4-legacy Family-F empty-room regression
// follow-up (ls2-global-constraints.md's "Inherited test debt" section, item
// 2; ls1-task-2-review.md section A, all six sites). None declared in a
// header (same SPECIAL()-family reasoning as dragon/healing_plant above).
// Of the six sites, only the three inside vampire_killer (victim/victim/
// victim2) were genuinely uninitialized in the pre-LS-1 code -- the other
// three (mob_ranger's tmpch, mob_ranger_new's tmpch, vampire_huntress's
// victim) already carried an explicit `= 0;` pre-init and are Family-F only
// in the weaker "the walk conversion needs the same break/pre-init
// scrutiny" sense (see ls1-task-2-review.md's per-site table).
extern int mob_ranger(char_data* host, char_data* ch, int cmd, char* arg, int callflag,
    waiting_type* wtl);
extern int mob_ranger_new(char_data* host, char_data* ch, int cmd, char* arg, int callflag,
    waiting_type* wtl);
extern int vampire_huntress(char_data* host, char_data* ch, int cmd, char* arg, int callflag,
    waiting_type* wtl);
extern int vampire_killer(char_data* host, char_data* ch, int cmd, char* arg, int callflag,
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

namespace {

// -----------------------------------------------------------------------
// SPECIAL(mob_ranger) -- site #1 (ls1-task-2-review.md section A): the
// "ambush" walk, originally `for (tmpch = world[host->in_room].people;
// tmpch; tmpch = tmpch->next_in_room) if (...) { tmpch = occ; break; }`,
// guarded by a pre-existing `tmpch = 0;` immediately before the if/else
// chain reaching it. mob_ranger's own return value cleanly distinguishes
// "ambush queued" (1) from "nothing found" (0), so no hook substitution is
// needed here (contrast SpecProMobRangerNew below, whose every path ends in
// `return 1;`).
// -----------------------------------------------------------------------

struct MobRangerContext {
    ScopedTestWorld test_world{1};
    char_data host{};
    char_data occupant{};
    char host_name[16] = "test_ranger";
    char occupant_name[16] = "test_target";

    MobRangerContext()
    {
        world[0].light = 1; // Unlit rooms fail CAN_SEE's darkness check.
        host.specials2.act = MOB_ISNPC;
        host.player.name = host_name;
        // POSITION_FIGHTING skips the `GET_POS(host) < POSITION_FIGHTING ->
        // update_pos(host)` call at function entry and, being neither
        // POSITION_STANDING, also keeps the do_hide() call and the final
        // wander-movement block (both well past the ambush check) out of
        // this rider's reach.
        host.specials.position = POSITION_FIGHTING;
        host.in_room = 0;
        host.next_in_room = &occupant;

        occupant.player.name = occupant_name;
        occupant.in_room = 0;
        occupant.next_in_room = nullptr;

        world[0].people = &host;
    }

    ~MobRangerContext()
    {
        world[0].people = nullptr;
        host.next_in_room = nullptr;
    }
};

} // namespace

TEST(SpecProMobRanger, AmbushesTheOccupantMatchingItsRaceAggressionMask) {
    MobRangerContext context;
    // IS_AGGR_TO(host, occupant) := host->specials2.pref & (1 << GET_RACE(occupant)).
    context.host.specials2.pref = (1 << RACE_HUMAN);
    context.occupant.player.race = RACE_HUMAN;

    const int result =
        mob_ranger(&context.host, &context.host, 0, mutable_arg(""), SPECIAL_SELF, nullptr);

    EXPECT_EQ(result, 1)
        << "Expected the converted occupants(room_of(host)) walk to reach the race-matching "
           "occupant (tmpch=0 pre-init, then tmpch=occ on match) and queue an ambush.";
}

TEST(SpecProMobRanger, DoesNothingWhenNoOccupantMatchesTheRaceAggressionMask) {
    MobRangerContext context;
    context.host.specials2.pref = (1 << RACE_HUMAN);
    context.occupant.player.race = RACE_HARAD; // non-matching -- must be skipped.

    const int result =
        mob_ranger(&context.host, &context.host, 0, mutable_arg(""), SPECIAL_SELF, nullptr);

    EXPECT_EQ(result, 0)
        << "Expected the walk to leave tmpch at its pre-init value (0) when no occupant "
           "matches -- the empty/no-match path this follow-up pins.";
}

namespace {

// -----------------------------------------------------------------------
// SPECIAL(mob_ranger_new) -- site #2 ("p_stab" aggression walk,
// should_attack()-gated). Every path through mob_ranger_new ends in
// `return 1;` (spec_pro.cpp's unconditional tail return), so found/
// not-found is observed by substituting a recording stub for
// combat_hooks.h's `hit` dispatch cell -- the same technique
// mobact_tests.cpp's ScopedRecordingHitHook uses for one_mobile_activity()
// -- rather than by return value.
// -----------------------------------------------------------------------

struct RecordedSpecProHitCall {
    char_data* target = nullptr;
    bool called = false;
};

RecordedSpecProHitCall g_recorded_spec_pro_hit_call;

void recording_spec_pro_hit_stub(char_data* /*ch*/, char* /*argument*/, waiting_type* wtl,
    int /*cmd*/, int /*subcmd*/)
{
    g_recorded_spec_pro_hit_call = RecordedSpecProHitCall{ wtl ? wtl->targ1.ptr.ch : nullptr, true };
}

struct ScopedRecordingSpecProHitHook {
    ScopedRecordingSpecProHitHook()
    {
        rots::combat::set_combat_command(
            rots::combat::combat_command::hit, recording_spec_pro_hit_stub);
    }

    ~ScopedRecordingSpecProHitHook() { register_combat_command_dispatch(); }

    ScopedRecordingSpecProHitHook(const ScopedRecordingSpecProHitHook&) = delete;
    ScopedRecordingSpecProHitHook& operator=(const ScopedRecordingSpecProHitHook&) = delete;
};

struct MobRangerNewContext {
    ScopedTestWorld test_world{1};
    char_data host{};
    char_data occupant{};
    char host_name[16] = "test_stabber";
    char occupant_name[16] = "test_mark";

    MobRangerNewContext()
    {
        world[0].light = 1;
        // MOB_SENTINEL keeps the unrelated final wander-movement block
        // (:2230, past every path this rider drives) out of reach
        // regardless of position.
        host.specials2.act = MOB_ISNPC | MOB_SENTINEL;
        host.player.name = host_name;
        host.specials.position = POSITION_FIGHTING;
        host.abilities.hit = 500;
        host.tmpabilities.hit = 500; // well above wimpy_health_limit (max/5) -- is_wimpy stays 0.
        host.in_room = 0;
        host.next_in_room = &occupant;

        occupant.player.name = occupant_name;
        occupant.in_room = 0;
        occupant.next_in_room = nullptr;

        world[0].people = &host;
    }

    ~MobRangerNewContext()
    {
        world[0].people = nullptr;
        host.next_in_room = nullptr;
    }
};

} // namespace

TEST(SpecProMobRangerNew, HitsTheOccupantMatchingItsRaceAggressionMask) {
    MobRangerNewContext context;
    ScopedRecordingSpecProHitHook hook;
    g_recorded_spec_pro_hit_call = RecordedSpecProHitCall{};

    context.host.specials2.pref = (1 << RACE_HUMAN);
    context.occupant.player.race = RACE_HUMAN;

    mob_ranger_new(&context.host, &context.host, 0, mutable_arg(""), SPECIAL_SELF, nullptr);

    EXPECT_TRUE(g_recorded_spec_pro_hit_call.called)
        << "Expected the converted occupants(room_of(host)) walk (should_attack() branch) to "
           "reach the matching occupant and issue a hit.";
    EXPECT_EQ(g_recorded_spec_pro_hit_call.target, &context.occupant);
}

TEST(SpecProMobRangerNew, DoesNothingWhenNoOccupantMatchesTheRaceAggressionMask) {
    MobRangerNewContext context;
    ScopedRecordingSpecProHitHook hook;
    g_recorded_spec_pro_hit_call = RecordedSpecProHitCall{};

    context.host.specials2.pref = (1 << RACE_HUMAN);
    context.occupant.player.race = RACE_HARAD; // non-matching.

    mob_ranger_new(&context.host, &context.host, 0, mutable_arg(""), SPECIAL_SELF, nullptr);

    EXPECT_FALSE(g_recorded_spec_pro_hit_call.called)
        << "Expected the walk to leave tmpch at its pre-init nullptr and never call "
           "issue_command(hit, ...) -- mob_ranger_new's tail always returns 1 regardless, so "
           "the hit dispatch (not the return value) is this path's only external signal.";
}

namespace {

// -----------------------------------------------------------------------
// SPECIAL(vampire_killer) -- sites #4/#5/#6, the three GENUINE UNINIT traps
// LS-1's Family-F conversion fixed (`victim`/`victim`/`victim2`, all
// declared but never initialized in the original code). All three walks
// fire in one call: the self-room bite loop (:3272) finds nothing and
// falls through (rather than returning early), reaching the two
// fixed-room loops (:3298 over real_room(15399), :3307 over
// real_room(15398)).
//
// Each of the three rooms gets an NPC bystander rather than being left
// literally empty, so the test proves each walk reads REAL occupant data
// through its resolver (self-room -> room_of(host); fixed rooms ->
// room_by_id_total(real_room(vnum))) and correctly applies the
// `!IS_NPC()` filter, not merely that an empty range-for is a no-op. Room
// 15399 is deliberately placed at world index 2 == top_of_world: per
// ls2-global-constraints.md's room_by_id() ban rationale,
// room_by_id_impl rejects `rnum >= top_of_world` (a graceful
// room_by_id_total() fallback would not), so a regression that
// substituted the banned room_by_id() for either fixed-room site would
// crash THIS fixture rather than pass silently.
//
// Driving any of the three walks to its FOUND outcome is deliberately out
// of scope for this follow-up: the self-room bite's success path calls the
// real raw_kill() (corpse creation, save_char(), crash_crashsave() -- file
// writes and the full PC-death machinery), and the two fixed-room walks'
// success path falls into the `else` branch's CAN_GO()/
// issue_command(move) door-state machinery -- both pull in subsystems
// entirely unrelated to the location-read conversion this follow-up is
// pinning. See the T4 report for the full cost accounting.
//
// Host's own room number is deliberately NOT 15398/15399: the
// `if (!which_room)` block's `case 15399:`/`case 15398:` arms set
// `tmpwtl.cmd = 1`, and the immediately following `if (tmpwtl.cmd == 1)`
// dereferences the function-local `room_data* room = 0;` (never assigned
// on this path) -- a pre-existing, out-of-scope latent null-deref this
// fixture must simply avoid triggering, not fix.
// -----------------------------------------------------------------------

struct VampireKillerContext {
    ScopedTestWorld test_world{3};
    char_data host{};
    char_data bystander_home{};
    char_data bystander_15399{};
    char_data bystander_15398{};
    char host_name[16] = "test_vampire";
    char bystander_home_name[16] = "test_wraith";
    char bystander_15399_name[16] = "test_guard_a";
    char bystander_15398_name[16] = "test_guard_b";

    VampireKillerContext()
    {
        world[0].number = 1; // host's own room -- NOT 15398/15399, see block comment above.
        world[1].number = 15398;
        world[2].number = 15399; // == top_of_world -- see the room_by_id() ban note above.
        world[0].light = 1;
        world[1].light = 1;
        world[2].light = 1;

        host.specials2.act = MOB_ISNPC;
        host.player.name = host_name;
        host.player.short_descr = host_name; // GET_NAME() reads short_descr for NPCs (IS_NPC(host)).
        host.in_room = 0;
        host.next_in_room = &bystander_home;

        bystander_home.specials2.act = MOB_ISNPC; // excluded by the walk's `!IS_NPC()` filter.
        bystander_home.player.name = bystander_home_name;
        bystander_home.player.short_descr = bystander_home_name;
        bystander_home.in_room = 0;
        bystander_home.next_in_room = nullptr;
        world[0].people = &host;

        bystander_15399.specials2.act = MOB_ISNPC;
        bystander_15399.player.name = bystander_15399_name;
        bystander_15399.player.short_descr = bystander_15399_name;
        bystander_15399.in_room = 2;
        bystander_15399.next_in_room = nullptr;
        world[2].people = &bystander_15399;

        bystander_15398.specials2.act = MOB_ISNPC;
        bystander_15398.player.name = bystander_15398_name;
        bystander_15398.player.short_descr = bystander_15398_name;
        bystander_15398.in_room = 1;
        bystander_15398.next_in_room = nullptr;
        world[1].people = &bystander_15398;
    }

    ~VampireKillerContext()
    {
        world[0].people = nullptr;
        world[1].people = nullptr;
        world[2].people = nullptr;
        host.next_in_room = nullptr;
    }
};

} // namespace

TEST(SpecProVampireKiller, ExcludesNpcBystandersInAllThreeRoomsAndTakesNoAction) {
    VampireKillerContext context;
    clear_test_random_values();
    // `if (!which_room)`'s `default:` arm (host's own room number matches
    // no case): number(0, 1) (result unobserved by this test) then
    // number(0, 15) MUST be nonzero so the function returns immediately,
    // before CAN_GO()/issue_command(move) -- machinery this rider
    // deliberately does not drive (see the block comment above).
    push_test_random_value(0.5);
    push_test_random_value(0.5);

    const int result =
        vampire_killer(&context.host, &context.host, 0, mutable_arg(""), SPECIAL_SELF, nullptr);

    EXPECT_EQ(result, 0);
    EXPECT_EQ(context.host.in_room, 0)
        << "The host should never have moved -- all three walks correctly found no eligible "
           "(non-NPC) victim in any of the three rooms.";
    clear_test_random_values();
}

namespace {

// -----------------------------------------------------------------------
// SPECIAL(vampire_huntress) -- site #3 ("kidnap" walk). Reaching it
// requires the function's own day/night wander block to run first
// (host must NOT be POSITION_FIGHTING/AFF_BASH) and move host into a real
// destination room via real_room()/char_to_room() -- weather_info.sunlight
// is pinned to SUN_RISE so the destination is unconditionally 15379
// regardless of host's starting room number (spec_pro.cpp's SUN_RISE
// override fires after -- and replaces -- whatever the preceding
// room-number switch computed).
//
// The destination room gets an NPC bystander (rather than being left
// empty) so the test proves the post-move walk reads the REAL destination
// room's occupant chain through room_of(host) and correctly applies the
// `!IS_NPC()` filter. Driving to the FOUND outcome is out of scope: tmpno
// == 1 is a multi-room kidnap sequence (door state across two more rooms)
// and tmpno == 2 calls the real melee hit() -- both unrelated to the
// location-read conversion under test. See the T4 report.
// -----------------------------------------------------------------------

struct VampireHuntressContext {
    ScopedTestWorld test_world{2};
    char_data host{};
    char_data bystander{};
    char host_name[16] = "test_huntress";
    char bystander_name[16] = "test_watchman";
    int saved_sunlight = 0;

    VampireHuntressContext()
    {
        saved_sunlight = weather_info.sunlight;
        weather_info.sunlight = SUN_RISE;

        world[0].number = 1; // host's starting room -- arbitrary (SUN_RISE overrides to_room).
        world[1].number = 15379; // real_room(15379) destination -- == top_of_world.
        world[0].light = 1;
        world[1].light = 1;

        host.specials2.act = MOB_ISNPC;
        host.player.name = host_name;
        host.player.short_descr = host_name; // GET_NAME() reads short_descr for NPCs (IS_NPC(host)).
        // Must NOT be POSITION_FIGHTING (required to enter the day/night
        // wander block at all: `GET_POS(host) != POSITION_FIGHTING`).
        host.specials.position = POSITION_STANDING;
        host.in_room = 0;
        host.next_in_room = nullptr;
        world[0].people = &host;

        bystander.specials2.act = MOB_ISNPC; // excluded by the walk's `!IS_NPC()` filter.
        bystander.player.name = bystander_name;
        bystander.player.short_descr = bystander_name;
        bystander.in_room = 1;
        bystander.next_in_room = nullptr;
        world[1].people = &bystander;
    }

    ~VampireHuntressContext()
    {
        weather_info.sunlight = saved_sunlight;
        world[0].people = nullptr;
        world[1].people = nullptr;
        host.next_in_room = nullptr;
    }
};

} // namespace

TEST(SpecProVampireHuntress, ExcludesAnNpcBystanderInTheDestinationRoomAfterTheNightlyMove) {
    VampireHuntressContext context;
    clear_test_random_values();
    // `tmpno = number(0, 2)` must be nonzero to enter the kidnap/attack
    // decision block at all (the 1-in-3 "fly off" branch takes an early
    // return this test does not want).
    push_test_random_value(0.9);

    const int result = vampire_huntress(
        &context.host, nullptr, 0, mutable_arg(""), SPECIAL_SELF, nullptr);

    EXPECT_EQ(result, 0);
    EXPECT_EQ(context.host.in_room, 1)
        << "Expected the day/night wander block's real_room(15379)/char_to_room() call to have "
           "moved the host into the destination room before the converted occupants(room_of("
           "host)) walk ran.";
    clear_test_random_values();
}
