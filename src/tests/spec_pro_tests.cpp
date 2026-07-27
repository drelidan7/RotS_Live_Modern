#include "../spells.h"
#include "rots/core/character.h"
#include "rots/core/types.h"
#include "../utils.h"
#include "../interpre.h"
#include "../combat_hooks.h"
#include "rots/core/descriptor.h"
#include "rots/core/object.h"
#include "damage_test_context.h"
#include "test_placement.h"
#include "test_random_utils.h"
#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <optional>

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
        // pin host's location to NOWHERE to keep act() on its early "no
        // recipient" return instead of indexing an unallocated world[].
        set_location(&host, NOWHERE);
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
    // host at the head, its ambush target behind it -- the head-first chain
    // this fixture used to publish by hand, and the order the converted
    // occupants(room_of(host)) walk visits them in (LS-3a T3,
    // test_placement.h). Declared LAST so it unwinds before the characters it
    // manages and before the ScopedTestWorld whose room it points into.
    ScopedRoomOccupants occupants{&test_world.room(), 0, {&host, &occupant}};

    MobRangerContext()
    {
        room_by_id_total(0)->light = 1; // Unlit rooms fail CAN_SEE's darkness check.
        host.specials2.act = MOB_ISNPC;
        host.player.name = host_name;
        // POSITION_FIGHTING skips the `GET_POS(host) < POSITION_FIGHTING ->
        // update_pos(host)` call at function entry and, being neither
        // POSITION_STANDING, also keeps the do_hide() call and the final
        // wander-movement block (both well past the ambush check) out of
        // this rider's reach.
        host.specials.position = POSITION_FIGHTING;

        occupant.player.name = occupant_name;
    }

    // The head reset and the unlink this destructor used to perform are now
    // `occupants`, which unwinds immediately after it -- and it restores the
    // SAVED head rather than hard-nulling, which ScopedTestWorld's reset makes
    // byte-identical here.
    ~MobRangerContext() = default;
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
    // Site 8 (LS-2 whole-branch review B1): world[0].light is saved here and
    // restored in the dtor below so it doesn't leak into a later test sharing
    // this process's world[0]. LS-3a T1 Stage A made ScopedTestWorld's reuse
    // branch reset .light at construction, so this now also serves to undo the
    // value this fixture forces on.
    byte original_light = 0;
    // host at the head, its mark behind it -- as in MobRangerContext above.
    ScopedRoomOccupants occupants{&test_world.room(), 0, {&host, &occupant}};

    MobRangerNewContext()
    {
        original_light = room_by_id_total(0)->light;
        room_by_id_total(0)->light = 1;
        // MOB_SENTINEL keeps the unrelated final wander-movement block
        // (:2230, past every path this rider drives) out of reach
        // regardless of position.
        host.specials2.act = MOB_ISNPC | MOB_SENTINEL;
        host.player.name = host_name;
        host.specials.position = POSITION_FIGHTING;
        host.abilities.hit = 500;
        host.tmpabilities.hit = 500; // well above wimpy_health_limit (max/5) -- is_wimpy stays 0.

        occupant.player.name = occupant_name;
    }

    // The light restore is not location state and stays here; the head reset
    // and the unlink are `occupants`, which unwinds immediately after this
    // body -- same order as before.
    ~MobRangerNewContext()
    {
        room_by_id_total(0)->light = original_light;
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
    // saved here and restored in the dtor below so neither leaks into a later
    // test sharing this process's world[]. LS-3a T1 Stage A made
    // ScopedTestWorld's reuse branch reset both fields at construction, so
    // these restores now undo this fixture's own writes rather than standing
    // as the only cross-test guard.
    int original_number[3] = { 0, 0, 0 };
    byte original_light[3] = { 0, 0, 0 };
    // One helper per room (LS-3a T3 idiom rule 10, test_placement.h), in the
    // order this fixture used to publish them by hand: room 0 gets
    // host -> bystander_home (host-first, the order vampire_killer's self-room
    // walk sees), and the two fixed rooms each get their single bystander.
    // Declared LAST so they unwind before the characters they manage and
    // before the ScopedTestWorld whose rooms they point into; the destructor
    // body's number/light restores still run first, exactly as before.
    ScopedRoomOccupants room0_occupants{&test_world.room(), 0, {&host, &bystander_home}};
    ScopedRoomOccupants room2_occupants{room_by_id_total(2), 2, {&bystander_15399}};
    ScopedRoomOccupants room1_occupants{room_by_id_total(1), 1, {&bystander_15398}};

    VampireKillerContext()
    {
        for (int room = 0; room < 3; ++room) {
            original_number[room] = room_by_id_total(room)->number;
            original_light[room] = room_by_id_total(room)->light;
        }

        room_by_id_total(0)->number = 1; // host's own room -- NOT 15398/15399, see block comment above.
        room_by_id_total(1)->number = 15398;
        room_by_id_total(2)->number = 15399; // == top_of_world -- see the room_by_id() ban note above.
        room_by_id_total(0)->light = 1;
        room_by_id_total(1)->light = 1;
        room_by_id_total(2)->light = 1;

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

        bystander_home.specials2.act = MOB_ISNPC; // excluded by the walk's `!IS_NPC()` filter.
        bystander_home.player.name = bystander_home_name;
        bystander_home.player.short_descr = bystander_home_name;

        bystander_15399.specials2.act = MOB_ISNPC;
        bystander_15399.player.name = bystander_15399_name;
        bystander_15399.player.short_descr = bystander_15399_name;

        bystander_15398.specials2.act = MOB_ISNPC;
        bystander_15398.player.name = bystander_15398_name;
        bystander_15398.player.short_descr = bystander_15398_name;
    }

    ~VampireKillerContext()
    {
        for (int room = 0; room < 3; ++room) {
            room_by_id_total(room)->number = original_number[room];
            room_by_id_total(room)->light = original_light[room];
        }
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
    EXPECT_EQ(location_of(&context.host), 0)
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
    room_by_id_total(0)->number = 15395;

    // CAN_GO() needs a real, open exit at EAST or the found branch's
    // dispatch would be silently skipped (indistinguishable from "not
    // found" again) -- a stack room_direction_data, explicitly nulled
    // below before the context that owns world[0] is torn down (the O-I2
    // dangling-stack-exit-pointer discipline this branch's fixtures now
    // follow throughout).
    room_direction_data* const original_dir_option_east = room_by_id_total(0)->dir_option[EAST];
    room_direction_data east_exit{};
    east_exit.exit_info = 0; // not closed/broken.
    east_exit.to_room = 0; // never actually taken -- the move hook below intercepts the
                            // dispatch before the real do_move() body would run.
    room_by_id_total(0)->dir_option[EAST] = &east_exit;

    ScopedRecordingSpecProMoveHook hook;
    g_recorded_spec_pro_move_call = RecordedSpecProMoveCall{};

    clear_test_random_values();
    push_test_random_value(0.0); // number(0, 10) in the found branch ("slow him down a bit") must
                                  // be 0 or the function returns before reaching CAN_GO().

    const int result =
        vampire_killer(&context.host, &context.host, 0, mutable_arg(""), SPECIAL_SELF, nullptr);

    room_by_id_total(0)->dir_option[EAST] = original_dir_option_east;
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

    room_by_id_total(0)->number = 15395; // Same reasoning as the room-15399 positive control above.

    room_direction_data* const original_dir_option_east = room_by_id_total(0)->dir_option[EAST];
    room_direction_data east_exit{};
    east_exit.exit_info = 0;
    east_exit.to_room = 0;
    room_by_id_total(0)->dir_option[EAST] = &east_exit;

    ScopedRecordingSpecProMoveHook hook;
    g_recorded_spec_pro_move_call = RecordedSpecProMoveCall{};

    clear_test_random_values();
    push_test_random_value(0.0); // number(0, 10) must be 0, same gate as above.

    const int result =
        vampire_killer(&context.host, &context.host, 0, mutable_arg(""), SPECIAL_SELF, nullptr);

    room_by_id_total(0)->dir_option[EAST] = original_dir_option_east;
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
    // One helper per room (idiom rule 10): host alone in its starting room,
    // the NPC bystander alone in the destination the wander block moves host
    // into. The destination's helper is declared LAST so it unwinds FIRST,
    // putting room 1's head back before room 0's helper takes host -- whom
    // char_to_room() re-seated into room 1's chain mid-test -- back out. It
    // also unlinks the bystander, which the hand-rolled teardown left pointing
    // at host, and de-locates host to NOWHERE, which it never did.
    ScopedRoomOccupants room0_occupants{&test_world.room(), 0, {&host}};
    ScopedRoomOccupants room1_occupants{room_by_id_total(1), 1, {&bystander}};

    VampireHuntressContext()
    {
        saved_sunlight = weather_info.sunlight;
        weather_info.sunlight = SUN_RISE;

        room_by_id_total(0)->number = 1; // host's starting room -- arbitrary (SUN_RISE overrides to_room).
        room_by_id_total(1)->number = 15379; // real_room(15379) destination -- == top_of_world.
        room_by_id_total(0)->light = 1;
        room_by_id_total(1)->light = 1;

        host.specials2.act = MOB_ISNPC;
        host.player.name = host_name;
        host.player.short_descr = host_name; // GET_NAME() reads short_descr for NPCs (IS_NPC(host)).
        // Must NOT be POSITION_FIGHTING (required to enter the day/night
        // wander block at all: `GET_POS(host) != POSITION_FIGHTING`).
        host.specials.position = POSITION_STANDING;

        bystander.specials2.act = MOB_ISNPC; // excluded by the walk's `!IS_NPC()` filter.
        bystander.player.name = bystander_name;
        bystander.player.short_descr = bystander_name;
    }

    // The weather restore is not location state and stays here; both rooms'
    // heads and the unlinks are the two ScopedRoomOccupants members, which
    // unwind immediately after this body.
    ~VampireHuntressContext()
    {
        weather_info.sunlight = saved_sunlight;
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
    EXPECT_EQ(location_of(&context.host), 1)
        << "Expected the day/night wander block's real_room(15379)/char_to_room() call to have "
           "moved the host into the destination room before the converted occupants(room_of("
           "host)) walk ran.";
    clear_test_random_values();
}

// ---------------------------------------------------------------------------
// SPECIAL(ferry_captain) -- LS-3a T2 tranche 2c CHARACTERIZATION (ruling R-A5,
// .superpowers/sdd/ls3a-global-constraints.md; census A sections 3.1/5.2/6.4).
//
// ferry_captain() owns the ONLY manual bulk occupant-chain splice left in
// production (the `if (new_room != old_room)` block, spec_pro.cpp) and had ZERO
// test coverage anywhere in the tree. R-A5 requires that behavior pinned BEFORE
// any conversion touches it: these tests land first and are then re-run
// UNCHANGED against (a) the eight routed cursor writes and (b) the
// relocate_all_occupants()/relocate_all_contents() encapsulation, as the proof
// that neither changed behavior.
//
// THESE TESTS DRIVE THE REAL SPECIAL, not a replayed statement sequence: every
// precondition on the path to the splice is satisfiable from a fixture (see
// FerryCaptainContext below), so nothing about the block's reachability is
// assumed. What the fixture deliberately does NOT cover, because none of the
// three reaches the splice:
//   * the `old_room != location_of(host)` re-seat arm (dispatch_char_from_room
//     + char_to_room + act) -- the fixture starts the captain already in the
//     current cabin, which is the steady state on every tick after the first;
//   * the object_list fallback scan for a ferry object that is in no room;
//   * the "I wonder where my ship is?" no-ferry-found arm.
//
// THE PINNED CONTRACT -- what the splice does today, and what commit 3's
// relocate_all_occupants()/relocate_all_contents() must reproduce exactly:
//   1. Combined chain ORDER is source-then-destination: the source room's
//      occupants keep their relative order and are followed by whatever the
//      destination room already held.
//   2. The combined chain is published at the destination room; the source
//      room's chain head is nulled.
//   3. EVERY member of the combined chain -- including the destination's
//      pre-existing occupants, for whom it is a no-op -- has its location
//      re-stamped to the destination room.
//   4. Objects follow the identical rule through room_data::contents /
//      obj_data::next_content / obj_data::in_room.
//   5. An EMPTY source room degrades to "destination chain untouched, source
//      stays empty" (the `if (tmpch)` / `if (tmpobj)` ELSE arms), and a
//      character whose location field says the source room but which is not
//      LINKED into its chain is not relocated at all.
//   6. NO light bookkeeping and NO zone white/dark-power bookkeeping happens --
//      the splice is deliberately not char_to_room()/detach_char_from_room().
//   7. The captain rides along: it is an occupant of the source cabin, so the
//      same splice relocates it, and the trailing cursor block's restore leaves
//      its location at the DESTINATION cabin rather than at a ferry dock.
//
// THE CURSOR IDIOM is pinned by observation, not by inspection (ruling R-T2-1:
// riders must assert cursor-DEPENDENT behavior). ferry_captain() renders four
// pairs of messages, one "inside" the cabin and one "outside" at the ferry
// object's dock; the outside half only reaches the dock because the captain's
// location field is temporarily pointed at the ferry between the act() call and
// its restore. This fixture puts a capturing descriptor on a cabin occupant AND
// on an occupant of EACH dock, and asserts every message landed in exactly the
// room the cursor aimed it at -- including the fact that the two LEADING pairs
// render at the OLD dock while the two TRAILING ones render at the NEW dock,
// because obj_from_room()/obj_to_room() move the ferry in between. A
// neutralized cursor write sends an "outside" text to the cabin instead; a
// neutralized restore strands the captain at a dock (contract item 7).
// ---------------------------------------------------------------------------

// ferry_captain()'s route/message table (consts.cpp). Whole-struct saved and
// restored per test, so the process-global entry the real game boots with is
// never left mutated for a later test in the monolithic runner.
extern int num_of_captains;
extern ferry_captain_type ferry_captain_data[];

// SPECIAL(ferry_captain) -- not declared in any header (same SPECIAL()-family
// reasoning as dragon/healing_plant/vampire_killer above).
extern int ferry_captain(char_data* host, char_data* ch, int cmd, char* arg, int callflag,
    waiting_type* wtl);

namespace {

// Four real world[] rooms: the cabin the captain and passengers start in, the
// cabin they end in, and the two docks the ferry object itself occupies before
// and after the move. The cabins are the splice's source/destination; the docks
// exist so the cursor blocks have DIFFERENT rooms to render into, which is what
// makes the cursor observable at all.
constexpr int kFerrySourceCabin = 0;
constexpr int kFerryDestinationCabin = 1;
constexpr int kFerrySourceDock = 2;
constexpr int kFerryDestinationDock = 3;

// obj_data::item_number the captain matches its ferry object on
// (ferry_captain_type::num_of_ferry). Arbitrary, but distinct from the
// zero-initialized item_number of every other object in the fixture, so the
// lookup cannot succeed by accident.
constexpr int kFerryItemNumber = 4242;

// Deliberately distinct, nonzero starting light levels: contract item 6 pins
// that the splice performs NO light bookkeeping, which is only observable
// against a value a recount would move.
constexpr int kSourceCabinLight = 3;
constexpr int kDestinationCabinLight = 5;

// The eight message slots, one recognizable literal each, with NO act() code
// ($n/$o) in any of them: convert_string() copies such text through verbatim,
// so every buffer assertion below is a plain substring test and none of them
// depends on PERS()/CAN_SEE() rendering. "-OUT" texts are the ones a cursor
// block renders at the ferry's dock; "-IN" texts render in the cabin. No
// literal here is a substring of any other.
constexpr const char* kLeaveOutside = "FERRY-LEAVE-OUT";
constexpr const char* kLeaveInside = "FERRY-LEAVE-IN";
constexpr const char* kArriveOutside = "FERRY-ARRIVE-OUT";
constexpr const char* kArriveInside = "FERRY-ARRIVE-IN";
constexpr const char* kMoveOutOutside = "FERRY-MOVEOUT-OUT";
constexpr const char* kMoveOutInside = "FERRY-MOVEOUT-IN";
constexpr const char* kMoveInOutside = "FERRY-MOVEIN-OUT";
constexpr const char* kMoveInInside = "FERRY-MOVEIN-IN";

// Which occupants each cabin starts with. The captain is ALWAYS located in the
// source cabin (ferry_captain() re-seats it there otherwise), but kEmptySource
// leaves it out of that room's occupant CHAIN -- the only way to reach the
// splice's `if (tmpch)`/`if (tmpobj)` else arms, which are otherwise
// unreachable through this SPECIAL precisely because the captain is normally
// linked in. Contract item 5 is pinned from that deliberately constructed
// state; commit 3's API tests exercise the same arms directly.
enum class FerryCabinLayout {
    kLoadedSourceAndDestination,
    kLoadedSourceEmptyDestination,
    kEmptySourceLoadedDestination,
};

// Points a descriptor's output at its OWN small_outbuf so act() output can be
// read back. Mirrors act_wiz_format_tests.cpp/act_format_tests.cpp's helper of
// the same name, INCLUDING its critical in-place-mutation contract:
// descriptor_data::output is a self-pointer into the same object's
// small_outbuf[], so a version returning descriptor_data by value would leave
// `output` aimed at a destroyed temporary's buffer.
void reset_capturing_descriptor(descriptor_data& descriptor, char_data* character)
{
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = 0; // CON_PLYNG
    descriptor.character = character;
}

// Copies one message literal into a ferry_captain_type's fixed char[255] slot.
void set_ferry_message(char (&slot)[255], const char* text)
{
    std::snprintf(slot, sizeof(slot), "%s", text);
}

bool buffer_contains(const descriptor_data& descriptor, const char* needle)
{
    return std::strstr(descriptor.small_outbuf, needle) != nullptr;
}

// Everything ferry_captain() reads on its way to the splice, wired so the
// function reaches that block on a single call with cmd == 0:
//   * the captain is STANDING (both position gates) and already located in
//     cabin_route[marker], so the re-seat arm is skipped;
//   * timer == 0 and stop_time[marker] > 0, so the leave_to_* cursor pair runs;
//   * the ferry object is findable in ferry_route[marker]'s contents, so both
//     the object_list fallback and the "where is my ship" arm are skipped;
//   * length == 2, so marker advances 0 -> 1 and source/destination differ.
//
// Occupant chains are published through ScopedRoomOccupants (test_placement.h,
// LS-3a T1): head-first in the order given, set_location()-stamped, and fully
// unlinked on teardown -- the fixture-hygiene rule, which matters especially
// here because the splice relinks process-global rooms behind the fixture's
// back.
struct FerryCaptainContext {
    ScopedTestWorld test_world { 4 };

    // char_to_room()/detach_char_from_room() dereference zone_by_id(r->zone)
    // unconditionally for a non-NPC character. Nothing on this fixture's path
    // calls either -- which is exactly contract item 6's point -- so this table
    // is both standing insurance and the instrument that makes "no zone
    // bookkeeping" measurable: its counters start at zero and must still be
    // zero afterwards.
    ScopedZoneTableOwner zone_table_owner;

    char_data captain {};
    char_data passenger {}; // carries the cabin-side capturing descriptor.
    char_data stowaway {};
    char_data resident {}; // already in the DESTINATION cabin before the move.
    char_data source_dock_watcher {};
    char_data destination_dock_watcher {};

    descriptor_data cabin_descriptor {};
    descriptor_data source_dock_descriptor {};
    descriptor_data destination_dock_descriptor {};

    obj_data ferry {};
    obj_data cargo_first {};
    obj_data cargo_second {};
    obj_data crate {}; // already in the DESTINATION cabin before the move.

    char captain_name[16] = "test_captain";
    char passenger_name[16] = "test_rider";
    char stowaway_name[16] = "test_stowaway";
    char resident_name[16] = "test_resident";
    char source_watcher_name[16] = "test_dockhand";
    char destination_watcher_name[16] = "test_porter";

    // The process-global route/message entry, restored verbatim on teardown.
    ferry_captain_type saved_captain_data {};

    // Emplaced in the constructor BODY rather than the member-initializer list:
    // the occupant set varies by layout, and an initializer_list returned from
    // a helper would dangle (its backing array does not outlive the
    // full-expression). ScopedRoomOccupants copies the list into its own vector,
    // so emplacing from a literal here is safe.
    std::optional<ScopedRoomOccupants> source_cabin;
    std::optional<ScopedRoomOccupants> destination_cabin;
    std::optional<ScopedRoomOccupants> source_dock;
    std::optional<ScopedRoomOccupants> destination_dock;

    // destination_stop_time selects which trailing message arm runs: 0 takes
    // the move_in_* arm (timer == 0), nonzero takes the arrive_to_* arm. Both
    // arms carry their own cursor pair, so each is worth pinning.
    explicit FerryCaptainContext(
        FerryCabinLayout layout = FerryCabinLayout::kLoadedSourceAndDestination,
        int destination_stop_time = 0)
    {
        room_by_id_total(kFerrySourceCabin)->light = kSourceCabinLight;
        room_by_id_total(kFerryDestinationCabin)->light = kDestinationCabinLight;

        captain.specials2.act = MOB_ISNPC;
        captain.player.name = captain_name;
        captain.player.short_descr = captain_name;
        captain.specials.position = POSITION_STANDING; // both position gates.
        captain.specials.store_prog_number = 0; // ferry_captain_data[0].

        // Non-NPC with a RACE_GOOD race (GET_RACE in [1, 9], utils.h):
        // char_to_room()'s zone white_power block only runs for a non-NPC of a
        // good or evil race, so these are what WOULD move
        // zone_table[0].white_power if the splice ever grew the bookkeeping
        // contract item 6 pins it as not having.
        passenger.player.name = passenger_name;
        passenger.player.race = 1;
        passenger.specials.position = POSITION_STANDING; // AWAKE(), act() gate.
        reset_capturing_descriptor(cabin_descriptor, &passenger);
        passenger.desc = &cabin_descriptor;

        stowaway.player.name = stowaway_name;
        stowaway.player.race = 1;
        stowaway.specials.position = POSITION_STANDING;

        resident.specials2.act = MOB_ISNPC;
        resident.player.name = resident_name;
        resident.player.short_descr = resident_name;
        resident.specials.position = POSITION_STANDING;

        source_dock_watcher.player.name = source_watcher_name;
        source_dock_watcher.player.race = 1;
        source_dock_watcher.specials.position = POSITION_STANDING;
        reset_capturing_descriptor(source_dock_descriptor, &source_dock_watcher);
        source_dock_watcher.desc = &source_dock_descriptor;

        destination_dock_watcher.player.name = destination_watcher_name;
        destination_dock_watcher.player.race = 1;
        destination_dock_watcher.specials.position = POSITION_STANDING;
        reset_capturing_descriptor(destination_dock_descriptor, &destination_dock_watcher);
        destination_dock_watcher.desc = &destination_dock_descriptor;

        switch (layout) {
        case FerryCabinLayout::kLoadedSourceAndDestination:
            source_cabin.emplace(room_by_id_total(kFerrySourceCabin), kFerrySourceCabin,
                std::initializer_list<char_data*> { &captain, &passenger, &stowaway });
            destination_cabin.emplace(room_by_id_total(kFerryDestinationCabin), kFerryDestinationCabin,
                std::initializer_list<char_data*> { &resident });
            break;
        case FerryCabinLayout::kLoadedSourceEmptyDestination:
            source_cabin.emplace(room_by_id_total(kFerrySourceCabin), kFerrySourceCabin,
                std::initializer_list<char_data*> { &captain, &passenger, &stowaway });
            destination_cabin.emplace(room_by_id_total(kFerryDestinationCabin), kFerryDestinationCabin,
                std::initializer_list<char_data*> {});
            break;
        case FerryCabinLayout::kEmptySourceLoadedDestination:
            source_cabin.emplace(room_by_id_total(kFerrySourceCabin), kFerrySourceCabin,
                std::initializer_list<char_data*> {});
            destination_cabin.emplace(room_by_id_total(kFerryDestinationCabin), kFerryDestinationCabin,
                std::initializer_list<char_data*> { &resident });
            // Located in the source cabin (ferry_captain() would otherwise
            // re-seat it there through char_to_room()) but deliberately NOT
            // linked into its chain -- see FerryCabinLayout's own comment.
            set_location(&captain, kFerrySourceCabin);
            break;
        }

        source_dock.emplace(room_by_id_total(kFerrySourceDock), kFerrySourceDock,
            std::initializer_list<char_data*> { &source_dock_watcher });
        destination_dock.emplace(room_by_id_total(kFerryDestinationDock), kFerryDestinationDock,
            std::initializer_list<char_data*> { &destination_dock_watcher });

        ferry.item_number = kFerryItemNumber;
        ferry.in_room = kFerrySourceDock;  // LS1-ALLOW: obj-location
        room_by_id_total(kFerrySourceDock)->contents = &ferry;

        saved_captain_data = ferry_captain_data[0];

        ferry_captain_type& route = ferry_captain_data[0];
        route = ferry_captain_type {};
        route.num_of_ferry = kFerryItemNumber;
        route.length = 2;
        route.marker = 0;
        route.timer = 0;
        route.ferry_route[0] = kFerrySourceDock;
        route.ferry_route[1] = kFerryDestinationDock;
        route.cabin_route[0] = kFerrySourceCabin;
        route.cabin_route[1] = kFerryDestinationCabin;
        route.stop_time[0] = 1; // > 0 -> the leave_to_* cursor pair runs.
        route.stop_time[1] = destination_stop_time;
        set_ferry_message(route.leave_to_outside, kLeaveOutside);
        set_ferry_message(route.leave_to_inside, kLeaveInside);
        set_ferry_message(route.arrive_to_outside, kArriveOutside);
        set_ferry_message(route.arrive_to_inside, kArriveInside);
        set_ferry_message(route.move_out_outside, kMoveOutOutside);
        set_ferry_message(route.move_out_inside, kMoveOutInside);
        set_ferry_message(route.move_in_outside, kMoveInOutside);
        set_ferry_message(route.move_in_inside, kMoveInInside);
    }

    ~FerryCaptainContext()
    {
        ferry_captain_data[0] = saved_captain_data;

        // The object chains have no ScopedRoomOccupants equivalent (the Stage-1
        // API is char-side only), so unlink them by hand: every one of these is
        // a stack object the fixture linked into a process-global room.
        for (int room = kFerrySourceCabin; room <= kFerryDestinationDock; ++room) {
            room_by_id_total(room)->contents = nullptr;
        }
        ferry.next_content = nullptr;
        cargo_first.next_content = nullptr;
        cargo_second.next_content = nullptr;
        crate.next_content = nullptr;

        // kEmptySourceLoadedDestination stamps this by hand above, so no
        // ScopedRoomOccupants takes it back out.
        set_location(&captain, NOWHERE);
    }

    FerryCaptainContext(const FerryCaptainContext&) = delete;
    FerryCaptainContext& operator=(const FerryCaptainContext&) = delete;

    // Links cargo_first -> cargo_second as the source cabin's contents. Called
    // from a test body rather than the constructor so the empty-source case can
    // skip it entirely.
    void load_source_cabin_contents()
    {
        cargo_first.in_room = kFerrySourceCabin;  // LS1-ALLOW: obj-location
        cargo_first.next_content = &cargo_second;
        cargo_second.in_room = kFerrySourceCabin;  // LS1-ALLOW: obj-location
        cargo_second.next_content = nullptr;
        room_by_id_total(kFerrySourceCabin)->contents = &cargo_first;
    }

    void load_destination_cabin_contents()
    {
        crate.in_room = kFerryDestinationCabin;  // LS1-ALLOW: obj-location
        crate.next_content = nullptr;
        room_by_id_total(kFerryDestinationCabin)->contents = &crate;
    }

    // One tick of the captain's route, on the SPECIAL()'s real signature.
    int tick()
    {
        return ferry_captain(&captain, nullptr, 0, mutable_arg(""), SPECIAL_SELF, nullptr);
    }
};

} // namespace

TEST(SpecProFerryCaptain, SplicesTheSourceCabinChainAheadOfTheDestinationsAndRestampsEveryMember) {
    FerryCaptainContext context;
    context.load_source_cabin_contents();
    context.load_destination_cabin_contents();

    EXPECT_EQ(context.tick(), TRUE);

    // Contract items 1/2: source-then-destination order, published at the
    // destination, source head nulled.
    EXPECT_EQ(rots::entity::first_occupant(room_by_id_total(kFerryDestinationCabin)), &context.captain);
    EXPECT_EQ(context.captain.next_in_room, &context.passenger); // LS1-ALLOW: representation-impl (occupant-chain SHAPE assertion -- pins the raw next_in_room links themselves, the one property no Stage-1 API expresses and the exact property LS-3b rewrites)
    EXPECT_EQ(context.passenger.next_in_room, &context.stowaway); // LS1-ALLOW: representation-impl (occupant-chain SHAPE assertion -- pins the raw next_in_room links themselves, the one property no Stage-1 API expresses and the exact property LS-3b rewrites)
    EXPECT_EQ(context.stowaway.next_in_room, &context.resident) // LS1-ALLOW: representation-impl (occupant-chain SHAPE assertion -- pins the raw next_in_room links themselves, the one property no Stage-1 API expresses and the exact property LS-3b rewrites)
        << "The destination's pre-existing occupants are appended AFTER the arriving chain, not "
           "before it -- the splice walks the source chain to its tail and links the destination's "
           "old head onto it.";
    EXPECT_EQ(context.resident.next_in_room, nullptr); // LS1-ALLOW: representation-impl (occupant-chain SHAPE assertion -- pins the raw next_in_room links themselves, the one property no Stage-1 API expresses and the exact property LS-3b rewrites)
    EXPECT_EQ(rots::entity::first_occupant(room_by_id_total(kFerrySourceCabin)), nullptr);

    // Contract items 3/7: every member re-stamped, including the destination's
    // own resident (a no-op for it) and the captain (whose trailing cursor
    // block restores it here rather than to a dock).
    EXPECT_EQ(location_of(&context.captain), kFerryDestinationCabin);
    EXPECT_EQ(location_of(&context.passenger), kFerryDestinationCabin);
    EXPECT_EQ(location_of(&context.stowaway), kFerryDestinationCabin);
    EXPECT_EQ(location_of(&context.resident), kFerryDestinationCabin);

    // Contract item 4: the object half follows the identical rule.
    EXPECT_EQ(room_by_id_total(kFerryDestinationCabin)->contents, &context.cargo_first);
    EXPECT_EQ(context.cargo_first.next_content, &context.cargo_second);
    EXPECT_EQ(context.cargo_second.next_content, &context.crate);
    EXPECT_EQ(context.crate.next_content, nullptr);
    EXPECT_EQ(room_by_id_total(kFerrySourceCabin)->contents, nullptr);
    EXPECT_EQ(context.cargo_first.in_room, kFerryDestinationCabin);  // LS1-ALLOW: obj-location
    EXPECT_EQ(context.cargo_second.in_room, kFerryDestinationCabin);  // LS1-ALLOW: obj-location
    EXPECT_EQ(context.crate.in_room, kFerryDestinationCabin);  // LS1-ALLOW: obj-location
}

TEST(SpecProFerryCaptain, MovesTheFerryObjectToTheNextDockAndAdvancesTheRouteMarker) {
    FerryCaptainContext context;

    EXPECT_EQ(context.tick(), TRUE);

    EXPECT_EQ(context.ferry.in_room, kFerryDestinationDock);  // LS1-ALLOW: obj-location
    EXPECT_EQ(room_by_id_total(kFerryDestinationDock)->contents, &context.ferry);
    EXPECT_EQ(room_by_id_total(kFerrySourceDock)->contents, nullptr);
    EXPECT_EQ(ferry_captain_data[0].marker, 1);
    EXPECT_EQ(ferry_captain_data[0].timer, 0)
        << "timer is reloaded from stop_time[] at the NEW marker, which this fixture leaves at 0.";
}

TEST(SpecProFerryCaptain, PerformsNoLightOrZonePowerBookkeepingWhileRelocatingTheCabin) {
    FerryCaptainContext context;
    context.load_source_cabin_contents();
    context.load_destination_cabin_contents();

    EXPECT_EQ(context.tick(), TRUE);

    // Contract item 6. char_to_room()/detach_char_from_room() would have moved
    // both of these; the manual splice deliberately moves neither, which is the
    // single most load-bearing difference between it and the real primitives.
    EXPECT_EQ(room_by_id_total(kFerrySourceCabin)->light, kSourceCabinLight);
    EXPECT_EQ(room_by_id_total(kFerryDestinationCabin)->light, kDestinationCabinLight);
    EXPECT_EQ(context.zone_table_owner.zone().white_power, 0)
        << "Two of the relocated occupants are non-NPC RACE_GOOD characters, so char_to_room() "
           "would have credited their char_power() to the destination zone.";
    EXPECT_EQ(context.zone_table_owner.zone().dark_power, 0);
}

TEST(SpecProFerryCaptain, RendersEachMessagePairsOutsideHalfAtTheFerrysDockAndInsideHalfInTheCabin) {
    FerryCaptainContext context;

    EXPECT_EQ(context.tick(), TRUE);

    // The two LEADING pairs render while the ferry is still at the source dock.
    EXPECT_TRUE(buffer_contains(context.source_dock_descriptor, kLeaveOutside))
        << "leave_to_outside only reaches the dock because the cursor write points the captain's "
           "location at the ferry object for the duration of that act() call.";
    EXPECT_TRUE(buffer_contains(context.source_dock_descriptor, kMoveOutOutside));

    // The two TRAILING ones render after obj_from_room()/obj_to_room() have
    // moved the ferry, so they land at the DESTINATION dock instead.
    EXPECT_FALSE(buffer_contains(context.source_dock_descriptor, kMoveInOutside));
    EXPECT_TRUE(buffer_contains(context.destination_dock_descriptor, kMoveInOutside));
    EXPECT_FALSE(buffer_contains(context.destination_dock_descriptor, kLeaveOutside));
    EXPECT_FALSE(buffer_contains(context.destination_dock_descriptor, kMoveOutOutside));

    // The cabin sees only the "inside" half of each pair -- the cursor restore
    // is what keeps the outside texts out of this buffer.
    EXPECT_TRUE(buffer_contains(context.cabin_descriptor, kLeaveInside));
    EXPECT_TRUE(buffer_contains(context.cabin_descriptor, kMoveOutInside));
    EXPECT_FALSE(buffer_contains(context.cabin_descriptor, kLeaveOutside));
    EXPECT_FALSE(buffer_contains(context.cabin_descriptor, kMoveOutOutside));
    EXPECT_FALSE(buffer_contains(context.cabin_descriptor, kMoveInOutside));
    EXPECT_FALSE(buffer_contains(context.source_dock_descriptor, kLeaveInside));
    EXPECT_FALSE(buffer_contains(context.source_dock_descriptor, kMoveOutInside));
}

TEST(SpecProFerryCaptain, TakesTheArriveArmAndStillRestoresTheCaptainToTheDestinationCabin) {
    // stop_time[1] != 0 leaves timer nonzero after the reload, selecting the
    // arrive_to_* arm instead of the move_in_* one. Both arms carry a cursor
    // pair; this pins the other one.
    FerryCaptainContext context(FerryCabinLayout::kLoadedSourceAndDestination, 5);

    EXPECT_EQ(context.tick(), TRUE);

    EXPECT_EQ(ferry_captain_data[0].timer, 5);
    EXPECT_TRUE(buffer_contains(context.destination_dock_descriptor, kArriveOutside));
    EXPECT_FALSE(buffer_contains(context.destination_dock_descriptor, kMoveInOutside));
    EXPECT_TRUE(buffer_contains(context.cabin_descriptor, kArriveInside));
    EXPECT_FALSE(buffer_contains(context.cabin_descriptor, kArriveOutside));
    EXPECT_EQ(location_of(&context.captain), kFerryDestinationCabin)
        << "Contract item 7: the arrive arm's trailing cursor restore must put the captain back "
           "in the cabin the splice moved it to, not leave it at the ferry's dock.";
}

TEST(SpecProFerryCaptain, MovesTheWholeSourceChainIntoAnEmptyDestinationCabinAndTerminatesIt) {
    FerryCaptainContext context(FerryCabinLayout::kLoadedSourceEmptyDestination);
    context.load_source_cabin_contents();

    EXPECT_EQ(context.tick(), TRUE);

    EXPECT_EQ(rots::entity::first_occupant(room_by_id_total(kFerryDestinationCabin)), &context.captain);
    EXPECT_EQ(context.captain.next_in_room, &context.passenger); // LS1-ALLOW: representation-impl (occupant-chain SHAPE assertion -- pins the raw next_in_room links themselves, the one property no Stage-1 API expresses and the exact property LS-3b rewrites)
    EXPECT_EQ(context.passenger.next_in_room, &context.stowaway); // LS1-ALLOW: representation-impl (occupant-chain SHAPE assertion -- pins the raw next_in_room links themselves, the one property no Stage-1 API expresses and the exact property LS-3b rewrites)
    EXPECT_EQ(context.stowaway.next_in_room, nullptr) // LS1-ALLOW: representation-impl (occupant-chain SHAPE assertion -- pins the raw next_in_room links themselves, the one property no Stage-1 API expresses and the exact property LS-3b rewrites)
        << "With nothing already in the destination, the arriving chain's tail keeps its null "
           "terminator rather than being linked onto a pre-existing head.";
    EXPECT_EQ(rots::entity::first_occupant(room_by_id_total(kFerrySourceCabin)), nullptr);
    EXPECT_EQ(location_of(&context.stowaway), kFerryDestinationCabin);

    EXPECT_EQ(room_by_id_total(kFerryDestinationCabin)->contents, &context.cargo_first);
    EXPECT_EQ(context.cargo_second.next_content, nullptr);
    EXPECT_EQ(room_by_id_total(kFerrySourceCabin)->contents, nullptr);
    EXPECT_EQ(context.cargo_second.in_room, kFerryDestinationCabin);  // LS1-ALLOW: obj-location
}

TEST(SpecProFerryCaptain, LeavesADestinationCabinIntactWhenTheSourceCabinsChainIsEmpty) {
    // Contract item 5, the `if (tmpch)`/`if (tmpobj)` ELSE arms. See
    // FerryCabinLayout's comment for why this state has to be constructed
    // rather than reached: production always links the captain into the source
    // cabin's chain, so the else arms are dead through this SPECIAL -- but
    // commit 3's relocate_all_occupants()/relocate_all_contents() must still
    // reproduce them, and this is the anchor for that.
    FerryCaptainContext context(FerryCabinLayout::kEmptySourceLoadedDestination);
    context.load_destination_cabin_contents();

    EXPECT_EQ(context.tick(), TRUE);

    EXPECT_EQ(rots::entity::first_occupant(room_by_id_total(kFerryDestinationCabin)), &context.resident);
    EXPECT_EQ(context.resident.next_in_room, nullptr); // LS1-ALLOW: representation-impl (occupant-chain SHAPE assertion -- pins the raw next_in_room links themselves, the one property no Stage-1 API expresses and the exact property LS-3b rewrites)
    EXPECT_EQ(location_of(&context.resident), kFerryDestinationCabin);
    EXPECT_EQ(rots::entity::first_occupant(room_by_id_total(kFerrySourceCabin)), nullptr);

    EXPECT_EQ(room_by_id_total(kFerryDestinationCabin)->contents, &context.crate);
    EXPECT_EQ(context.crate.next_content, nullptr);
    EXPECT_EQ(room_by_id_total(kFerrySourceCabin)->contents, nullptr);

    EXPECT_EQ(location_of(&context.captain), kFerrySourceCabin)
        << "A character whose location field names the source room but which is not LINKED into "
           "its occupant chain is not relocated: the splice walks the chain, and the trailing "
           "cursor block restores whatever location the captain already had.";
}
