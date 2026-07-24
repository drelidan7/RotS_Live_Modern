// Coverage rider for the LS-1 Wave (Tranche C) script batch's mobact.cpp
// sub-commit. one_mobile_activity() had zero behavioral coverage anywhere
// in the tree before this rider (combat_hooks_tests.cpp/gtest_main.cpp only
// exercise the combat_hooks.h dispatch *hook*, never the real function
// body). Two converted walks get targeted tests here:
//
//  - The "Race aggressions" walk (census Family F site, originally
//    `for (tmp_ch = world[ch->in_room].people; tmp_ch; tmp_ch =
//    tmp_ch->next_in_room) if (...) { ...; break; } if (tmp_ch) return;`) --
//    tmp_ch is read AFTER the loop, so the conversion needed the
//    found=nullptr pre-init recipe.
//  - The "Standard aggressive mobs" walk, which is NOT one of the census's
//    named flag families but needed the same scrutiny: its original loop
//    condition was `tmp_ch && !found` (not a `break`), so a naive
//    occupants() conversion would have kept scanning past the first
//    non-MOB_SWITCHING match and let a LATER occupant silently overwrite
//    `vict` -- changing which occupant gets attacked. The conversion added
//    an explicit `if (found) break;` guard to reproduce the original
//    first-match-wins semantics; this test is the regression check that
//    the guard actually works (proves the FIRST eligible occupant is
//    targeted, not the second).
//
// Both tests drive the real one_mobile_activity(ch) body directly (no
// hook indirection) and observe the outcome by substituting a recording
// stub for the `hit` cell of combat_hooks.h's issue_command() dispatch
// table (same substitution technique combat_hooks_tests.cpp uses), since
// `vict`/`tmp_ch` are locals with no other externally observable effect.

#include "../combat_hooks.h"
#include "../interpre.h"
#include "../utils.h"
#include "rots/core/character.h"
#include "rots/core/room.h"
#include "test_world.h"

#include <gtest/gtest.h>

void one_mobile_activity(char_data *ch);

extern room_data world;
extern int top_of_world;

namespace {

struct RecordedHitCall {
    char_data *target = nullptr;
    bool called = false;
};

RecordedHitCall g_recorded_hit_call;

void recording_hit_stub(char_data * /*ch*/, char * /*argument*/, waiting_type *wtl, int /*cmd*/,
                        int /*subcmd*/) {
    g_recorded_hit_call = RecordedHitCall{wtl ? wtl->targ1.ptr.ch : nullptr, true};
}

// Temporarily substitutes a recording stub for combat_hooks.h's `hit` cell
// (every other cell keeps its real registration), restoring the full
// dispatch table via register_combat_command_dispatch() on scope exit --
// same "swap one cell, restore via the real registrar" shape
// combat_hooks_tests.cpp's ScopedUnregisteredCombatCommand uses.
struct ScopedRecordingHitHook {
    ScopedRecordingHitHook() {
        rots::combat::set_combat_command(rots::combat::combat_command::hit, recording_hit_stub);
    }

    ~ScopedRecordingHitHook() { register_combat_command_dispatch(); }

    ScopedRecordingHitHook(const ScopedRecordingHitHook &) = delete;
    ScopedRecordingHitHook &operator=(const ScopedRecordingHitHook &) = delete;
};

// A one-room world with `ch` (the acting NPC) present but NOT itself a
// member of the room's occupant chain (one_mobile_activity()'s converted
// walks only ever compare occupants against ch by pointer, never assume ch
// is one of its own room's occupants) plus up to two additional occupants
// wired into world[0].people the same way DamageTestContext/mystic_tests.cpp
// wire theirs.
struct MobactTestContext {
    ScopedTestWorld test_world{1};
    char_data ch{};
    char_data occupant_a{};
    char_data occupant_b{};
    char occupant_a_name[16] = "occupant_a";
    char occupant_b_name[16] = "occupant_b";
    char_data *original_people = nullptr;

    MobactTestContext() {
        top_of_world = 0;
        world[0].room_flags = 0;
        world[0].light = 1; // Unlit rooms fail CAN_SEE's darkness check.
        original_people = world[0].people;
        world[0].people = nullptr;

        // Common ch setup every test in this file needs: a plain awake NPC,
        // not fighting, no master/pet/guardian entanglements, MOB_SENTINEL
        // so the wandering-movement block's `number(0, 45)` roll never fires
        // (its `!IS_SET(..., MOB_SENTINEL)` guard short-circuits first).
        ch.specials2.act = MOB_ISNPC | MOB_SENTINEL;
        ch.specials.position = POSITION_STANDING;
        ch.player.level = 20;
        ch.in_room = 0;

        occupant_a.player.name = occupant_a_name;
        occupant_a.player.short_descr = occupant_a_name;
        occupant_a.specials.position = POSITION_STANDING;
        occupant_a.in_room = 0;

        occupant_b.player.name = occupant_b_name;
        occupant_b.player.short_descr = occupant_b_name;
        occupant_b.specials.position = POSITION_STANDING;
        occupant_b.in_room = 0;
    }

    // Wires occupant_a then occupant_b (in that order) into world[0]'s
    // occupant chain -- occupant_a is therefore the FIRST candidate any
    // converted forward walk over room_of(ch)'s occupants reaches.
    void seed_two_occupants() {
        occupant_a.next_in_room = &occupant_b;
        occupant_b.next_in_room = nullptr;
        world[0].people = &occupant_a;
    }

    void seed_one_occupant() {
        occupant_a.next_in_room = nullptr;
        world[0].people = &occupant_a;
    }

    ~MobactTestContext() {
        world[0].people = original_people;
        occupant_a.next_in_room = nullptr;
        occupant_b.next_in_room = nullptr;
    }
};

} // namespace

TEST(MobactRaceAggression, TargetsTheMatchingRaceOccupantAndStops) {
    MobactTestContext context;
    ScopedRecordingHitHook hook;
    g_recorded_hit_call = RecordedHitCall{};

    // IS_AGGR_TO(ch, vict) := ch->specials2.pref & (1 << GET_RACE(vict)).
    context.ch.specials2.pref = (1 << RACE_HUMAN);
    context.occupant_a.player.race = RACE_HARAD; // non-matching -- must be skipped.
    context.occupant_b.player.race = RACE_HUMAN; // matching -- must be targeted.
    context.seed_two_occupants();

    one_mobile_activity(&context.ch);

    EXPECT_TRUE(g_recorded_hit_call.called)
        << "Expected the converted race-aggression walk (occupants(room_of(ch))) to reach the "
           "matching-race occupant and issue a hit.";
    EXPECT_EQ(g_recorded_hit_call.target, &context.occupant_b)
        << "Expected the race-matching occupant (found=nullptr pre-init, then found=occ on match) "
           "to be the hit target.";
}

TEST(MobactRaceAggression, DoesNothingWhenNoOccupantMatchesTheAggressionMask) {
    MobactTestContext context;
    ScopedRecordingHitHook hook;
    g_recorded_hit_call = RecordedHitCall{};

    context.ch.specials2.pref = (1 << RACE_HUMAN);
    context.occupant_a.player.race = RACE_HARAD; // non-matching.
    context.seed_one_occupant();

    one_mobile_activity(&context.ch);

    EXPECT_FALSE(g_recorded_hit_call.called)
        << "Expected the race-aggression walk to leave `tmp_ch` at its pre-init nullptr (no match "
           "found) and never call issue_command(hit, ...).";
}

TEST(MobactStandardAggressive, TargetsTheFirstEligibleOccupantNotALaterOne) {
    MobactTestContext context;
    ScopedRecordingHitHook hook;
    g_recorded_hit_call = RecordedHitCall{};

    // No race-aggression mask, so the "Race aggressions" block above this
    // one is skipped entirely (`if (ch->specials2.pref)` is false).
    context.ch.specials2.pref = 0;
    context.ch.specials2.act |= MOB_AGGRESSIVE;
    // Neither MOB_AGGRESSIVE_EVIL/GOOD/NEUTRAL is set on ch, so the
    // alignment-class disjunction's final `(!EVIL && !NEUTRAL && !GOOD)`
    // clause is unconditionally true -- both occupants qualify regardless
    // of alignment, isolating this test to the found/break regression.
    // Both occupants are non-NPC (IS_NPC() false, the walk's own
    // `!IS_NPC(tmp_ch)` requirement) and not MOB_SWITCHING, so the ELSE
    // branch (`vict = tmp_ch; found = TRUE;`) fires on the FIRST eligible
    // occupant -- the branch a missing `if (found) break;` guard would let
    // a second eligible occupant silently overwrite.
    context.seed_two_occupants();

    one_mobile_activity(&context.ch);

    EXPECT_TRUE(g_recorded_hit_call.called);
    EXPECT_EQ(g_recorded_hit_call.target, &context.occupant_a)
        << "Expected the FIRST eligible occupant to win (matching the original `tmp_ch && !found` "
           "loop condition's first-match-then-stop semantics) -- a missing `if (found) break;` "
           "guard would let occupant_b's later iteration silently overwrite `vict` instead.";
}
