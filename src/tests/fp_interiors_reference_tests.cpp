// fp-interiors wave, Task 1 (scaffolding, consumer-free -- no live formula code
// is changed by this file). Two things live here:
//
//   1. Exact-value tests for rots::fp::to_game_int (src/fp_policy.h), the ONE
//      boundary helper every T2 conversion will round through.
//   2. Byte-verbatim TEST-ONLY int "references" of the OLD (pre-conversion)
//      combat-math formulas the fp-interiors wave will convert in T2, paired
//      with TEST-LOCAL double "transcriptions" of what T2 will write (the
//      double interior + single rots::fp::to_game_int rounding). The paired
//      tests assert the double transcription stays within the census's
//      per-formula delta bound of the frozen int reference.
//
// Source of truth for every formula body, bound, and input vector:
// .superpowers/sdd/fpi-census.md (gitignored; T0's census) and
// .superpowers/sdd/fpi-task-1-brief.md. Every _int_reference function below
// cites its live source file:line range and is copied byte-verbatim (control
// flow and arithmetic operations unchanged) -- only sub-call outputs and
// char_data*-read fields become plain parameters (the census's authorized
// "parameterize the inputs" extraction shape -- see the task-1 brief: "That
// is not a redesign"). None of these reference/transcription functions are
// reachable from -- or linked into -- the game binary; this TU is test-tree
// only.
//
// T1 is consumer-free: the "double transcription" functions below are
// TEST-LOCAL only, representing what T2 will write directly in the live
// source files. T2 will point the same paired assertions at the live
// converted production functions instead of these test-local copies.
//
// Bound methodology (census Step 4): BOUND = (count of in-formula integer
// divisions the double chain defers/eliminates on the exercised path) + 1
// (the terminal round contributes <1 more; each eliminated division loses
// <1 in the old truncating code). Where this file's own line-by-line
// division count for a vector comes out higher than the census's stated
// approximate figure, the more conservative (larger) bound is used here and
// noted, per the same method -- the census itself calls this "a safe upper
// bound", not a tight one.
//
// Sub-call return values (class_HP's prof-point inputs, weapon_master's
// get_bonus_OB/PB, get_confuse_modifier, get_power_of_arda,
// utils::get_skill_penalty/get_dodge_penalty, weapon_skill_num, CAN_SEE,
// dispatch_attack_speed_multiplier, ...) are NOT re-implemented here -- per
// the census, an identically-invoked sub-call cancels out of the delta, so
// both the int reference and the double transcription simply take its
// result as a shared plain parameter (the exception is the two
// do_squareroot() shapes, which genuinely sit INSIDE the converted
// arithmetic chains -- see the two dedicated sections below).

#include "../comm.h"
#include "../fp_policy.h"
#include "../spells.h"
#include "../utils.h"
#include "rots/core/character.h"
#include "rots/core/descriptor.h"
#include "rots/core/object.h"
#include "rots/core/room.h"
#include "rots/core/tables.h"
#include "rots/core/types.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {

// ===========================================================================
// Step 2 -- rots::fp::to_game_int exact-value tests (permanent).
// ===========================================================================

TEST(ToGameInt, RoundsHalfAwayFromZeroPositive) {
    EXPECT_EQ(rots::fp::to_game_int(2.5), 3);
    EXPECT_EQ(rots::fp::to_game_int(0.5), 1);
    EXPECT_EQ(rots::fp::to_game_int(100.5), 101);
}

TEST(ToGameInt, RoundsHalfAwayFromZeroNegative) {
    EXPECT_EQ(rots::fp::to_game_int(-2.5), -3);
    EXPECT_EQ(rots::fp::to_game_int(-0.5), -1);
    EXPECT_EQ(rots::fp::to_game_int(-100.5), -101);
}

TEST(ToGameInt, TruncationVsRoundingPositive) {
    // These would truncate differently than they round -- the whole point of
    // routing every boundary through to_game_int instead of a bare (int) cast.
    EXPECT_EQ(rots::fp::to_game_int(2.4), 2);
    EXPECT_EQ(rots::fp::to_game_int(2.6), 3);
    EXPECT_EQ(rots::fp::to_game_int(100.499999), 100);
    EXPECT_EQ(rots::fp::to_game_int(100.500001), 101);
}

TEST(ToGameInt, TruncationVsRoundingNegative) {
    EXPECT_EQ(rots::fp::to_game_int(-2.4), -2);
    EXPECT_EQ(rots::fp::to_game_int(-2.6), -3);
    EXPECT_EQ(rots::fp::to_game_int(-100.499999), -100);
    EXPECT_EQ(rots::fp::to_game_int(-100.500001), -101);
}

TEST(ToGameInt, ZeroAndExactIntegers) {
    EXPECT_EQ(rots::fp::to_game_int(0.0), 0);
    EXPECT_EQ(rots::fp::to_game_int(-0.0), 0);
    EXPECT_EQ(rots::fp::to_game_int(3.0), 3);
    EXPECT_EQ(rots::fp::to_game_int(-3.0), -3);
}

// ===========================================================================
// Shared do_squareroot() transcriptions.
//
// Two distinct do_squareroot(int, char_data*) overloads exist in production
// (same signature, different TUs, deliberately non-conflicting -- see
// entity_lifecycle.cpp's own banner comment). Both sit INSIDE a converted
// arithmetic chain (B1/B6's entity_lifecycle.cpp version; B13's
// char_utils_combat.cpp version), so per the T0 census's CONTROLLER RULING
// they are handled differently, not both re-used as opaque canceling
// sub-calls:
//   - entity_lifecycle.cpp's sqrt-based overload: option (b) -- T2 inlines
//     std::sqrt(<double>) directly, no int-truncating argument. Used below
//     only inside the BYTE-VERBATIM int references (the OLD behavior); the
//     double transcriptions call std::sqrt() directly instead.
//   - char_utils_combat.cpp's table-interpolation overload: option (a) --
//     stays UNCHANGED and is called identically (same shared helper, though
//     not necessarily the same argument) by both sides. Transcribed once
//     below and reused by both the B13 int reference and double
//     transcription.
// ===========================================================================

// entity_lifecycle.cpp:1957 (anonymous-namespace do_squareroot(int,
// char_data*) overload) -- byte-verbatim. Feeds B1's class_hp_int_reference
// and B6's ene_regen_weapon_int_reference (the OLD, int-truncating shape).
int do_squareroot_sqrt_int_reference(int i) { return int(std::sqrt(i) * 200.0); }

// char_utils_combat.cpp:139-144 (file-local do_squareroot(int, char_data*)
// overload, table-interpolation over consts.cpp's square_root[171]) --
// byte-verbatim, using the REAL production square_root[] table (declared
// extern by ../utils.h, defined in consts.cpp and linked into this test
// binary) rather than a hand-copied duplicate -- this is genuinely the same
// data both a live call and this reference would read. Shared by BOTH the
// B13 int reference (OLD int-argument shape) and the B13 double
// transcription (per controller ruling option (a): this table lookup itself
// is UNCHANGED by the wave, only its argument's upstream precision moves).
int do_squareroot_table_int_reference(int i) {
    if (i / 4 > 170) {
        i = 170 * 4;
    }
    return ((4 - i % 4) * square_root[i / 4] + (i % 4) * square_root[i / 4 + 1]);
}

// ===========================================================================
// Family 1 -- recalc_abilities() HP (B1). entity_lifecycle.cpp:1977,1981,1985
// (!IS_NPC branch), plus its class_HP() sub-formula (entity_lifecycle.cpp:
// 1461-1468), which the controller ruling folds INTO this chain rather than
// treating as an opaque canceling sub-call (see the brief: "entity
// do_squareroot sites (B1 HP...) will convert with INLINE double sqrt").
// ===========================================================================

// entity_lifecycle.cpp:1461-1468 (class_HP) -- byte-verbatim; prof-point
// sub-call outputs and the RACE_ORC branch are plain parameters instead of a
// char_data*.
int class_hp_int_reference(int warrior_prof_points, int ranger_prof_points, int cleric_prof_points,
                           bool is_orc) {
    double hp_coofs = 3 * warrior_prof_points + 2 * ranger_prof_points + cleric_prof_points;
    if (is_orc) {
        hp_coofs = hp_coofs * 4.0 / 7.0;
    }
    return int(std::sqrt(hp_coofs) * 200.0);
}

// entity_lifecycle.cpp:1977,1981,1985 (recalc_abilities HP block) --
// byte-verbatim; class_hp is class_hp_int_reference(...)'s output.
int hp_int_reference(int level, int con, int constabilities_hit, int class_hp, int mini_level,
                     bool is_defender, int raw_stealth, int levela) {
    int hit = 10 + std::min(LEVEL_MAX, level) + constabilities_hit * con / 20 +
              (class_hp * (con + 20) / 14) * std::min(LEVEL_MAX * 100, mini_level) / 100000;
    if (is_defender) {
        hit += hit / 10;
    }
    hit = std::max(hit - (raw_stealth * levela + raw_stealth * 3) / 33, 10);
    return hit;
}

// T1 test-local double transcription of the whole HP chain (class_HP()
// inlined per the controller ruling -- std::sqrt() applied directly to a
// double, no int-truncating argument). T2 will write this same shape
// directly into entity_lifecycle.cpp.
double hp_family_double_transcription(int level, int con, int constabilities_hit,
                                      int warrior_prof_points, int ranger_prof_points,
                                      int cleric_prof_points, bool is_orc, int mini_level,
                                      bool is_defender, int raw_stealth, int levela) {
    double hp_coofs_d = 3.0 * warrior_prof_points + 2.0 * ranger_prof_points + cleric_prof_points;
    if (is_orc) {
        hp_coofs_d = hp_coofs_d * 4.0 / 7.0;
    }
    double class_hp_d = std::sqrt(hp_coofs_d) * 200.0;

    double hit =
        10.0 + std::min<double>(LEVEL_MAX, level) + constabilities_hit * con / 20.0 +
        (class_hp_d * (con + 20) / 14.0) * std::min<double>(LEVEL_MAX * 100, mini_level) / 100000.0;
    if (is_defender) {
        hit += hit / 10.0;
    }
    hit = std::max(hit - (raw_stealth * levela + raw_stealth * 3) / 33.0, 10.0);
    return hit;
}

TEST(HpFamily, PairedBoundV1BaselineNonDefenderNoStealth) {
    // Census bound 4 (3 eliminated divisions: /20, /14, /100000) + 1 for the
    // controller-ruling sqrt adjustment = 5.
    const int class_hp = class_hp_int_reference(100, 50, 50, false);
    const int ref = hp_int_reference(30, 20, 100, class_hp, 3000, false, 0, 30);
    const double dbl =
        hp_family_double_transcription(30, 20, 100, 100, 50, 50, false, 3000, false, 0, 30);
    EXPECT_LE(std::abs(rots::fp::to_game_int(dbl) - ref), 5);
}

TEST(HpFamily, PairedBoundV2DefenderBonus) {
    // Census bound 5 (adds the Defender /10) + 1 = 6.
    const int class_hp = class_hp_int_reference(100, 50, 50, false);
    const int ref = hp_int_reference(30, 20, 100, class_hp, 3000, true, 0, 30);
    const double dbl =
        hp_family_double_transcription(30, 20, 100, 100, 50, 50, false, 3000, true, 0, 30);
    EXPECT_LE(std::abs(rots::fp::to_game_int(dbl) - ref), 6);
}

TEST(HpFamily, PairedBoundV3StealthMalus) {
    // Census bound 6 (adds the stealth-malus /33) + 1 = 7.
    const int class_hp = class_hp_int_reference(100, 50, 50, false);
    const int ref = hp_int_reference(30, 20, 100, class_hp, 3000, true, 60, 30);
    const double dbl =
        hp_family_double_transcription(30, 20, 100, 100, 50, 50, false, 3000, true, 60, 30);
    EXPECT_LE(std::abs(rots::fp::to_game_int(dbl) - ref), 7);
}

TEST(HpFamily, ExactValueV1) {
    // Hand-derived (independently, not by running the code under test):
    // hp_coofs = 3*100 + 2*50 + 50 = 450; class_hp = int(sqrt(450)*200) =
    // int(21.2132...*200) = int(4242.64...) = 4242.
    // hit = 10 + min(30,30) + 100*20/20 + (4242*40/14)*min(3000,3000)/100000
    //     = 10 + 30 + 100 + (12120*3000)/100000 = 10+30+100+363 = 503.
    // hit = max(503 - 0, 10) = 503 (no Defender, no stealth).
    const int class_hp = class_hp_int_reference(100, 50, 50, false);
    ASSERT_EQ(class_hp, 4242);
    const int ref = hp_int_reference(30, 20, 100, class_hp, 3000, false, 0, 30);
    EXPECT_EQ(ref, 503);

    // Double transcription: hp_coofs_d = 450.0; class_hp_d =
    // sqrt(450)*200 = 4242.640687...; hit = 10+30+100 +
    // (4242.640687*40/14)*3000/100000 = 140 + 363.654916... = 503.654916...
    // -> round-half-away-from-zero -> 504.
    const double dbl =
        hp_family_double_transcription(30, 20, 100, 100, 50, 50, false, 3000, false, 0, 30);
    EXPECT_EQ(rots::fp::to_game_int(dbl), 504);
}

// ===========================================================================
// Family 1 -- recalc_abilities() mana (B2). entity_lifecycle.cpp:1989.
// ===========================================================================

int mana_int_reference(int constabilities_mana, int intel, int will, int mage_level) {
    return constabilities_mana + intel + will / 2 + mage_level * 2;
}

double mana_double_transcription(int constabilities_mana, int intel, int will, int mage_level) {
    return constabilities_mana + intel + will / 2.0 + mage_level * 2.0;
}

TEST(ManaFamily, PairedBoundOddWill) {
    // Census bound: 1 eliminated division (WILL/2) + 1 = 2.
    const int ref = mana_int_reference(10, 13, 11, 0);
    const double dbl = mana_double_transcription(10, 13, 11, 0);
    EXPECT_LE(std::abs(rots::fp::to_game_int(dbl) - ref), 2);
}

TEST(ManaFamily, PairedBoundEvenWillTieNoTruncation) {
    const int ref = mana_int_reference(10, 13, 10, 0);
    const double dbl = mana_double_transcription(10, 13, 10, 0);
    EXPECT_LE(std::abs(rots::fp::to_game_int(dbl) - ref), 2);
}

TEST(ManaFamily, ExactValueEvenWillNoTruncation) {
    // Hand-derived: 10 + 13 + 10/2 + 0 = 10+13+5 = 28 both sides (no
    // fractional term at all -- WILL is even, so int and double agree
    // exactly).
    EXPECT_EQ(mana_int_reference(10, 13, 10, 0), 28);
    EXPECT_EQ(rots::fp::to_game_int(mana_double_transcription(10, 13, 10, 0)), 28);
}

// ===========================================================================
// Family 1 -- recalc_abilities() move (B3). entity_lifecycle.cpp:1993 (the
// initial assignment only -- the later flat +=15/+=50 race bonuses stay
// exact int adds per the census and are not part of this boundary chain).
// ===========================================================================

int move_int_reference(int constabilities_move, int con, int ranger_level, int travelling) {
    return constabilities_move + con + 20 + ranger_level + travelling / 4;
}

double move_double_transcription(int constabilities_move, int con, int ranger_level,
                                 int travelling) {
    return constabilities_move + con + 20.0 + ranger_level + travelling / 4.0;
}

TEST(MoveFamily, PairedBoundTravellingFractional) {
    // Census bound: 1 eliminated division (TRAVELLING/4) + 1 = 2.
    const int ref = move_int_reference(10, 20, 0, 10);
    const double dbl = move_double_transcription(10, 20, 0, 10);
    EXPECT_LE(std::abs(rots::fp::to_game_int(dbl) - ref), 2);
}

TEST(MoveFamily, ExactValueTravellingExactMultipleOfFour) {
    // Hand-derived: 10 + 20 + 20 + 0 + 8/4 = 52 both sides (8/4 is exact).
    EXPECT_EQ(move_int_reference(10, 20, 0, 8), 52);
    EXPECT_EQ(rots::fp::to_game_int(move_double_transcription(10, 20, 0, 8)), 52);
}

// ===========================================================================
// Family 4 -- recalc_abilities() null_speed (B4). entity_lifecycle.cpp:2014.
// ===========================================================================

int null_speed_int_reference(int dex, int attack, int stealth) {
    return 3 * dex + 2 * (attack + stealth / 2) / 3 + 100;
}

double null_speed_double_transcription(int dex, int attack, int stealth) {
    return 3.0 * dex + (2.0 * (attack + stealth / 2.0)) / 3.0 + 100.0;
}

TEST(NullSpeedFamily, PairedBoundV1NoStealth) {
    // Census bound: 2 eliminated divisions (STEALTH/2 and the outer /3) + 1 = 3.
    const int ref = null_speed_int_reference(20, 100, 0);
    const double dbl = null_speed_double_transcription(20, 100, 0);
    EXPECT_LE(std::abs(rots::fp::to_game_int(dbl) - ref), 3);
}

TEST(NullSpeedFamily, PairedBoundV2WithStealth) {
    const int ref = null_speed_int_reference(20, 100, 60);
    const double dbl = null_speed_double_transcription(20, 100, 60);
    EXPECT_LE(std::abs(rots::fp::to_game_int(dbl) - ref), 3);
}

TEST(NullSpeedFamily, ExactValueV1) {
    // Hand-derived: 3*20 + 2*(100+0)/3 + 100 = 60 + int(66.666...) + 100 =
    // 60 + 66 + 100 = 226 (int, truncates). Double: 60 + 66.666.../1 ... =
    // 60.0 + (2.0*100.0)/3.0 + 100.0 = 60 + 66.666666... + 100 =
    // 226.666666... -> round-half-away-from-zero -> 227.
    EXPECT_EQ(null_speed_int_reference(20, 100, 0), 226);
    EXPECT_EQ(rots::fp::to_game_int(null_speed_double_transcription(20, 100, 0)), 227);
}

// ===========================================================================
// Family 4 -- recalc_abilities() str_speed (B5). entity_lifecycle.cpp:2016,
// 2019, 2024, 2026, 2028.
// ===========================================================================

int str_speed_int_reference(int bal_str, int weight, int bulk, bool is_twohanded, int dex) {
    int str_speed = bal_str * 2500000 / (weight * (bulk + 3));
    if (is_twohanded) {
        str_speed *= 2;
    }
    if (bulk < 4) {
        int dex_speed = dex * 2500000 / (weight * (bulk + 3));
        int tmp2 = (str_speed * bulk / 5) + (dex_speed * (5 - bulk) / 5);
        str_speed = std::max(str_speed, tmp2);
    }
    return str_speed;
}

double str_speed_double_transcription(int bal_str, int weight, int bulk, bool is_twohanded,
                                      int dex) {
    double str_speed = bal_str * 2500000.0 / (weight * (bulk + 3));
    if (is_twohanded) {
        str_speed *= 2.0;
    }
    if (bulk < 4) {
        double dex_speed = dex * 2500000.0 / (weight * (bulk + 3));
        double tmp2 = (str_speed * bulk / 5.0) + (dex_speed * (5 - bulk) / 5.0);
        str_speed = std::max(str_speed, tmp2);
    }
    return str_speed;
}

TEST(StrSpeedFamily, PairedBoundV1OneHandedBulk3) {
    // Census bound: 1 eliminated division (the base str_speed division;
    // bulk==3 so the dex_speed branch's two further divisions also apply) + 1 = 5.
    const int ref = str_speed_int_reference(20, 10, 3, false, 20);
    const double dbl = str_speed_double_transcription(20, 10, 3, false, 20);
    EXPECT_LE(std::abs(rots::fp::to_game_int(dbl) - ref), 5);
}

TEST(StrSpeedFamily, PairedBoundV2TwohandedBulk2) {
    const int ref = str_speed_int_reference(20, 10, 2, true, 20);
    const double dbl = str_speed_double_transcription(20, 10, 2, true, 20);
    EXPECT_LE(std::abs(rots::fp::to_game_int(dbl) - ref), 5);
}

TEST(StrSpeedFamily, ExactValueV1) {
    // Hand-derived: str_speed = 20*2500000/(10*6) = 50000000/60 =
    // 833333.333... -> int truncates to 833333. bulk(3)<4, so:
    // dex_speed = same as str_speed base = 833333 (int) / 833333.333...(double).
    // tmp2_int = 833333*3/5 + 833333*2/5 = 499999 (int, 3/5 and 2/5 truncate
    // separately) + 333333 = 833332; max(833333,833332)=833333 unchanged.
    EXPECT_EQ(str_speed_int_reference(20, 10, 3, false, 20), 833333);
    // Double: 833333.333... unaffected by the max() branch (tmp2_d works out
    // to <= str_speed_d here too), rounds to 833333 (fraction .333 < .5).
    EXPECT_EQ(rots::fp::to_game_int(str_speed_double_transcription(20, 10, 3, false, 20)), 833333);
}

// ===========================================================================
// Family 4 -- recalc_abilities() ENE_regen, weapon branch (B6).
// entity_lifecycle.cpp:2031-2045 (the harmonic mean, do_squareroot, race
// bumps, and the int*=float attack-speed multiply).
//
// The harmonic mean reads the PRE-ROUND double str_speed/null_speed in the
// double transcription (per the census: "the harmonic mean ... must read the
// full-precision doubles, not the rounded fields") but the OLD, already-int
// field values in the int reference (that's literally what the old code
// read -- both fields were already stored as ints by this point).
// ===========================================================================

int ene_regen_weapon_int_reference(int str_speed, int null_speed, bool race_dwarf_axe,
                                   bool race_haradrim_spears, float attack_speed_multiplier) {
    int tmp = 1000000;
    tmp /= 1000000 / str_speed + 1000000 / (null_speed * null_speed);
    int ene_regen = do_squareroot_sqrt_int_reference(tmp / 100) / 20;
    if (race_dwarf_axe) {
        ene_regen += std::min(ene_regen / 10, 10);
    } else if (race_haradrim_spears) {
        ene_regen += std::min(ene_regen / 20, 20);
    }
    ene_regen =
        static_cast<int>(ene_regen * attack_speed_multiplier); // int *= float (old behavior)
    return ene_regen;
}

// Controller ruling option (b): std::sqrt(tmp_d/100.0) inlined directly,
// replacing the OLD int-truncating do_squareroot(tmp/100, ch) call -- the
// argument truncation IS the per-step truncation the wave removes here.
double ene_regen_weapon_double_transcription(double str_speed_d, double null_speed_d,
                                             bool race_dwarf_axe, bool race_haradrim_spears,
                                             double attack_speed_multiplier) {
    double tmp = 1000000.0 / (1000000.0 / str_speed_d + 1000000.0 / (null_speed_d * null_speed_d));
    double ene_regen = std::sqrt(tmp / 100.0) * 200.0 / 20.0;
    if (race_dwarf_axe) {
        ene_regen += std::min(ene_regen / 10.0, 10.0);
    } else if (race_haradrim_spears) {
        ene_regen += std::min(ene_regen / 20.0, 20.0);
    }
    ene_regen *= attack_speed_multiplier;
    return ene_regen;
}

TEST(EneRegenWeaponFamily, PairedBoundV1) {
    // Brief-mandated bound for B6: census bound 6 + 1 (sqrt no longer
    // cancels, per the controller ruling) = 7.
    const int str_speed_i = str_speed_int_reference(20, 10, 3, false, 20);
    const int null_speed_i = null_speed_int_reference(20, 100, 0);
    const double str_speed_d = str_speed_double_transcription(20, 10, 3, false, 20);
    const double null_speed_d = null_speed_double_transcription(20, 100, 0);

    const int ref = ene_regen_weapon_int_reference(str_speed_i, null_speed_i, false, false, 1.0f);
    const double dbl =
        ene_regen_weapon_double_transcription(str_speed_d, null_speed_d, false, false, 1.0);
    EXPECT_LE(std::abs(rots::fp::to_game_int(dbl) - ref), 7);
}

TEST(EneRegenWeaponFamily, PairedBoundV2Twohanded) {
    const int str_speed_i = str_speed_int_reference(20, 10, 2, true, 20);
    const int null_speed_i = null_speed_int_reference(20, 100, 0);
    const double str_speed_d = str_speed_double_transcription(20, 10, 2, true, 20);
    const double null_speed_d = null_speed_double_transcription(20, 100, 0);

    const int ref = ene_regen_weapon_int_reference(str_speed_i, null_speed_i, false, false, 1.0f);
    const double dbl =
        ene_regen_weapon_double_transcription(str_speed_d, null_speed_d, false, false, 1.0);
    EXPECT_LE(std::abs(rots::fp::to_game_int(dbl) - ref), 7);
}

TEST(EneRegenWeaponFamily, PairedBoundV3RaceBump) {
    const int str_speed_i = str_speed_int_reference(20, 10, 3, false, 20);
    const int null_speed_i = null_speed_int_reference(20, 100, 0);
    const double str_speed_d = str_speed_double_transcription(20, 10, 3, false, 20);
    const double null_speed_d = null_speed_double_transcription(20, 100, 0);

    const int ref = ene_regen_weapon_int_reference(str_speed_i, null_speed_i, true, false, 1.0f);
    const double dbl =
        ene_regen_weapon_double_transcription(str_speed_d, null_speed_d, true, false, 1.0);
    EXPECT_LE(std::abs(rots::fp::to_game_int(dbl) - ref), 7);
}

TEST(EneRegenWeaponFamily, ExactValueV1) {
    // Hand-derived from the V1 str_speed/null_speed values above
    // (str_speed_int=833333, null_speed_int=226):
    // tmp = 1000000 / (1000000/833333 + 1000000/(226*226))
    //     = 1000000 / (1 + 1000000/51076) = 1000000 / (1 + 19) = 1000000/20 = 50000.
    // do_squareroot_table... no: entity's sqrt-based do_squareroot(50000/100=500)
    //     = int(sqrt(500)*200) = int(22.3606...*200) = int(4472.13...) = 4472.
    // ene_regen = 4472/20 = 223 (int). No race bump, *1.0 unchanged.
    const int str_speed_i = str_speed_int_reference(20, 10, 3, false, 20);
    const int null_speed_i = null_speed_int_reference(20, 100, 0);
    ASSERT_EQ(str_speed_i, 833333);
    ASSERT_EQ(null_speed_i, 226);
    const int ref = ene_regen_weapon_int_reference(str_speed_i, null_speed_i, false, false, 1.0f);
    EXPECT_EQ(ref, 223);
}

// ===========================================================================
// Family 4 -- recalc_abilities() ENE_regen, no-weapon branch (B7).
// entity_lifecycle.cpp:2048. Exact ints; the round is a no-op, but the
// helper is applied anyway for grep-uniformity (per the census).
// ===========================================================================

int ene_regen_noweapon_int_reference(int dex) { return 60 + 5 * dex; }

double ene_regen_noweapon_double_transcription(int dex) { return 60.0 + 5.0 * dex; }

TEST(EneRegenNoWeaponFamily, PairedBoundExact) {
    // Census bound: 0 eliminated divisions -> exact 0 delta.
    const int ref = ene_regen_noweapon_int_reference(20);
    const double dbl = ene_regen_noweapon_double_transcription(20);
    EXPECT_EQ(rots::fp::to_game_int(dbl), ref);
}

TEST(EneRegenNoWeaponFamily, ExactValue) {
    EXPECT_EQ(ene_regen_noweapon_int_reference(20), 160);
    EXPECT_EQ(rots::fp::to_game_int(ene_regen_noweapon_double_transcription(20)), 160);
}

// ===========================================================================
// T2a -- live recalc_abilities() repointing. entity_lifecycle.cpp's
// recalc_abilities() (B1-B7) now computes HP/mana/move/null_speed/str_speed/
// ENE_regen in double, single-rounding through rots::fp::to_game_int at each
// field write. These tests drive the REAL, converted production function on
// a constructed char_data at the same vectors as the paired tests above and
// assert equality with the (unchanged, test-local) double transcriptions --
// per the task-2a brief ("add tests that drive the live recalc_abilities on
// a constructed char_data ... and assert equality with the transcription").
// The transcription/int-reference tests above are left as-is (T1's oracle).
// ===========================================================================

struct RecalcAbilitiesTestContext {
    char_data character{};
    char_prof_data profs{};

    RecalcAbilitiesTestContext() {
        character.profs = &profs;
        // character.knowledge is an owning std::vector<byte> (RAII T3); size
        // it to MAX_SKILLS the way clear_char() would for a PC, mirroring
        // CharUtilsTestContext's fixture (char_utils_tests.cpp) -- this
        // fixture never calls clear_char() either.
        character.knowledge.assign(MAX_SKILLS, 0);
        character.player.name = const_cast<char*>("player-name");
        character.player.short_descr = const_cast<char*>("mob-name");
        // player.race defaults to 0 (RACE_GOD); max_race_str[RACE_GOD] == 22
        // (consts.cpp), so GET_BAL_STR(STR<=22) == GET_STR unchanged for
        // every vector below (all use STR/BAL_STR == 20).
    }
};

obj_data make_recalc_test_weapon(int weight, int bulk) {
    obj_data weapon{};
    weapon.obj_flags.weight = weight;
    weapon.obj_flags.value[2] = bulk;
    return weapon;
}

TEST(RecalcAbilitiesLive, NoWeaponBranchMatchesHpManaMoveAndEneRegenBoundaries) {
    // Same vectors as HpFamily::ExactValueV1 (B1), ManaFamily::
    // ExactValueEvenWillNoTruncation (B2), MoveFamily::
    // ExactValueTravellingExactMultipleOfFour (B3), and EneRegenNoWeaponFamily
    // ::ExactValue (B7) above -- one character, one recalc_abilities() call,
    // all four no-weapon-reachable boundaries checked at once.
    RecalcAbilitiesTestContext ctx;
    ctx.character.player.level = 30;
    ctx.character.tmpabilities.con = 20;
    ctx.character.tmpabilities.intel = 13;
    ctx.character.tmpabilities.wil = 10;
    ctx.character.tmpabilities.dex = 20;
    ctx.character.constabilities.hit = 100;
    ctx.character.constabilities.mana = 10;
    ctx.character.constabilities.move = 10;
    ctx.profs.prof_coof[PROF_WARRIOR] = 100;
    ctx.profs.prof_coof[PROF_RANGER] = 50;
    ctx.profs.prof_coof[PROF_CLERIC] = 50;
    ctx.character.specials2.mini_level = 3000;
    ctx.character.knowledge[SKILL_TRAVELLING] = 8;

    recalc_abilities(&ctx.character);

    // B1 (HP): the OLD (pre-conversion) per-step-truncating code produced
    // 503 (HpFamily::ExactValueV1's hp_int_reference, re-verified here); the
    // converted double chain rounds the full-precision 503.654916... up to
    // 504 -- the "~1 HP artifact" this wave's controller ruling flagged.
    // Both assertions pin the live function to the NEW value and record
    // that it genuinely differs from the frozen OLD reference (this is the
    // task's RED/GREEN evidence vector: pre-conversion the live function
    // returned 503 and this EXPECT_EQ(..., 504) would have failed).
    const int class_hp = class_hp_int_reference(100, 50, 50, false);
    const int old_hp_ref = hp_int_reference(30, 20, 100, class_hp, 3000, false, 0, 30);
    EXPECT_EQ(old_hp_ref, 503);
    EXPECT_NE(ctx.character.abilities.hit, old_hp_ref);
    const double hp_dbl =
        hp_family_double_transcription(30, 20, 100, 100, 50, 50, false, 3000, false, 0, 30);
    EXPECT_EQ(ctx.character.abilities.hit, rots::fp::to_game_int(hp_dbl));
    EXPECT_EQ(ctx.character.abilities.hit, 504);

    // B2 (mana): WILL=10 is even, so int and double agree exactly (no
    // drift) -- still repointed at the live function for parity.
    EXPECT_EQ(ctx.character.abilities.mana,
              rots::fp::to_game_int(mana_double_transcription(10, 13, 10, 0)));
    EXPECT_EQ(ctx.character.abilities.mana, 28);

    // B3 (move): TRAVELLING=8 -> 8/4=2 exactly, so int and double agree
    // exactly (no drift) -- still repointed at the live function.
    EXPECT_EQ(ctx.character.abilities.move,
              rots::fp::to_game_int(move_double_transcription(10, 20, 0, 8)));
    EXPECT_EQ(ctx.character.abilities.move, 52);

    // B7 (no-weapon ENE_regen): exact ints both sides, round is a no-op.
    EXPECT_EQ(GET_ENE_REGEN(&ctx.character),
              rots::fp::to_game_int(ene_regen_noweapon_double_transcription(20)));
    EXPECT_EQ(GET_ENE_REGEN(&ctx.character), 160);
}

TEST(RecalcAbilitiesLive, WeaponBranchMatchesNullSpeedStrSpeedAndEneRegenBoundaries) {
    // Same vector as NullSpeedFamily::ExactValueV1 (B4) / StrSpeedFamily::
    // ExactValueV1 (B5) / EneRegenWeaponFamily::ExactValueV1 (B6) above:
    // DEX=20, ATTACK=100, STEALTH=0, BAL_STR=20, one-handed weapon
    // weight=10/bulk=3, no race bump, no attack-speed-multiplier hook
    // registered (defaults to 1.0f -- entity_lifecycle.cpp's
    // dispatch_attack_speed_multiplier() STUB fallback).
    RecalcAbilitiesTestContext ctx;
    ctx.character.player.level = 30;
    ctx.character.tmpabilities.con = 20;
    ctx.character.tmpabilities.dex = 20;
    ctx.character.tmpabilities.str = 20;
    ctx.character.knowledge[SKILL_ATTACK] = 100;
    obj_data weapon = make_recalc_test_weapon(/*weight=*/10, /*bulk=*/3);
    ctx.character.equipment[WIELD] = &weapon;

    recalc_abilities(&ctx.character);

    // B4 (null_speed): the OLD int-truncating code gives 226
    // (NullSpeedFamily::ExactValueV1); the double chain rounds the
    // full-precision 226.666... up to 227 -- a second genuine drift point,
    // matching the double transcription exactly.
    const double null_speed_d = null_speed_double_transcription(20, 100, 0);
    EXPECT_NE(ctx.character.specials.null_speed, null_speed_int_reference(20, 100, 0));
    EXPECT_EQ(null_speed_int_reference(20, 100, 0), 226);
    EXPECT_EQ(ctx.character.specials.null_speed, rots::fp::to_game_int(null_speed_d));
    EXPECT_EQ(ctx.character.specials.null_speed, 227);

    // B5 (str_speed): the max()-clamped fraction (.333...) rounds down both
    // sides here -- no drift, but still proves the live field matches the
    // double transcription exactly, not merely within the paired bound.
    const double str_speed_d = str_speed_double_transcription(20, 10, 3, false, 20);
    EXPECT_EQ(ctx.character.specials.str_speed, rots::fp::to_game_int(str_speed_d));
    EXPECT_EQ(ctx.character.specials.str_speed, 833333);

    // B6 (ENE_regen, weapon branch): the harmonic mean must read the
    // PRE-ROUND doubles (str_speed_d/null_speed_d), not the just-rounded
    // int fields above, to stay a single-rounding result -- exactly what
    // ene_regen_weapon_double_transcription's own parameters are (per its
    // banner comment). No race bump (race defaults to RACE_GOD, not
    // DWARF/HARADRIM); multiplier defaults to 1.0 (unregistered hook).
    const double ene_regen_d =
        ene_regen_weapon_double_transcription(str_speed_d, null_speed_d, false, false, 1.0);
    EXPECT_EQ(GET_ENE_REGEN(&ctx.character), rots::fp::to_game_int(ene_regen_d));
}

// ===========================================================================
// Family 2 -- get_real_OB() (B8). visibility.cpp:148-263, player (!IS_NPC)
// branch. Sub-call outputs (weapon_master.get_bonus_OB(), get_confuse_
// modifier, get_power_of_arda, utils::get_skill_penalty, CAN_SEE,
// weapon_skill_num(...)) and GET_*() macro reads become plain parameters.
// tactics_code takes the raw TACTICS_* value (utils.h); 0/other hits the
// switch's default arm, same as the live code.
// ===========================================================================

int get_real_ob_player_int_reference(
    bool is_light_fighting, bool has_weapon, int weapon_bulk, bool weapon_weight_le_cutoff,
    int warrior_level, int ranger_level, int max_warrior_level, int bal_str, int tmpabilities_dex,
    int levela, int ob, int skill_penalty, int bonus_ob, int raw_knowledge_natural_attack, int str,
    int weapon_skill_base, bool is_twohanded, bool is_ranged, int weapon_value2,
    int archery_skill_raw, int twohanded_knowledge_raw, int tactics_code, int berserk_skill,
    bool is_confused, int confuse_modifier, int power_of_arda, bool race_uruk, bool race_orc,
    bool race_magus, bool race_ologhai, bool can_see) {
    int wl = warrior_level;
    int offense_stat = bal_str;

    if (is_light_fighting && has_weapon) {
        if (weapon_bulk <= 2 || (weapon_bulk == 3 && weapon_weight_le_cutoff)) {
            offense_stat = std::max(offense_stat, tmpabilities_dex);
            int ranger_bonus = ranger_level / 3;
            wl += ranger_bonus;
        }
    }

    int ob_bonus = (wl * 3 + 3 * max_warrior_level * levela / 30) / 2 + offense_stat;

    int tmpob = ob;
    tmpob -= skill_penalty;
    tmpob += bonus_ob;

    int weapon_skill = 0;
    if (!has_weapon && raw_knowledge_natural_attack == 0) {
        return tmpob + ob_bonus;
    } else if (!has_weapon && raw_knowledge_natural_attack > 0) {
        weapon_skill = raw_knowledge_natural_attack;
        tmpob -= (str / 2 - 6);
    } else {
        weapon_skill = weapon_skill_base;
        if (is_twohanded) {
            if (is_ranged) {
                tmpob += weapon_value2 * (200 + archery_skill_raw) / 100 - 15;
                weapon_skill = (weapon_skill + archery_skill_raw) / 2;
            } else {
                tmpob += weapon_value2 * (200 + twohanded_knowledge_raw) / 100 - 15;
                weapon_skill = (weapon_skill + twohanded_knowledge_raw) / 2;
            }
        } else {
            tmpob -= (weapon_value2 * 2 - 6);
        }
    }

    int tactics = 0;
    switch (tactics_code) {
    case TACTICS_DEFENSIVE:
        tmpob += ob_bonus - ob_bonus / 4 - 8;
        tactics = 4;
        break;
    case TACTICS_CAREFUL:
        tmpob += ob_bonus - ob_bonus / 8 - 4;
        tactics = 6;
        break;
    case TACTICS_NORMAL:
        tmpob += ob_bonus;
        tactics = 8;
        break;
    case TACTICS_AGGRESSIVE:
        tmpob += ob_bonus + ob_bonus / 16 + 2;
        tactics = 10;
        break;
    case TACTICS_BERSERK:
        tmpob += ob_bonus + ob_bonus / 16 + 5 + berserk_skill / 8;
        tactics = 10;
        break;
    default:
        tmpob += ob_bonus + bal_str;
        break;
    };

    if (is_confused) {
        tmpob -= (confuse_modifier * 2 / 3);
    }

    int sun_mod = power_of_arda;
    if (sun_mod) {
        if (race_uruk)
            tmpob = tmpob * 4 / 5 - sun_mod;
        if (race_orc)
            tmpob = tmpob * 3 / 4 - sun_mod;
        if (race_magus)
            tmpob = tmpob * 4 / 5 - sun_mod;
        if (race_ologhai)
            tmpob = tmpob * 4 / 5 - sun_mod;
    }

    if (!can_see) {
        tmpob -= 10;
    }

    if (!has_weapon) {
        tmpob += weapon_skill * (str + 20) * tactics / 1000;
    } else {
        tmpob += weapon_skill * (weapon_value2 + 20) * tactics / 1000;
    }

    return tmpob;
}

double get_real_ob_player_double_transcription(
    bool is_light_fighting, bool has_weapon, int weapon_bulk, bool weapon_weight_le_cutoff,
    int warrior_level, int ranger_level, int max_warrior_level, int bal_str, int tmpabilities_dex,
    int levela, int ob, int skill_penalty, int bonus_ob, int raw_knowledge_natural_attack, int str,
    int weapon_skill_base, bool is_twohanded, bool is_ranged, int weapon_value2,
    int archery_skill_raw, int twohanded_knowledge_raw, int tactics_code, int berserk_skill,
    bool is_confused, int confuse_modifier, int power_of_arda, bool race_uruk, bool race_orc,
    bool race_magus, bool race_ologhai, bool can_see) {
    double wl = warrior_level;
    double offense_stat = bal_str;

    if (is_light_fighting && has_weapon) {
        if (weapon_bulk <= 2 || (weapon_bulk == 3 && weapon_weight_le_cutoff)) {
            offense_stat = std::max(offense_stat, double(tmpabilities_dex));
            double ranger_bonus = ranger_level / 3.0;
            wl += ranger_bonus;
        }
    }

    double ob_bonus = (wl * 3.0 + (3.0 * max_warrior_level * levela) / 30.0) / 2.0 + offense_stat;

    double tmpob = ob;
    tmpob -= skill_penalty;
    tmpob += bonus_ob;

    double weapon_skill = 0.0;
    if (!has_weapon && raw_knowledge_natural_attack == 0) {
        return tmpob + ob_bonus;
    } else if (!has_weapon && raw_knowledge_natural_attack > 0) {
        weapon_skill = raw_knowledge_natural_attack;
        tmpob -= (str / 2.0 - 6.0);
    } else {
        weapon_skill = weapon_skill_base;
        if (is_twohanded) {
            if (is_ranged) {
                tmpob += (weapon_value2 * (200.0 + archery_skill_raw)) / 100.0 - 15.0;
                weapon_skill = (weapon_skill + archery_skill_raw) / 2.0;
            } else {
                tmpob += (weapon_value2 * (200.0 + twohanded_knowledge_raw)) / 100.0 - 15.0;
                weapon_skill = (weapon_skill + twohanded_knowledge_raw) / 2.0;
            }
        } else {
            tmpob -= (weapon_value2 * 2.0 - 6.0);
        }
    }

    double tactics = 0.0;
    switch (tactics_code) {
    case TACTICS_DEFENSIVE:
        tmpob += ob_bonus - ob_bonus / 4.0 - 8.0;
        tactics = 4.0;
        break;
    case TACTICS_CAREFUL:
        tmpob += ob_bonus - ob_bonus / 8.0 - 4.0;
        tactics = 6.0;
        break;
    case TACTICS_NORMAL:
        tmpob += ob_bonus;
        tactics = 8.0;
        break;
    case TACTICS_AGGRESSIVE:
        tmpob += ob_bonus + ob_bonus / 16.0 + 2.0;
        tactics = 10.0;
        break;
    case TACTICS_BERSERK:
        tmpob += ob_bonus + ob_bonus / 16.0 + 5.0 + berserk_skill / 8.0;
        tactics = 10.0;
        break;
    default:
        tmpob += ob_bonus + bal_str;
        break;
    };

    if (is_confused) {
        tmpob -= (confuse_modifier * 2.0) / 3.0;
    }

    int sun_mod = power_of_arda;
    if (sun_mod) {
        if (race_uruk)
            tmpob = (tmpob * 4.0) / 5.0 - sun_mod;
        if (race_orc)
            tmpob = (tmpob * 3.0) / 4.0 - sun_mod;
        if (race_magus)
            tmpob = (tmpob * 4.0) / 5.0 - sun_mod;
        if (race_ologhai)
            tmpob = (tmpob * 4.0) / 5.0 - sun_mod;
    }

    if (!can_see) {
        tmpob -= 10.0;
    }

    if (!has_weapon) {
        tmpob += (weapon_skill * (str + 20.0) * tactics) / 1000.0;
    } else {
        tmpob += (weapon_skill * (weapon_value2 + 20.0) * tactics) / 1000.0;
    }

    return tmpob;
}

TEST(GetRealObFamily, PairedBoundV1OneHandedNormalTactics) {
    // 3 eliminated divisions (ob_bonus's /30 and outer /2, plus the final
    // /1000) + 1 = 4.
    const int ref = get_real_ob_player_int_reference(
        false, true, 3, false, 30, 0, 30, 20, 20, 30, 50, 5, 10, 0, 20, 80, false, false, 10, 0, 0,
        TACTICS_NORMAL, 0, false, 0, 0, false, false, false, false, true);
    const double dbl = get_real_ob_player_double_transcription(
        false, true, 3, false, 30, 0, 30, 20, 20, 30, 50, 5, 10, 0, 20, 80, false, false, 10, 0, 0,
        TACTICS_NORMAL, 0, false, 0, 0, false, false, false, false, true);
    EXPECT_LE(std::abs(rots::fp::to_game_int(dbl) - ref), 4);
}

TEST(GetRealObFamily, PairedBoundV2TwohandedRanged) {
    // Adds the ranged /100 and the weapon-skill-averaging /2: 5 divisions + 1 = 6.
    const int ref = get_real_ob_player_int_reference(
        false, true, 3, false, 30, 0, 30, 20, 20, 30, 50, 5, 10, 0, 20, 80, true, true, 20, 40, 0,
        TACTICS_NORMAL, 0, false, 0, 0, false, false, false, false, true);
    const double dbl = get_real_ob_player_double_transcription(
        false, true, 3, false, 30, 0, 30, 20, 20, 30, 50, 5, 10, 0, 20, 80, true, true, 20, 40, 0,
        TACTICS_NORMAL, 0, false, 0, 0, false, false, false, false, true);
    EXPECT_LE(std::abs(rots::fp::to_game_int(dbl) - ref), 6);
}

TEST(GetRealObFamily, PairedBoundV3UrukPowerOfArda) {
    // V1's 3 divisions plus the URUK arda malus's *4/5: 4 divisions + 1 = 5.
    const int ref = get_real_ob_player_int_reference(
        false, true, 3, false, 30, 0, 30, 20, 20, 30, 50, 5, 10, 0, 20, 80, false, false, 10, 0, 0,
        TACTICS_NORMAL, 0, false, 0, 10, true, false, false, false, true);
    const double dbl = get_real_ob_player_double_transcription(
        false, true, 3, false, 30, 0, 30, 20, 20, 30, 50, 5, 10, 0, 20, 80, false, false, 10, 0, 0,
        TACTICS_NORMAL, 0, false, 0, 10, true, false, false, false, true);
    EXPECT_LE(std::abs(rots::fp::to_game_int(dbl) - ref), 5);
}

TEST(GetRealObFamily, PairedBoundTieOBZero) {
    // ob chosen so the int reference lands exactly on OB==0 -- the TA2/TA3
    // tie-asymmetry boundary the census flags (fight.cpp:2632/2656).
    const int ref = get_real_ob_player_int_reference(
        false, true, 3, false, 30, 0, 30, 20, 20, 30, -120, 5, 10, 0, 20, 80, false, false, 10, 0,
        0, TACTICS_NORMAL, 0, false, 0, 0, false, false, false, false, true);
    ASSERT_EQ(ref, 0);
    const double dbl = get_real_ob_player_double_transcription(
        false, true, 3, false, 30, 0, 30, 20, 20, 30, -120, 5, 10, 0, 20, 80, false, false, 10, 0,
        0, TACTICS_NORMAL, 0, false, 0, 0, false, false, false, false, true);
    EXPECT_LE(std::abs(rots::fp::to_game_int(dbl) - ref), 4);
}

TEST(GetRealObFamily, ExactValueV1) {
    // Hand-derived: ob_bonus = (30*3 + 3*30*30/30)/2 + 20 = (90+90)/2+20 =
    // 90+20 = 110. tmpob = 50-5+10 = 55. weapon (one-handed): tmpob -=
    // (10*2-6)=14 -> 41. TACTICS_NORMAL: tmpob += 110 -> 151, tactics=8.
    // No confuse/arda. can_see. final: tmpob += 80*(10+20)*8/1000 =
    // 151 + 19200/1000 = 151+19 = 170.
    const int ref = get_real_ob_player_int_reference(
        false, true, 3, false, 30, 0, 30, 20, 20, 30, 50, 5, 10, 0, 20, 80, false, false, 10, 0, 0,
        TACTICS_NORMAL, 0, false, 0, 0, false, false, false, false, true);
    EXPECT_EQ(ref, 170);

    // Double transcription evaluates to 170.2 (19200/1000 fully carries the
    // .2 the int math truncated away); round-half-away-from-zero -> 170.
    const double dbl = get_real_ob_player_double_transcription(
        false, true, 3, false, 30, 0, 30, 20, 20, 30, 50, 5, 10, 0, 20, 80, false, false, 10, 0, 0,
        TACTICS_NORMAL, 0, false, 0, 0, false, false, false, false, true);
    EXPECT_EQ(rots::fp::to_game_int(dbl), 170);
}

// ===========================================================================
// Family 2 -- get_real_parry() (B9). visibility.cpp:265-363, player
// (!IS_NPC) branch.
// ===========================================================================

int get_real_parry_player_int_reference(
    int parry_stat, int warrior_level, int level, int bal_str, int bonus_pb, bool has_weapon,
    int raw_knowledge_natural_attack, int raw_skill_natural_attack, int weapon_value1,
    int weapon_skill_base, bool is_bow, bool is_twohanded, int archery_skill_raw,
    int twohanded_knowledge_raw, int parry_knowledge_raw, int tactics_code, bool is_confused,
    int confuse_modifier, int power_of_arda, bool race_uruk, bool race_orc, bool race_magus,
    bool race_ologhai, bool can_see) {
    int tmpparry = parry_stat;
    int bonus = warrior_level * 2 + std::min(30, level) + bal_str;
    tmpparry += bonus_pb;

    int weapon_bonus = 0;
    int tmpskill;
    if (!has_weapon && raw_knowledge_natural_attack == 0) {
        return tmpparry + bonus / 2;
    } else if (!has_weapon && raw_knowledge_natural_attack > 0) {
        tmpskill = raw_skill_natural_attack;
    } else {
        weapon_bonus = weapon_value1;
        tmpskill = weapon_skill_base;
        if (is_bow) {
            tmpskill = archery_skill_raw;
        }
        if (is_twohanded) {
            tmpskill = (tmpskill + twohanded_knowledge_raw) / 2;
            if (is_bow) {
                tmpskill = (tmpskill + archery_skill_raw) / 2;
            }
        }
    }

    tmpskill = (tmpskill + 3 * parry_knowledge_raw) / 4;
    if (tactics_code == TACTICS_BERSERK) {
        tmpskill /= 2;
    }

    int tactics = 0;
    switch (tactics_code) {
    case TACTICS_DEFENSIVE:
        tmpparry += bonus / 2 + 3 * bonus / 16;
        tactics = 4;
        break;
    case TACTICS_CAREFUL:
        tmpparry += bonus / 2 + bonus / 8;
        tactics = 6;
        break;
    case TACTICS_NORMAL:
        tmpparry += bonus / 2;
        tactics = 8;
        break;
    case TACTICS_AGGRESSIVE:
        tmpparry += bonus / 2 - bonus / 8;
        tactics = 10;
        break;
    case TACTICS_BERSERK:
        tmpparry += bonus / 2 - bonus / 8;
        tactics = 12;
        break;
    default:
        tmpparry += bonus / 2;
        tactics = 10;
        break;
    };

    tmpparry += tmpskill * (weapon_bonus + 20) * (14 - tactics) / 1000;
    if (is_twohanded) {
        tmpparry += weapon_bonus / 2;
    }

    if (is_confused) {
        tmpparry -= (confuse_modifier * 2 / 3);
    }

    int sun_mod = power_of_arda;
    if (sun_mod) {
        if (race_uruk)
            tmpparry = tmpparry * 9 / 10 - sun_mod;
        if (race_orc)
            tmpparry = tmpparry * 8 / 9 - sun_mod;
        if (race_magus)
            tmpparry = tmpparry * 9 / 10 - sun_mod;
        if (race_ologhai)
            tmpparry = tmpparry * 9 / 10 - sun_mod;
    }

    if (!can_see) {
        tmpparry -= 10;
    }

    return tmpparry;
}

double get_real_parry_player_double_transcription(
    int parry_stat, int warrior_level, int level, int bal_str, int bonus_pb, bool has_weapon,
    int raw_knowledge_natural_attack, int raw_skill_natural_attack, int weapon_value1,
    int weapon_skill_base, bool is_bow, bool is_twohanded, int archery_skill_raw,
    int twohanded_knowledge_raw, int parry_knowledge_raw, int tactics_code, bool is_confused,
    int confuse_modifier, int power_of_arda, bool race_uruk, bool race_orc, bool race_magus,
    bool race_ologhai, bool can_see) {
    double tmpparry = parry_stat;
    double bonus = warrior_level * 2.0 + std::min(30, level) + bal_str;
    tmpparry += bonus_pb;

    double weapon_bonus = 0.0;
    double tmpskill;
    if (!has_weapon && raw_knowledge_natural_attack == 0) {
        return tmpparry + bonus / 2.0;
    } else if (!has_weapon && raw_knowledge_natural_attack > 0) {
        tmpskill = raw_skill_natural_attack;
    } else {
        weapon_bonus = weapon_value1;
        tmpskill = weapon_skill_base;
        if (is_bow) {
            tmpskill = archery_skill_raw;
        }
        if (is_twohanded) {
            tmpskill = (tmpskill + twohanded_knowledge_raw) / 2.0;
            if (is_bow) {
                tmpskill = (tmpskill + archery_skill_raw) / 2.0;
            }
        }
    }

    tmpskill = (tmpskill + 3.0 * parry_knowledge_raw) / 4.0;
    if (tactics_code == TACTICS_BERSERK) {
        tmpskill /= 2.0;
    }

    double tactics = 0.0;
    switch (tactics_code) {
    case TACTICS_DEFENSIVE:
        tmpparry += bonus / 2.0 + (3.0 * bonus) / 16.0;
        tactics = 4.0;
        break;
    case TACTICS_CAREFUL:
        tmpparry += bonus / 2.0 + bonus / 8.0;
        tactics = 6.0;
        break;
    case TACTICS_NORMAL:
        tmpparry += bonus / 2.0;
        tactics = 8.0;
        break;
    case TACTICS_AGGRESSIVE:
        tmpparry += bonus / 2.0 - bonus / 8.0;
        tactics = 10.0;
        break;
    case TACTICS_BERSERK:
        tmpparry += bonus / 2.0 - bonus / 8.0;
        tactics = 12.0;
        break;
    default:
        tmpparry += bonus / 2.0;
        tactics = 10.0;
        break;
    };

    tmpparry += (tmpskill * (weapon_bonus + 20.0) * (14.0 - tactics)) / 1000.0;
    if (is_twohanded) {
        tmpparry += weapon_bonus / 2.0;
    }

    if (is_confused) {
        tmpparry -= (confuse_modifier * 2.0) / 3.0;
    }

    int sun_mod = power_of_arda;
    if (sun_mod) {
        if (race_uruk)
            tmpparry = (tmpparry * 9.0) / 10.0 - sun_mod;
        if (race_orc)
            tmpparry = (tmpparry * 8.0) / 9.0 - sun_mod;
        if (race_magus)
            tmpparry = (tmpparry * 9.0) / 10.0 - sun_mod;
        if (race_ologhai)
            tmpparry = (tmpparry * 9.0) / 10.0 - sun_mod;
    }

    if (!can_see) {
        tmpparry -= 10.0;
    }

    return tmpparry;
}

TEST(GetRealParryFamily, PairedBoundV1OneHandedNormalTactics) {
    // Census: 3 eliminated divisions (tmpskill's /4, tactics NORMAL's /2,
    // the final /1000) + 1 = 4.
    const int ref = get_real_parry_player_int_reference(
        20, 30, 30, 20, 5, true, 0, 0, 10, 80, false, false, 0, 0, 60, TACTICS_NORMAL, false, 0, 0,
        false, false, false, false, true);
    const double dbl = get_real_parry_player_double_transcription(
        20, 30, 30, 20, 5, true, 0, 0, 10, 80, false, false, 0, 0, 60, TACTICS_NORMAL, false, 0, 0,
        false, false, false, false, true);
    EXPECT_LE(std::abs(rots::fp::to_game_int(dbl) - ref), 4);
}

TEST(GetRealParryFamily, PairedBoundV2Twohanded) {
    // Adds the twohanded skill-averaging /2 and the weapon_bonus/2 final add:
    // 5 divisions + 1 = 6 (conservative vs. the census's shorthand "4 -> 5";
    // this file's own line-by-line count finds a second /2 site the census
    // prose didn't spell out -- using the larger, still-safe bound per the
    // BOUND=divisions+1 method).
    const int ref = get_real_parry_player_int_reference(
        20, 30, 30, 20, 5, true, 0, 0, 10, 80, false, true, 0, 0, 60, TACTICS_NORMAL, false, 0, 0,
        false, false, false, false, true);
    const double dbl = get_real_parry_player_double_transcription(
        20, 30, 30, 20, 5, true, 0, 0, 10, 80, false, true, 0, 0, 60, TACTICS_NORMAL, false, 0, 0,
        false, false, false, false, true);
    EXPECT_LE(std::abs(rots::fp::to_game_int(dbl) - ref), 6);
}

TEST(GetRealParryFamily, PairedBoundV3OrcPowerOfArda) {
    const int ref = get_real_parry_player_int_reference(
        20, 30, 30, 20, 5, true, 0, 0, 10, 80, false, false, 0, 0, 60, TACTICS_NORMAL, false, 0, 10,
        false, true, false, false, true);
    const double dbl = get_real_parry_player_double_transcription(
        20, 30, 30, 20, 5, true, 0, 0, 10, 80, false, false, 0, 0, 60, TACTICS_NORMAL, false, 0, 10,
        false, true, false, false, true);
    EXPECT_LE(std::abs(rots::fp::to_game_int(dbl) - ref), 5);
}

TEST(GetRealParryFamily, ExactValueV1) {
    // Hand-derived: bonus = 30*2+30+20 = 110. tmpparry=20+5=25. Weapon
    // branch: tmpskill=80. tmpskill=(80+3*60)/4=(80+180)/4=260/4=65.
    // TACTICS_NORMAL: tmpparry += 110/2=55 -> 80, tactics=8.
    // tmpparry += 65*(10+20)*(14-8)/1000 = 65*30*6/1000 = 11700/1000 = 11
    // (int truncates) -> 91. No confuse/arda/CAN_SEE malus.
    const int ref = get_real_parry_player_int_reference(
        20, 30, 30, 20, 5, true, 0, 0, 10, 80, false, false, 0, 0, 60, TACTICS_NORMAL, false, 0, 0,
        false, false, false, false, true);
    EXPECT_EQ(ref, 91);

    // Double: 11700.0/1000.0 = 11.7 exactly -> 80+11.7 = 91.7 -> rounds to 92.
    const double dbl = get_real_parry_player_double_transcription(
        20, 30, 30, 20, 5, true, 0, 0, 10, 80, false, false, 0, 0, 60, TACTICS_NORMAL, false, 0, 0,
        false, false, false, false, true);
    EXPECT_EQ(rots::fp::to_game_int(dbl), 92);
}

// ===========================================================================
// Family 2 -- get_real_dodge() (B10). char_utils_combat.cpp:384-431, player
// (!IS_NPC) branch.
// ===========================================================================

int get_real_dodge_player_int_reference(int dodge_skill, int stealth_skill, int ranger_level,
                                        int dodge_penalty, bool race_beorning, int tactics_code,
                                        bool is_confused, int confuse_modifier, int power_of_arda,
                                        bool race_uruk, bool race_orc, bool race_magus,
                                        bool race_ologhai, int dodge_stat, int dex) {
    int dodge = ((dodge_skill + stealth_skill / 2 + 60) * ranger_level / 200 +
                 (dodge_skill + stealth_skill / 4) / 20);
    dodge -= dodge_penalty;
    dodge += 3;

    if (race_beorning) {
        dodge += 20;
    }

    if (tactics_code == TACTICS_BERSERK)
        dodge /= 2;

    if (is_confused)
        dodge -= (confuse_modifier * 2 / 3);

    int sun_mod = power_of_arda;
    if (sun_mod) {
        if (race_uruk)
            dodge = dodge * 9 / 10 - sun_mod;
        if (race_orc)
            dodge = dodge * 8 / 9 - sun_mod;
        if (race_magus)
            dodge = dodge * 9 / 10 - sun_mod;
        if (race_ologhai)
            dodge = dodge * 9 / 10 - sun_mod;
    }

    switch (tactics_code) {
    case TACTICS_DEFENSIVE:
        return (dodge + dodge_stat + 6) + dex;
    case TACTICS_CAREFUL:
        return (dodge + dodge_stat + 4) + dex;
    case TACTICS_NORMAL:
        return (dodge + dodge_stat) + dex;
    case TACTICS_AGGRESSIVE:
        return (dodge + dodge_stat - 4) + dex;
    case TACTICS_BERSERK:
        return (dodge + dodge_stat - 4) + dex / 2;
    default:
        return (dodge + dodge_stat + dex);
    };
}

double get_real_dodge_player_double_transcription(
    int dodge_skill, int stealth_skill, int ranger_level, int dodge_penalty, bool race_beorning,
    int tactics_code, bool is_confused, int confuse_modifier, int power_of_arda, bool race_uruk,
    bool race_orc, bool race_magus, bool race_ologhai, int dodge_stat, int dex) {
    double dodge = ((dodge_skill + stealth_skill / 2.0 + 60.0) * ranger_level) / 200.0 +
                   (dodge_skill + stealth_skill / 4.0) / 20.0;
    dodge -= dodge_penalty;
    dodge += 3.0;

    if (race_beorning) {
        dodge += 20.0;
    }

    if (tactics_code == TACTICS_BERSERK)
        dodge /= 2.0;

    if (is_confused)
        dodge -= (confuse_modifier * 2.0) / 3.0;

    int sun_mod = power_of_arda;
    if (sun_mod) {
        if (race_uruk)
            dodge = (dodge * 9.0) / 10.0 - sun_mod;
        if (race_orc)
            dodge = (dodge * 8.0) / 9.0 - sun_mod;
        if (race_magus)
            dodge = (dodge * 9.0) / 10.0 - sun_mod;
        if (race_ologhai)
            dodge = (dodge * 9.0) / 10.0 - sun_mod;
    }

    switch (tactics_code) {
    case TACTICS_DEFENSIVE:
        return (dodge + dodge_stat + 6.0) + dex;
    case TACTICS_CAREFUL:
        return (dodge + dodge_stat + 4.0) + dex;
    case TACTICS_NORMAL:
        return (dodge + dodge_stat) + dex;
    case TACTICS_AGGRESSIVE:
        return (dodge + dodge_stat - 4.0) + dex;
    case TACTICS_BERSERK:
        return (dodge + dodge_stat - 4.0) + dex / 2.0;
    default:
        return (dodge + dodge_stat + dex);
    };
}

TEST(GetRealDodgeFamily, PairedBoundV1NormalTactics) {
    // This file's own line-by-line count finds 4 eliminated divisions
    // (STEALTH/2, the outer /200, STEALTH/4, the outer /20 -- the census
    // prose's "3 typical" undercounts the STEALTH/2 term) + 1 = 5.
    const int ref = get_real_dodge_player_int_reference(60, 40, 20, 2, false, TACTICS_NORMAL, false,
                                                        0, 0, false, false, false, false, 10, 20);
    const double dbl = get_real_dodge_player_double_transcription(
        60, 40, 20, 2, false, TACTICS_NORMAL, false, 0, 0, false, false, false, false, 10, 20);
    EXPECT_LE(std::abs(rots::fp::to_game_int(dbl) - ref), 5);
}

TEST(GetRealDodgeFamily, PairedBoundV2Berserk) {
    const int ref = get_real_dodge_player_int_reference(
        60, 40, 20, 2, false, TACTICS_BERSERK, false, 0, 0, false, false, false, false, 10, 20);
    const double dbl = get_real_dodge_player_double_transcription(
        60, 40, 20, 2, false, TACTICS_BERSERK, false, 0, 0, false, false, false, false, 10, 20);
    EXPECT_LE(std::abs(rots::fp::to_game_int(dbl) - ref), 6);
}

TEST(GetRealDodgeFamily, PairedBoundV3OrcPowerOfArda) {
    const int ref = get_real_dodge_player_int_reference(60, 40, 20, 2, false, TACTICS_NORMAL, false,
                                                        0, 10, false, true, false, false, 10, 20);
    const double dbl = get_real_dodge_player_double_transcription(
        60, 40, 20, 2, false, TACTICS_NORMAL, false, 0, 10, false, true, false, false, 10, 20);
    EXPECT_LE(std::abs(rots::fp::to_game_int(dbl) - ref), 6);
}

TEST(GetRealDodgeFamily, ExactValueV1) {
    // Hand-derived: dodge = (60+40/2+60)*20/200 + (60+40/4)/20 =
    // (60+20+60)*20/200 + (60+10)/20 = (140*20)/200 + 70/20 =
    // 2800/200 + 3(int) = 14 + 3 = 17. -2(penalty)+3 = 18.
    // TACTICS_NORMAL: return (18+10)+20 = 48.
    const int ref = get_real_dodge_player_int_reference(60, 40, 20, 2, false, TACTICS_NORMAL, false,
                                                        0, 0, false, false, false, false, 10, 20);
    EXPECT_EQ(ref, 48);

    // Double: (140.0*20.0)/200.0 + 70.0/20.0 = 14.0+3.5 = 17.5. -2+3=18.5.
    // TACTICS_NORMAL: (18.5+10)+20 = 48.5 -> rounds to 49.
    const double dbl = get_real_dodge_player_double_transcription(
        60, 40, 20, 2, false, TACTICS_NORMAL, false, 0, 0, false, false, false, false, 10, 20);
    EXPECT_EQ(rots::fp::to_game_int(dbl), 49);
}

// ===========================================================================
// Family 3 -- hit() core damage formula (B11). fight.cpp:2680,2688,2695.
// damage_roll is taken post-weapon_master.do_on_damage_rolled(...) (an
// unconverted sub-call) as a plain int input, matching the census.
// ===========================================================================

int hit_dam_int_reference(int dam_in, bool is_npc, int get_damage, int ob, int damage_roll,
                          bool is_twohanded, int bal_str) {
    int dam = dam_in;
    if (is_npc) {
        dam /= 2;
    }
    dam += get_damage * 10;
    dam = (dam * (ob + 100) *
           (10000 + (damage_roll * damage_roll) + (is_twohanded ? 2 : 1) * 133 * bal_str)) /
          13300000;
    return dam;
}

double hit_dam_double_transcription(int dam_in, bool is_npc, int get_damage, int ob,
                                    int damage_roll, bool is_twohanded, int bal_str) {
    double dam = dam_in;
    if (is_npc) {
        dam /= 2.0;
    }
    dam += get_damage * 10.0;
    dam = (dam * (ob + 100) *
           (10000.0 + (damage_roll * damage_roll) + (is_twohanded ? 2 : 1) * 133.0 * bal_str)) /
          13300000.0;
    return dam;
}

TEST(HitDamFamily, PairedBoundV1Player) {
    // Census bound: 1 eliminated division (the final /13300000) + 1 = 2.
    const int ref = hit_dam_int_reference(30, false, 5, 50, 50, false, 20);
    const double dbl = hit_dam_double_transcription(30, false, 5, 50, 50, false, 20);
    EXPECT_LE(std::abs(rots::fp::to_game_int(dbl) - ref), 2);
}

TEST(HitDamFamily, PairedBoundV2MobTwohanded) {
    // Census bound: adds the mob dam/=2 division -> 2 divisions + 1 = 3.
    const int ref = hit_dam_int_reference(30, true, 5, 50, 100, true, 20);
    const double dbl = hit_dam_double_transcription(30, true, 5, 50, 100, true, 20);
    EXPECT_LE(std::abs(rots::fp::to_game_int(dbl) - ref), 3);
}

TEST(HitDamFamily, PairedBoundV3DamageRollZeroExtreme) {
    const int ref = hit_dam_int_reference(30, false, 5, 50, 0, false, 20);
    const double dbl = hit_dam_double_transcription(30, false, 5, 50, 0, false, 20);
    EXPECT_LE(std::abs(rots::fp::to_game_int(dbl) - ref), 2);
}

TEST(HitDamFamily, ExactValueV1) {
    // Hand-derived: dam=30 (not npc). dam += 5*10=50 -> 80.
    // dam = 80*(50+100)*(10000+2500+1*133*20)/13300000
    //     = 80*150*(10000+2500+2660)/13300000 = 12000*15160/13300000
    //     = 181920000/13300000 = 13.6... -> int truncates to 13.
    const int ref = hit_dam_int_reference(30, false, 5, 50, 50, false, 20);
    EXPECT_EQ(ref, 13);

    // Double: 181920000.0/13300000.0 = 13.6781954... -> rounds to 14.
    const double dbl = hit_dam_double_transcription(30, false, 5, 50, 50, false, 20);
    EXPECT_EQ(rots::fp::to_game_int(dbl), 14);
}

// ===========================================================================
// Family 3 -- natural_attack_dam() (B12). fight.cpp:2519-2538.
// ===========================================================================

int natural_attack_dam_int_reference(bool has_natural_attack_skill, int level, int warrior_level,
                                     int str, bool is_light_fighting, bool is_wild_fighting) {
    if (!has_natural_attack_skill) {
        return BAREHANDED_DAMAGE * 10;
    }
    int level_factor = level;
    level_factor = level_factor / 3;
    int warrior_factor = warrior_level;
    int str_factor = str;
    int dam = level_factor + str_factor + warrior_factor;
    if (!is_light_fighting && level > 11) {
        if (is_wild_fighting) {
            dam = (int)((double)dam * 0.75);
        } else {
            dam = (int)((double)dam * 0.50);
        }
    }
    return dam;
}

double natural_attack_dam_double_transcription(bool has_natural_attack_skill, int level,
                                               int warrior_level, int str, bool is_light_fighting,
                                               bool is_wild_fighting) {
    if (!has_natural_attack_skill) {
        return BAREHANDED_DAMAGE * 10.0;
    }
    double level_factor = level / 3.0;
    double warrior_factor = warrior_level;
    double str_factor = str;
    double dam = level_factor + str_factor + warrior_factor;
    if (!is_light_fighting && level > 11) {
        dam = is_wild_fighting ? dam * 0.75 : dam * 0.50;
    }
    return dam;
}

TEST(NaturalAttackDamFamily, PairedBoundV1Wild) {
    // Census bound: 1 eliminated division (level/3) folded with the two old
    // (int)(double) casts + 1 = 3.
    const int ref = natural_attack_dam_int_reference(true, 30, 15, 20, false, true);
    const double dbl = natural_attack_dam_double_transcription(true, 30, 15, 20, false, true);
    EXPECT_LE(std::abs(rots::fp::to_game_int(dbl) - ref), 3);
}

TEST(NaturalAttackDamFamily, PairedBoundV2NonWildSpec) {
    const int ref = natural_attack_dam_int_reference(true, 30, 15, 20, false, false);
    const double dbl = natural_attack_dam_double_transcription(true, 30, 15, 20, false, false);
    EXPECT_LE(std::abs(rots::fp::to_game_int(dbl) - ref), 3);
}

TEST(NaturalAttackDamFamily, PairedBoundV3LevelAtOrBelowElevenNoScaling) {
    const int ref = natural_attack_dam_int_reference(true, 9, 15, 20, false, false);
    const double dbl = natural_attack_dam_double_transcription(true, 9, 15, 20, false, false);
    EXPECT_LE(std::abs(rots::fp::to_game_int(dbl) - ref), 3);
}

TEST(NaturalAttackDamFamily, PairedBoundV4NoSkillBarehanded) {
    // Exact (BAREHANDED_DAMAGE*10, no arithmetic at all): bound 0.
    const int ref = natural_attack_dam_int_reference(false, 30, 15, 20, false, false);
    const double dbl = natural_attack_dam_double_transcription(false, 30, 15, 20, false, false);
    EXPECT_EQ(rots::fp::to_game_int(dbl), ref);
}

TEST(NaturalAttackDamFamily, ExactValueV1Wild) {
    // Hand-derived: level_factor=30/3=10 (exact). dam=10+20+15=45.
    // level>11 and WildFighting: dam=(int)(45.0*0.75)=(int)33.75=33.
    const int ref = natural_attack_dam_int_reference(true, 30, 15, 20, false, true);
    EXPECT_EQ(ref, 33);

    // Double: level_factor=10.0 (exact -- 30/3.0). dam=45.0*0.75=33.75 -> rounds to 34.
    const double dbl = natural_attack_dam_double_transcription(true, 30, 15, 20, false, true);
    EXPECT_EQ(rots::fp::to_game_int(dbl), 34);
}

// ===========================================================================
// Family 3 -- get_weapon_damage() dam_coef (B13). char_utils_combat.cpp:
// 179-261 (the non-bow weapon-object chain; the bow early-return is a
// simple pass-through outside the boundary chain, per the census).
// str_speed/null_speed here are LOCAL to this function (a fixed 20/20
// str/dex baseline, not a character's actual stats), so no character
// parameters are needed beyond the weapon-object fields and its owner.
// ===========================================================================

int dam_coef_int_reference(int weapon_ob_coef, int weapon_parry_coef, int weapon_bulk,
                           int weapon_type_value3, int obj_level_raw, bool has_owner,
                           int owner_level, int owner_skill, int weight) {
    int parry_coef = weapon_parry_coef;
    int OB_coef = weapon_ob_coef;
    int obj_level = obj_level_raw;

    if (has_owner) {
        if (obj_level > (owner_level * 4 / 3 + 7))
            obj_level -= (obj_level - owner_level * 4 / 3 - 7) * 2 / 3;
        obj_level = obj_level * owner_skill / 100;
    }

    switch (weapon_type_value3) {
    case 2:
        parry_coef += 8;
        OB_coef -= 5;
        break;
    case 3:
    case 4:
        parry_coef -= 2;
        break;
    case 6:
    case 7:
        parry_coef += 3;
        break;
    }

    if (parry_coef < -7)
        parry_coef = parry_coef / 3 - 1;
    else if (parry_coef < 0)
        parry_coef = parry_coef / 2;

    if (parry_coef > 5)
        parry_coef = parry_coef * 2 - 5;

    if (OB_coef < -7)
        OB_coef = OB_coef / 2 - 1;
    else if (OB_coef < 0)
        OB_coef = OB_coef * 2 / 3;

    if (OB_coef > 40)
        OB_coef = 40;

    int dam_coef = (40 + obj_level - parry_coef) * (50 - OB_coef) * 4 / 3;
    dam_coef = dam_coef * (20 - std::abs(weapon_bulk - 3)) / 20;

    int null_speed = 225;
    int use_weight = (weight == 0) ? 1 : weight;

    int str_speed = 2 * 20 * 2500000 / (use_weight * (weapon_bulk + 3));

    int tmp = (1000000 / (1000000 / str_speed + 1000000 / (null_speed * null_speed)));

    int ene_regen = do_squareroot_table_int_reference(tmp / 100) / 20;
    dam_coef = dam_coef / ene_regen * 3;

    if (dam_coef > 70)
        dam_coef = 70 + (dam_coef - 70) * 3 / 4;

    if (dam_coef > 90)
        dam_coef = 90 + (dam_coef - 90) * 3 / 4;

    return dam_coef;
}

double dam_coef_double_transcription(int weapon_ob_coef, int weapon_parry_coef, int weapon_bulk,
                                     int weapon_type_value3, int obj_level_raw, bool has_owner,
                                     int owner_level, int owner_skill, int weight) {
    double parry_coef = weapon_parry_coef;
    double OB_coef = weapon_ob_coef;
    double obj_level = obj_level_raw;

    if (has_owner) {
        if (obj_level > (owner_level * 4.0 / 3.0 + 7.0))
            obj_level -= (obj_level - owner_level * 4.0 / 3.0 - 7.0) * 2.0 / 3.0;
        obj_level = obj_level * owner_skill / 100.0;
    }

    switch (weapon_type_value3) {
    case 2:
        parry_coef += 8.0;
        OB_coef -= 5.0;
        break;
    case 3:
    case 4:
        parry_coef -= 2.0;
        break;
    case 6:
    case 7:
        parry_coef += 3.0;
        break;
    }

    if (parry_coef < -7.0)
        parry_coef = parry_coef / 3.0 - 1.0;
    else if (parry_coef < 0.0)
        parry_coef = parry_coef / 2.0;

    if (parry_coef > 5.0)
        parry_coef = parry_coef * 2.0 - 5.0;

    if (OB_coef < -7.0)
        OB_coef = OB_coef / 2.0 - 1.0;
    else if (OB_coef < 0.0)
        OB_coef = OB_coef * 2.0 / 3.0;

    if (OB_coef > 40.0)
        OB_coef = 40.0;

    double dam_coef = (40.0 + obj_level - parry_coef) * (50.0 - OB_coef) * 4.0 / 3.0;
    dam_coef = dam_coef * (20.0 - std::abs(weapon_bulk - 3)) / 20.0;

    double null_speed = 225.0;
    int use_weight = (weight == 0) ? 1 : weight;

    double str_speed = 2.0 * 20.0 * 2500000.0 / (use_weight * (weapon_bulk + 3));

    double tmp = (1000000.0 / (1000000.0 / str_speed + 1000000.0 / (null_speed * null_speed)));

    // Controller ruling option (a): the table-interp do_squareroot itself
    // stays UNCHANGED and is called with the same truncated-int argument
    // shape as the old code (tmp/100 -> static_cast<int> of the double
    // intermediate); only the truncation POINT moved later in the chain.
    double ene_regen = do_squareroot_table_int_reference(static_cast<int>(tmp / 100.0)) / 20.0;
    dam_coef = dam_coef / ene_regen * 3.0;

    if (dam_coef > 70.0)
        dam_coef = 70.0 + (dam_coef - 70.0) * 3.0 / 4.0;

    if (dam_coef > 90.0)
        dam_coef = 90.0 + (dam_coef - 90.0) * 3.0 / 4.0;

    return dam_coef;
}

TEST(DamCoefFamily, PairedBoundV1NoOwner) {
    // Census bound: ~9 eliminated divisions -> 10.
    const int ref = dam_coef_int_reference(10, 0, 3, 1, 40, false, 0, 0, 10);
    const double dbl = dam_coef_double_transcription(10, 0, 3, 1, 40, false, 0, 0, 10);
    EXPECT_LE(std::abs(rots::fp::to_game_int(dbl) - ref), 10);
}

TEST(DamCoefFamily, PairedBoundV2OwnerMalusBothCases) {
    // Owner level low enough to trigger BOTH owner-adjustment maluses.
    const int ref = dam_coef_int_reference(10, 0, 3, 1, 60, true, 20, 50, 10);
    const double dbl = dam_coef_double_transcription(10, 0, 3, 1, 60, true, 20, 50, 10);
    EXPECT_LE(std::abs(rots::fp::to_game_int(dbl) - ref), 10);
}

TEST(DamCoefFamily, PairedBoundV3BulkZero) {
    // bulk=0 -> |bulk-3|=3, exercising the abs() branch's other side.
    const int ref = dam_coef_int_reference(10, 0, 0, 1, 40, false, 0, 0, 10);
    const double dbl = dam_coef_double_transcription(10, 0, 0, 1, 40, false, 0, 0, 10);
    EXPECT_LE(std::abs(rots::fp::to_game_int(dbl) - ref), 10);
}

TEST(DamCoefFamily, PairedBoundCapBoundaryExactlyNinety) {
    // obj_level_raw=102 lands the int reference's PRE-cap dam_coef at exactly
    // 90 -- the TA5/TA7 tie-asymmetry boundary the census flags
    // (char_utils_combat.cpp:255/258): at exactly 90 the second soft-cap
    // does NOT engage (the compare is strict '>').
    const int ref = dam_coef_int_reference(10, 0, 3, 1, 102, false, 0, 0, 10);
    ASSERT_EQ(ref, 90);
    const double dbl = dam_coef_double_transcription(10, 0, 3, 1, 102, false, 0, 0, 10);
    EXPECT_LE(std::abs(rots::fp::to_game_int(dbl) - ref), 10);
}

TEST(DamCoefFamily, ExactValueV1NoOwner) {
    // Hand-derived (python-mirrored independently of this TU -- see the
    // task-1 report's verification script): with weapon_ob_coef=10,
    // weapon_parry_coef=0, bulk=3, type3=1 (no switch case), obj_level=40,
    // no owner, weight=10: OB_coef stays 10 (not <0, not >40); parry_coef
    // stays 0. dam_coef=(40+40-0)*(50-10)*4/3 = 80*40*4/3 = 12800/3 = 4266
    // (int). *(20-0)/20 = 4266 (unchanged, bulk==3). str_speed =
    // 2*20*2500000/(10*6) = 100000000/60 = 1666666. tmp =
    // 1000000/(1000000/1666666 + 1000000/50625) = 1000000/(0+19) =
    // 1000000/19 = 52631. ene_regen = do_squareroot_table(52631/100=526) is
    // clamped to i=170*4=680 since 526/4=131<=170 -- NOT clamped actually
    // (131<=170), so table lookup at index 131/132, giving a value; final
    // ene_regen/20 -- see the report for the fully-expanded arithmetic since
    // the square_root[] table lookup itself is exercised, not hand-derived
    // digit-by-digit here. This test pins the value this reference function
    // actually produces (independently cross-checked by the Python mirror,
    // not by re-running this C++ code), guarding against a transcription
    // regression.
    const int ref = dam_coef_int_reference(10, 0, 3, 1, 40, false, 0, 0, 10);
    EXPECT_EQ(ref, 54);
    const double dbl = dam_coef_double_transcription(10, 0, 3, 1, 40, false, 0, 0, 10);
    EXPECT_EQ(rots::fp::to_game_int(dbl), 58);
}

} // namespace
