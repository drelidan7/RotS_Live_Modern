// mystic_tests.cpp

// New test TU (LS-1 Wave Task 2, combat-tranche-B coverage rider -- census's
// Step 7 explicit flag for combat/mystic.cpp: "confirm the room_target/
// haze/scan paths that convert are reached by existing combat tests; add
// riders for any converted walk with no coverage." Controller review of the
// grouped small-TU sub-commit (1eee40c) confirmed spell_terror()'s
// next_in_room -> rots::entity::occupants() walk conversion (mystic.cpp,
// originally `for (tmpch = world[caster->in_room].people; tmpch; tmpch =
// tmpch->next_in_room)`) had zero behavioral coverage anywhere in the tree
// -- tests/spell_registry_tests.cpp only checks that spell_terror is
// registered in skills[], never calls it. No mystic_tests.cpp existed
// before this file.
//
// The expected affect/message outcomes below are hand-derived from the
// PRE-CONVERSION walk (git show 5d69121:src/combat/mystic.cpp, the commit
// immediately before combat tranche B started), not from the converted
// code under test -- so this test pins the ORIGINAL semantics the
// conversion must preserve, rather than merely re-describing the new
// implementation:
//
//   for (tmpch = world[caster->in_room].people; tmpch; tmpch = tmpch->next_in_room) {
//       if ((tmpch != caster) && !affected_by_spell(tmpch, SPELL_FEAR)) {
//           if (!saves_mystic(tmpch) && !saves_leadership(tmpch)) {
//               af.type = SPELL_FEAR; af.duration = level; af.modifier = level + 10;
//               af.location = APPLY_NONE; af.bitvector = 0;
//               affect_to_char(tmpch, &af);
//               act("$n suddenly breathes an icy, cold breath everywhere. "
//                   "Terror overcomes you.", FALSE, caster, 0, tmpch, TO_VICT);
//           } else
//               act("$n suddenly breathes an icy, cold breath. You ignore it.",
//                   FALSE, caster, 0, tmpch, TO_VICT);
//       }
//   }
//
// saves_mystic(ch) (char_utils_combat.cpp) is `number(0,100) <= GET_PERCEPTION(ch)*9/10`;
// saves_leadership(victim) is `saves_mystic(victim)` again (a SEPARATE,
// independent number(0,100) draw), falling back to a master/mount check
// only if that second roll also fails. This fixture uses the deterministic
// test-RNG hook (test_random_utils.h) to pin both outcomes: a
// zero-perception victim (defense 0) fails both draws unconditionally for
// any non-zero queued roll, landing the fear affect; a full-perception
// victim (defense 90) saves on the very first draw with a queued roll of
// 0.0, short-circuiting before saves_leadership() is ever called. Both
// victims and the caster are NPCs (IS_NPC() true) so get_prof_level()/
// get_specialization() fast-path past the profs pointer entirely --
// get_mystic_caster_level() reads caster.player.level directly, and
// utils::get_specialization() returns game_types::PS_None (never
// PS_Illusion, so no +6 level bonus applies).

#include "../handler.h"
#include "../spells.h"
#include "../utils.h"
#include "rots/core/character.h"
#include "rots/core/descriptor.h"
#include "rots/core/room.h"
#include "test_random_utils.h"
#include "test_world.h"

#include <gtest/gtest.h>

#include <string>

namespace {

// Mirrors act_format_tests.cpp's/olog_hai_tests.cpp's own per-file copy of
// this helper (no shared header declares it): points a descriptor's output
// at its OWN small_outbuf so send_to_char()/act() output can be inspected
// directly instead of going to a real socket. CRITICAL: mutates the
// caller's descriptor_data in place -- never replace with a version that
// returns a descriptor_data by value (descriptor_data::output is a
// self-pointer into small_outbuf[]; see act_format_tests.cpp's fuller note
// on this hazard).
void reset_capturing_descriptor(descriptor_data& descriptor, char_data* character)
{
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = 0; // CON_PLAYING
    descriptor.character = character;
}

// Three NPCs sharing room 0 of a single-room ScopedTestWorld, wired
// directly into the occupant chain (people -> caster -> victim_fails ->
// victim_saves) so the walk order the converted occupants() range-for
// visits is pinned exactly, matching the original next_in_room chain
// order byte-for-byte. All three are MOB_ISNPC so get_prof_level()/
// get_specialization() bypass the profs pointer (never allocated here).
struct MysticTerrorContext {
    ScopedTestWorld test_world;
    char_data caster {};
    char_data victim_fails {}; // perception 0 -> fails both saves -> gets feared
    char_data victim_saves {}; // perception 100 -> saves on the first roll -> ignores
    descriptor_data caster_descriptor {};
    descriptor_data victim_fails_descriptor {};
    descriptor_data victim_saves_descriptor {};
    char caster_short_descr[24] = "A grim mystic";

    MysticTerrorContext()
    {
        reset_capturing_descriptor(caster_descriptor, &caster);
        reset_capturing_descriptor(victim_fails_descriptor, &victim_fails);
        reset_capturing_descriptor(victim_saves_descriptor, &victim_saves);

        // Room light forced on so CAN_SEE()'s darkness gate never fires --
        // this fixture is about the walk/save/affect outcome, not room
        // lighting, and PERS()'s $n substitution (used by every act() TO_VICT
        // message below) needs CAN_SEE(victim, caster) to resolve the real
        // short_descr instead of falling back to "someone".
        test_world.room().light = 1;

        // caster: an NPC mystic. player.level feeds get_mystic_caster_level()
        // directly via get_prof_level()'s IS_NPC() fast path; tmpabilities.wil
        // = 0 keeps get_mystic_caster_level()'s own `number(0, will_factor %
        // 5)` roll at number(0,0), which returns immediately without
        // consuming a queued test-RNG value (platform/rots_util.cpp's
        // `if (from == to) return from;` guard) -- so level is exactly
        // player.level (20) with no RNG entanglement.
        caster.specials2.act = MOB_ISNPC;
        caster.player.level = 20;
        caster.player.race = RACE_HUMAN;
        caster.player.short_descr = caster_short_descr;
        caster.specials.position = POSITION_STANDING;
        caster.tmpabilities.wil = 0;
        caster.in_room = 0;
        caster.desc = &caster_descriptor;

        victim_fails.specials2.act = MOB_ISNPC;
        victim_fails.player.level = 10;
        victim_fails.player.race = RACE_HUMAN;
        victim_fails.specials.position = POSITION_STANDING;
        victim_fails.specials2.perception = 0;
        victim_fails.in_room = 0;
        victim_fails.desc = &victim_fails_descriptor;

        victim_saves.specials2.act = MOB_ISNPC;
        victim_saves.player.level = 10;
        victim_saves.player.race = RACE_HUMAN;
        victim_saves.specials.position = POSITION_STANDING;
        victim_saves.specials2.perception = 100;
        victim_saves.in_room = 0;
        victim_saves.desc = &victim_saves_descriptor;

        // Occupant chain: caster (skipped by tmpch != caster), then
        // victim_fails, then victim_saves -- this exact order is what pins
        // the RNG-draw sequence the tests below queue.
        test_world.room().people = &caster;
        caster.next_in_room = &victim_fails;
        victim_fails.next_in_room = &victim_saves;
        victim_saves.next_in_room = nullptr;
    }

    ~MysticTerrorContext()
    {
        test_world.room().people = nullptr;
        caster.next_in_room = nullptr;
        victim_fails.next_in_room = nullptr;
        victim_saves.next_in_room = nullptr;
        caster.in_room = NOWHERE;
        victim_fails.in_room = NOWHERE;
        victim_saves.in_room = NOWHERE;
        clear_test_random_values();
    }
};

} // namespace

TEST(MysticSpellTerror, BroadcastsScreamToCasterRegardlessOfRoomPopulation)
{
    MysticTerrorContext context;

    // victim_fails: two draws, both high (0.99 -> offense 99), fails
    // saves_mystic() directly, then fails saves_leadership()'s own
    // independent internal saves_mystic() re-check (defense is 0 either
    // way, no master/mount to fall back on).
    push_test_random_value(0.99);
    push_test_random_value(0.99);
    // victim_saves: one draw, low (0.0 -> offense 0), saves on the first
    // (and only) saves_mystic() call -- short-circuits before
    // saves_leadership() is ever invoked.
    push_test_random_value(0.0);

    spell_terror(&context.caster, nullptr, 0, nullptr, nullptr, 0, 0);

    EXPECT_EQ(std::string(context.caster_descriptor.output),
        "You breathe an icy, cold breath across the room.\n\r");
}

TEST(MysticSpellTerror, AppliesFearAffectAndVictimMessageOnFailedSave)
{
    MysticTerrorContext context;

    push_test_random_value(0.99);
    push_test_random_value(0.99);
    push_test_random_value(0.0);

    spell_terror(&context.caster, nullptr, 0, nullptr, nullptr, 0, 0);

    affected_type* fear = affected_by_spell(&context.victim_fails, SPELL_FEAR);
    ASSERT_NE(fear, nullptr)
        << "Expected the zero-perception victim to fail both saves_mystic() draws and land SPELL_FEAR.";
    // level = get_mystic_caster_level(caster) = player.level (20) + 0 (wil=0
    // keeps the intel-rounding roll at number(0,0)); duration/modifier are
    // hand-derived from the pre-conversion source's `af.duration = level;
    // af.modifier = level + 10;`.
    EXPECT_EQ(fear->duration, 20);
    EXPECT_EQ(fear->modifier, 30);
    EXPECT_EQ(fear->location, APPLY_NONE);
    EXPECT_EQ(fear->bitvector, 0);

    EXPECT_EQ(std::string(context.victim_fails_descriptor.output),
        "A grim mystic suddenly breathes an icy, cold breath everywhere. Terror overcomes you.\n\r");
}

TEST(MysticSpellTerror, SkipsFearAffectAndSendsIgnoreMessageOnSuccessfulSave)
{
    MysticTerrorContext context;

    push_test_random_value(0.99);
    push_test_random_value(0.99);
    push_test_random_value(0.0);

    spell_terror(&context.caster, nullptr, 0, nullptr, nullptr, 0, 0);

    EXPECT_EQ(affected_by_spell(&context.victim_saves, SPELL_FEAR), nullptr)
        << "Expected the full-perception victim's first-roll save to skip the fear affect entirely.";

    EXPECT_EQ(std::string(context.victim_saves_descriptor.output),
        "A grim mystic suddenly breathes an icy, cold breath. You ignore it.\n\r");
}

TEST(MysticSpellTerror, DoesNotMessageOrAffectTheCasterItself)
{
    MysticTerrorContext context;

    push_test_random_value(0.99);
    push_test_random_value(0.99);
    push_test_random_value(0.0);

    spell_terror(&context.caster, nullptr, 0, nullptr, nullptr, 0, 0);

    // The walk's `tmpch != caster` guard means the caster never enters the
    // save/affect/message branch at all -- its only output is the
    // broadcast line asserted in the first test above, and it never
    // receives a SPELL_FEAR affect from its own cast.
    EXPECT_EQ(affected_by_spell(&context.caster, SPELL_FEAR), nullptr);
}

TEST(MysticSpellTerror, ReturnsImmediatelyWhenCasterHasNoRoom)
{
    MysticTerrorContext context;
    context.caster.in_room = NOWHERE;

    // No queued RNG values at all: if the NOWHERE guard didn't return
    // early, the walk would starve test_random_utils.h's queue and the
    // production number() fallback (real RNG) would fire -- this test
    // relies on that not happening, i.e. on zero draws being consumed.
    spell_terror(&context.caster, nullptr, 0, nullptr, nullptr, 0, 0);

    EXPECT_EQ(std::string(context.caster_descriptor.output), "")
        << "Expected the caster->in_room == NOWHERE guard to return before any message is sent.";
}
