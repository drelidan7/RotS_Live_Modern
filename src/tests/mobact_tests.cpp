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
#include "../comm.h" /* For register_virt_program_number_hook() -- RR3 T1b */
#include "../interpre.h"
#include "../script_hooks.h"
#include "../utils.h"
#include "rots/core/character.h"
#include "rots/core/room.h"
#include "test_placement.h"
#include "test_random_utils.h"
#include "test_world.h"

#include <gtest/gtest.h>

#include <optional>

void one_mobile_activity(char_data *ch);
// mobile_activity() has no header declaration anywhere in the tree (comm.cpp
// calls it from the pulse loop through a local extern) -- forward-declared
// here for RR Wave R3 Task 1b's dispatch-invariant pair at the end of this
// file, matching mobact.cpp:56's signature exactly.
void mobile_activity(void);

extern room_data world;
extern int top_of_world;
extern char_data *character_list;

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

    // Room 0's occupant chain, re-published by the seed_* helpers below
    // (LS-3a T3, test_placement.h). An optional rather than a plain member
    // because the chain's membership is per-test: the constructor emplaces
    // an EMPTY one (which saves the room's prior head and publishes an empty
    // chain, exactly what the raw save-then-null pair used to do), and each
    // seed_* re-emplaces over it -- destroying the previous instance, which
    // restores that same saved head, before the new one saves it again.
    // Declared last so it unwinds before the characters it manages.
    std::optional<ScopedRoomOccupants> occupants;

    MobactTestContext() {
        top_of_world = 0;
        room_by_id_total(0)->room_flags = 0;
        room_by_id_total(0)->light = 1; // Unlit rooms fail CAN_SEE's darkness check.
        occupants.emplace(room_by_id_total(0), 0, std::initializer_list<char_data*> {});

        // Common ch setup every test in this file needs: a plain awake NPC,
        // not fighting, no master/pet/guardian entanglements, MOB_SENTINEL
        // so the wandering-movement block's `number(0, 45)` roll never fires
        // (its `!IS_SET(..., MOB_SENTINEL)` guard short-circuits first).
        ch.specials2.act = MOB_ISNPC | MOB_SENTINEL;
        ch.specials.position = POSITION_STANDING;
        ch.player.level = 20;
        // `ch` is deliberately NOT one of its own room's occupants (see the
        // struct comment), so it is located, not published.
        set_location(&ch, 0);

        occupant_a.player.name = occupant_a_name;
        occupant_a.player.short_descr = occupant_a_name;
        occupant_a.specials.position = POSITION_STANDING;

        occupant_b.player.name = occupant_b_name;
        occupant_b.player.short_descr = occupant_b_name;
        occupant_b.specials.position = POSITION_STANDING;
    }

    // Wires occupant_a then occupant_b (in that order) into world[0]'s
    // occupant chain -- occupant_a is therefore the FIRST candidate any
    // converted forward walk over room_of(ch)'s occupants reaches.
    void seed_two_occupants() {
        occupants.emplace(room_by_id_total(0), 0,
                          std::initializer_list<char_data*> { &occupant_a, &occupant_b });
    }

    void seed_one_occupant() {
        occupants.emplace(room_by_id_total(0), 0,
                          std::initializer_list<char_data*> { &occupant_a });
    }

    // The head restore and both unlinks this destructor used to do are
    // `occupants`, which unwinds on its own -- nothing is left to do here.
    ~MobactTestContext() = default;
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

// ---------------------------------------------------------------------------
// RR Wave R3 Task 1b -- mobile_activity()'s dispatch-invariant guard (owner
// ruling R3-O-1; docs/superpowers/specs/2026-08-21-rr3-combat-design.md
// section 2).
//
// mobile_activity() walks the whole character_list once per pulse with no
// placement check on the walk itself and splits two ways (dispatch census
// M-5). The NPC arm needs no new guard: one_mobile_activity() carries a real
// entry guard of its own (mobact.cpp:106, which tests BOTH halves --
// `< 0` excludes the sentinel and `> top_of_world` establishes in-range) and
// every one of its 16 ledger sites is dominated by it. The PC/virt-program
// arm is NOT routed through that callee and inherits nothing, so it is the
// arm -- and the only arm -- this wave guards.
//
// That arm is also exactly where census P5's permanently-unplaced-NPC leak
// would surface for a non-NPC actor: `char_to_room(X, location_of(Y))` with
// an absent Y leaves X in character_list with no location, and for an NPC
// :106 already catches it.
//
// DISCRIMINATOR: a single non-NPC in character_list with a store_prog_number,
// and a recording stub installed behind script_hooks.h's
// virt_program_number seam. The `!number(0, 3)` sampling gate is pinned with
// the deterministic test-RNG hook (test_random_utils.h) so the walk reaches
// the arm on the one iteration this test drives.
// ---------------------------------------------------------------------------

namespace {

int g_virt_program_call_count = 0;

SPECIAL(recording_virt_program) {
    ++g_virt_program_call_count;
    return 0;
}

void *recording_virt_program_number(int /*number*/) {
    return reinterpret_cast<void *>(&recording_virt_program);
}

// Installs the recording virt-program lookup for a scope and restores
// spec_ass.cpp's real registration afterwards -- the same "swap out, restore
// via the real registrar" shape ScopedRecordingHitHook above uses.
struct ScopedRecordingVirtProgramHook {
    ScopedRecordingVirtProgramHook() {
        rots::script::set_virt_program_number_hook(recording_virt_program_number);
    }

    ~ScopedRecordingVirtProgramHook() { register_virt_program_number_hook(); }

    ScopedRecordingVirtProgramHook(const ScopedRecordingVirtProgramHook &) = delete;
    ScopedRecordingVirtProgramHook &operator=(const ScopedRecordingVirtProgramHook &) = delete;
};

// Publishes exactly one character as the whole process-global character_list
// for a scope, restoring the previous head afterwards -- mobile_activity()
// walks that global, so a test that wants a one-iteration walk has to own it.
struct ScopedSoleCharacterListMember {
    explicit ScopedSoleCharacterListMember(char_data *member)
        : saved_head(character_list) {
        member->next = nullptr;
        character_list = member;
    }

    ~ScopedSoleCharacterListMember() { character_list = saved_head; }

    ScopedSoleCharacterListMember(const ScopedSoleCharacterListMember &) = delete;
    ScopedSoleCharacterListMember &operator=(const ScopedSoleCharacterListMember &) = delete;

    char_data *saved_head;
};

} // namespace

TEST(MobileActivityDispatchInvariant, RefusesThePcVirtProgramArmForAnUnplacedActor) {
    ScopedTestWorld test_world;
    ScopedRecordingVirtProgramHook virt_program_hook;

    char_data ch{};
    ch.specials2.act = 0; // NOT MOB_ISNPC -- the PC/virt-program arm
    ch.specials.store_prog_number = 7;
    set_location(&ch, NOWHERE);

    ScopedSoleCharacterListMember sole_member(&ch);

    g_virt_program_call_count = 0;
    clear_test_random_values();
    push_test_random_value(0.0); // pins the walk's `!number(0, 3)` sampling gate

    mobile_activity();

    clear_test_random_values();

    EXPECT_EQ(g_virt_program_call_count, 0)
        << "Expected the dispatch-invariant guard to skip the PC/virt-program arm for an actor "
           "with no location -- one_mobile_activity()'s :106 guard does not cover this arm.";
}

TEST(MobileActivityDispatchInvariant, RunsThePcVirtProgramArmForAPlacedActor) {
    ScopedTestWorld test_world;
    ScopedRecordingVirtProgramHook virt_program_hook;

    char_data ch{};
    ch.specials2.act = 0;
    ch.specials.store_prog_number = 7;
    set_location(&ch, 0);

    ScopedSoleCharacterListMember sole_member(&ch);

    g_virt_program_call_count = 0;
    clear_test_random_values();
    push_test_random_value(0.0);

    mobile_activity();

    clear_test_random_values();

    EXPECT_EQ(g_virt_program_call_count, 1)
        << "Expected the identical fixture with a PLACED actor to reach the registered virt "
           "program -- the guard must not block a normal pulse.";
}
