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
    // Site 8 (LS-2 whole-branch review B1): world[0].light is never reset by
    // ScopedTestWorld's reuse branch -- saved here, restored in the dtor
    // below, so it doesn't leak into a later test sharing this process's
    // world[0].
    byte original_light = 0;

    MobRangerNewContext()
    {
        original_light = world[0].light;
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
        world[0].light = original_light;
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
// O-I3 (LS-2 whole-branch review, Opus): the negative-control test below
// used to be the ONLY test here, and it drove every one of the three walks
// to "nothing found" -- so it passed identically whether the walks read
// the right rooms, the wrong rooms, or were deleted outright, pinning
// nothing. Two of the three (:3298/:3307, the fixed-room walks) now ALSO
// have a positive control each (below): each gives exactly one fixed room
// a PC bystander (excluded by neither CAN_SEE() nor !IS_NPC()) and forces
// the found branch's own "slow him down" gate (number(0, 10)) to 0,
// observing the resulting issue_command(move, ...) dispatch through a
// recording hook on the `move` combat_command cell -- the same technique
// ScopedRecordingSpecProHitHook already uses for mob_ranger_new below,
// applied to the cell this found branch actually dispatches through (not
// `hit`).
//
// The self-room walk's (:3272) FOUND outcome remains out of scope: its
// success path calls the real raw_kill() (corpse creation, save_char(),
// crash_crashsave() -- file writes and the full PC-death machinery).
// CharacterizationCombatTest.DamageTranscriptSeed42
// (characterization_combat_tests.cpp) shows what safely driving that path
// costs -- a fabricated mob_index slot, global_release_flag=0,
// character_list wiring, register_npc_char()/remove_char_exists()
// bookkeeping, and a placement-new reconstruction of the stack victim
// after free_char() runs on it -- disproportionate to this coverage
// follow-up. Re-deferred with this cost recorded, matching the ruling's
// own "4 of 6 sites pinned; the other two re-deferred with cost recorded"
// fallback (ls2-review-adjudication.md O-I3; the sixth site is
// vampire_huntress's own found path, re-deferred below for a different
// reason).
//
// Each of the three rooms still gets an NPC bystander in the (unmodified)
// negative control below, so it still proves each walk reads REAL
// occupant data through its resolver (self-room -> room_of(host); fixed
// rooms -> room_by_id_total(real_room(vnum))) and correctly applies the
// `!IS_NPC()` filter when nobody eligible is present -- now the genuine
// negative half of a discriminator pair for the two fixed-room sites,
// rather than the whole story for all three. Room 15399 is deliberately
// placed at world index 2 == top_of_world: per
// ls2-global-constraints.md's room_by_id() ban rationale,
// room_by_id_impl rejects `rnum >= top_of_world` (a graceful
// room_by_id_total() fallback would not), so a regression that
// substituted the banned room_by_id() for either fixed-room site would
// crash THIS fixture rather than pass silently.
//
// Host's own room number is deliberately NOT 15398/15399 in the negative
// control (the two new positive controls below use 15395 instead -- a
// DIFFERENT case in the found branch's own switch, chosen so its
// `tmpwtl.cmd = 2` maps to a plain, side-effect-free direction (EAST) with
// no nested door-state block): the `if (!which_room)` block's
// `case 15399:`/`case 15398:` arms set `tmpwtl.cmd = 1`, and the
// immediately following `if (tmpwtl.cmd == 1)` dereferences the
// function-local `room_data* room = 0;` (never assigned on this path) --
// a pre-existing, out-of-scope latent null-deref this fixture must simply
// avoid triggering, not fix.
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
    // Site 8 (LS-2 whole-branch review B1): world[0..2].number/.light are
    // never reset by ScopedTestWorld's reuse branch -- saved here, restored
    // in the dtor below, so neither leaks into a later test sharing this
    // process's world[].
    int original_number[3] = { 0, 0, 0 };
    byte original_light[3] = { 0, 0, 0 };

    VampireKillerContext()
    {
        for (int room = 0; room < 3; ++room) {
            original_number[room] = world[room].number;
            original_light[room] = world[room].light;
        }

        world[0].number = 1; // host's own room -- NOT 15398/15399, see block comment above.
        world[1].number = 15398;
        world[2].number = 15399; // == top_of_world -- see the room_by_id() ban note above.
        world[0].light = 1;
        world[1].light = 1;
        world[2].light = 1;

        host.specials2.act = MOB_ISNPC;
        host.player.name = host_name;
        host.player.short_descr = host_name; // GET_NAME() reads short_descr for NPCs (IS_NPC(host)).
        // O-I3 fixture fix: char_data{}'s zero-initialized specials.position
        // defaults to POSITION_DEAD (0), and CAN_SEE()'s own
        // `GET_POS(sub) <= POSITION_SLEEPING` gate (visibility.cpp) returns 0
        // for EVERY occupant a "dead" host looks at -- masking the walk's
        // `!IS_NPC()` filter behind an unconditional CAN_SEE() failure
        // instead of exercising it. VampireHuntressContext already sets this
        // (POSITION_STANDING, required to even enter vampire_huntress's own
        // wander block); vampire_killer has no GET_POS(host) check of its
        // own, so this is a pure visibility-fixture fix with no effect on
        // which branch the function takes.
        host.specials.position = POSITION_STANDING;
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
        for (int room = 0; room < 3; ++room) {
            world[room].number = original_number[room];
            world[room].light = original_light[room];
        }

        world[0].people = nullptr;
        world[1].people = nullptr;
        world[2].people = nullptr;
        host.next_in_room = nullptr;
    }
};

// O-I3 positive-control infrastructure: the found branch (spec_pro.cpp
// :3343-3395) dispatches through issue_command(move, ...), not hit, so it
// needs its own recording hook rather than reusing
// ScopedRecordingSpecProHitHook -- same technique (a stub substituted for a
// combat_hooks.h dispatch cell via set_combat_command(), restored via
// register_combat_command_dispatch() on scope exit), different cell.
struct RecordedSpecProMoveCall {
    char_data* ch = nullptr;
    int cmd = 0;
    bool called = false;
};

RecordedSpecProMoveCall g_recorded_spec_pro_move_call;

void recording_spec_pro_move_stub(char_data* ch, char* /*argument*/, waiting_type* /*wtl*/,
    int cmd, int /*subcmd*/)
{
    g_recorded_spec_pro_move_call = RecordedSpecProMoveCall{ ch, cmd, true };
}

struct ScopedRecordingSpecProMoveHook {
    ScopedRecordingSpecProMoveHook()
    {
        rots::combat::set_combat_command(
            rots::combat::combat_command::move, recording_spec_pro_move_stub);
    }

    ~ScopedRecordingSpecProMoveHook() { register_combat_command_dispatch(); }

    ScopedRecordingSpecProMoveHook(const ScopedRecordingSpecProMoveHook&) = delete;
    ScopedRecordingSpecProMoveHook& operator=(const ScopedRecordingSpecProMoveHook&) = delete;
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

// O-I3 positive control #1: room 15399's bystander becomes a PC, so the
// :3298 walk (`room_by_id_total(real_room(15399))`) sets victim/which_room=1
// instead of leaving them at their Family-F pre-init. Room 15398 keeps its
// NPC bystander (which_room stays 1, not 3, so the `number(1, 2)`
// tie-break draw is never reached -- one fewer random value to stage).
TEST(SpecProVampireKiller, FindsAndDispatchesMoveWhenRoom15399HasAnEligibleVictim) {
    VampireKillerContext context;
    context.bystander_15399.specials2.act = 0; // PC now -- not excluded by !IS_NPC().

    // Host's own room must match one of the found branch's switch cases
    // (spec_pro.cpp:3348-3389) to reach a deterministic, defined direction
    // instead of the `default: tmpwtl.cmd = 0;` arm, whose
    // `CAN_GO(host, tmpwtl.cmd - 1)` would read dir_option[-1] (UB) --
    // 15395 -> tmpwtl.cmd = 2 (EAST; see room.h's NORTH=0/EAST=1/...).
    // VampireKillerContext's own dtor restores world[0].number from
    // original_number[0], captured before its constructor's own `= 1`
    // assignment, so this override is safe to leave unrestored here.
    world[0].number = 15395;

    // CAN_GO() needs a real, open exit at EAST or the found branch's
    // dispatch would be silently skipped (indistinguishable from "not
    // found" again) -- a stack room_direction_data, explicitly nulled
    // below before the context that owns world[0] is torn down (the O-I2
    // dangling-stack-exit-pointer discipline this branch's fixtures now
    // follow throughout).
    room_direction_data* const original_dir_option_east = world[0].dir_option[EAST];
    room_direction_data east_exit{};
    east_exit.exit_info = 0; // not closed/broken.
    east_exit.to_room = 0; // never actually taken -- the move hook below intercepts the
                            // dispatch before the real do_move() body would run.
    world[0].dir_option[EAST] = &east_exit;

    ScopedRecordingSpecProMoveHook hook;
    g_recorded_spec_pro_move_call = RecordedSpecProMoveCall{};

    clear_test_random_values();
    push_test_random_value(0.0); // number(0, 10) in the found branch ("slow him down a bit") must
                                  // be 0 or the function returns before reaching CAN_GO().

    const int result =
        vampire_killer(&context.host, &context.host, 0, mutable_arg(""), SPECIAL_SELF, nullptr);

    world[0].dir_option[EAST] = original_dir_option_east;
    clear_test_random_values();

    EXPECT_EQ(result, 0);
    EXPECT_TRUE(g_recorded_spec_pro_move_call.called)
        << "Expected the converted room_by_id_total(real_room(15399)) walk (:3298) to find the "
           "PC bystander, set which_room=1, and dispatch issue_command(move, ...) once the "
           "\"slow him down\" gate rolled 0.";
    EXPECT_EQ(g_recorded_spec_pro_move_call.ch, &context.host);
    EXPECT_EQ(g_recorded_spec_pro_move_call.cmd, 2);
}

// O-I3 positive control #2: the mirror image of the test above, pinning
// the OTHER fixed-room walk (:3307, `room_by_id_total(real_room(15398))`,
// `victim2`/which_room += 2). Room 15399 keeps its NPC bystander this time.
TEST(SpecProVampireKiller, FindsAndDispatchesMoveWhenRoom15398HasAnEligibleVictim) {
    VampireKillerContext context;
    context.bystander_15398.specials2.act = 0; // PC now -- not excluded by !IS_NPC().

    world[0].number = 15395; // Same reasoning as the room-15399 positive control above.

    room_direction_data* const original_dir_option_east = world[0].dir_option[EAST];
    room_direction_data east_exit{};
    east_exit.exit_info = 0;
    east_exit.to_room = 0;
    world[0].dir_option[EAST] = &east_exit;

    ScopedRecordingSpecProMoveHook hook;
    g_recorded_spec_pro_move_call = RecordedSpecProMoveCall{};

    clear_test_random_values();
    push_test_random_value(0.0); // number(0, 10) must be 0, same gate as above.

    const int result =
        vampire_killer(&context.host, &context.host, 0, mutable_arg(""), SPECIAL_SELF, nullptr);

    world[0].dir_option[EAST] = original_dir_option_east;
    clear_test_random_values();

    EXPECT_EQ(result, 0);
    EXPECT_TRUE(g_recorded_spec_pro_move_call.called)
        << "Expected the converted room_by_id_total(real_room(15398)) walk (:3307) to find the "
           "PC bystander, set which_room=2, and dispatch issue_command(move, ...) once the "
           "\"slow him down\" gate rolled 0.";
    EXPECT_EQ(g_recorded_spec_pro_move_call.ch, &context.host);
    EXPECT_EQ(g_recorded_spec_pro_move_call.cmd, 2);
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
// O-I3 (LS-2 whole-branch review, Opus): both assertions the test below
// makes are produced by the wander block's char_to_room() call, which runs
// BEFORE and INDEPENDENTLY of this walk -- so as written it passes with
// the walk deleted, pinning nothing about the `!IS_NPC()` filter its own
// name claims. Unlike vampire_killer's two fixed-room sites above, this
// one is confirmed GENUINELY INFEASIBLE to positive-control at proportional
// cost, re-deferred per the ruling's fallback (ls2-review-adjudication.md
// O-I3) rather than fixed -- both of the walk's two "found" sub-paths were
// checked directly against the source (spec_pro.cpp):
//   - tmpno == 2 calls the real hit(host, victim, TYPE_UNDEFINED) --
//     dead code as reached from here: hit()'s own first gate,
//     `if (GET_POS(ch) < POSITION_FIGHTING) return;` (fight.cpp:2560-2561),
//     always fires, because the day/night wander block this walk lives in
//     requires `GET_POS(host) != POSITION_FIGHTING` just to run
//     (spec_pro.cpp:2941) and nothing between there and the hit() call
//     raises host's position. The call executes and returns with ZERO
//     observable effect (no message, no state change) regardless of
//     whether victim was found -- there is no side effect to assert on.
//   - tmpno == 1 (the kidnap branch) genuinely has observable side
//     effects, but reaching them safely needs a THIRD room graph (the
//     victim's post-kidnap destination -- 15399 or 15398 depending on
//     victim->player.race -- must itself have a valid dir_option[0] whose
//     to_room has a valid dir_option[2], both SET_BIT()'d unconditionally
//     with no null guard) and ends in WAIT_STATE_FULL(host, ...) -- the
//     exact stack-char_data-into-global-waiting_list splice `2afaee9`
//     bisected out of an unrelated test, on a fixture built for THIS
//     follow-up, at a cost this coverage rider cannot justify.
// The destination room still gets an NPC bystander below (rather than
// being left empty), which is worth keeping: it does still prove the
// wander block's post-move room_of(host) resolver reaches a REAL
// destination room's occupant chain, even though it cannot additionally
// prove the `!IS_NPC()` filter without one of the two infeasible paths
// above. See the T4 report for the original scoping call.
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
