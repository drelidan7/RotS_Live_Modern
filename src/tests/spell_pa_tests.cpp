#include "../big_brother.h" /* register_target_valid_hook() -- Task 1d's adjacency test */
#include "../color.h"
#include "../entity_hooks.h" /* target-valid hook seam -- Task 1d's adjacency test */
#include "../interpre.h" /* For ACMD() -- RR Wave R3 Task 1b's do_cast pair below */
#include "../rots_rng.h"
#include "../spells.h"
#include "rots/core/character.h"
#include "rots/core/room.h"
#include "rots/core/descriptor.h"
#include "rots/core/types.h"
#include "../utils.h"
#include "test_char_cleanup.h"
#include "test_placement.h"
#include "test_world.h"

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <string>
#include <string_view>

extern struct room_data world;
extern int top_of_world;
void clear_char(struct char_data* ch, int mode);
void say_spell(struct char_data* caster, int spell_index);
void send_magic_room_message(struct char_data* caster, std::string_view message);
// do_cast has no header declaration anywhere in the tree (ACMD bodies are
// address-taken into cmd_info[]/combat_hooks.h's table, never called by name
// outside their own TU) -- forward-declared here with the ACMD() macro for RR
// Wave R3 Task 1b's dispatch-invariant pair, mirroring act_othe_tests.cpp's
// `ACMD(do_knock);` precedent.
ACMD(do_cast);
// consts.cpp owns the spell table; no header declares it (every consumer
// spells its own extern -- act_othe.cpp:51, act_wiz.cpp:99, ...). Task 1d's
// adjacency test needs it to install and restore one scratch cell.
extern struct skill_data skills[];

namespace {

descriptor_data make_descriptor()
{
    descriptor_data descriptor {};
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    return descriptor;
}

void initialize_player_character(char_data* character, const char* name)
{
    clear_char(character, MOB_VOID);
    character->player.name = const_cast<char*>(name);
    character->specials.position = POSITION_STANDING;
}

} // namespace

TEST(SpellParser, SaySpellUsesMagicColorForColorEnabledObservers)
{
    ScopedTestWorld test_world;
    test_world.room().number = 3001;

    char_data caster {};
    char_data observer {};
    descriptor_data observer_descriptor = make_descriptor();
    // Re-point output to THIS object's own small_outbuf: make_descriptor()
    // returns by value and its internal `output = &small_outbuf` self-pointer
    // dangles into the returned-from frame when NRVO isn't applied (MSVC
    // Debug) -- writes would otherwise corrupt freed stack. Phase 3 Task 6.
    observer_descriptor.output = observer_descriptor.small_outbuf;

    initialize_player_character(&caster, "caster");
    // Releases caster.profs/skills/knowledge (clear_char() heap
    // allocations, via initialize_player_character()) at scope exit
    // (Phase 5 T6 leak sweep).
    ScopedClearCharFields caster_cleanup { caster };
    initialize_player_character(&observer, "observer");
    // Releases observer.profs/skills/knowledge (clear_char() heap
    // allocations, via initialize_player_character()) at scope exit
    // (Phase 5 T6 leak sweep).
    ScopedClearCharFields observer_cleanup { observer };
    observer.desc = &observer_descriptor;
    SET_BIT(PRF_FLAGS(&observer), PRF_COLOR);
    set_colornum(&observer, COLOR_MAGIC, CBBLU);

    // Caster at the head, observer behind it -- the chain the two
    // attach_character_to_room() calls plus the world[0].people write used to
    // build by hand, in the same order (LS-3a T3, test_placement.h). Declared
    // after initialize_player_character(), whose clear_char() would otherwise
    // wipe both the link and the location (idiom rule 9), and after the two
    // ScopedClearCharFields so it unwinds before them.
    ScopedRoomOccupants occupants { &test_world.room(), 0, { &caster, &observer } };

    say_spell(&caster, SPELL_MAGIC_MISSILE);

    const std::string output = observer_descriptor.output;
    EXPECT_NE(output.find(color_sequence[CBBLU]), std::string::npos) << output;
    EXPECT_NE(output.find("Caster utters a strange command, 'magic missile'"), std::string::npos)
        << output;
    EXPECT_NE(output.find(color_sequence[CNRM]), std::string::npos) << output;
}

TEST(SpellParser, MagicRoomMessageOmitsColorCodesForObserversWithoutColorEnabled)
{
    ScopedTestWorld test_world;
    test_world.room().number = 3002;

    char_data caster {};
    char_data observer {};
    descriptor_data observer_descriptor = make_descriptor();
    // Re-point output to THIS object's own small_outbuf: make_descriptor()
    // returns by value and its internal `output = &small_outbuf` self-pointer
    // dangles into the returned-from frame when NRVO isn't applied (MSVC
    // Debug) -- writes would otherwise corrupt freed stack. Phase 3 Task 6.
    observer_descriptor.output = observer_descriptor.small_outbuf;

    initialize_player_character(&caster, "caster");
    // Releases caster.profs/skills/knowledge (clear_char() heap
    // allocations, via initialize_player_character()) at scope exit
    // (Phase 5 T6 leak sweep).
    ScopedClearCharFields caster_cleanup { caster };
    initialize_player_character(&observer, "observer");
    // Releases observer.profs/skills/knowledge (clear_char() heap
    // allocations, via initialize_player_character()) at scope exit
    // (Phase 5 T6 leak sweep).
    ScopedClearCharFields observer_cleanup { observer };
    observer.desc = &observer_descriptor;
    REMOVE_BIT(PRF_FLAGS(&observer), PRF_COLOR);
    set_colornum(&observer, COLOR_MAGIC, CBBLU);

    // Caster at the head, observer behind it -- the chain the two
    // attach_character_to_room() calls plus the world[0].people write used to
    // build by hand, in the same order (LS-3a T3, test_placement.h). Declared
    // after initialize_player_character(), whose clear_char() would otherwise
    // wipe both the link and the location (idiom rule 9), and after the two
    // ScopedClearCharFields so it unwinds before them.
    ScopedRoomOccupants occupants { &test_world.room(), 0, { &caster, &observer } };

    send_magic_room_message(&caster, "$n begins quietly muttering some strange, powerful words.\n\r");

    const std::string output = observer_descriptor.output;
    EXPECT_EQ(output.find(color_sequence[CBBLU]), std::string::npos) << output;
    EXPECT_EQ(output.find(color_sequence[CNRM]), std::string::npos) << output;
    EXPECT_NE(output.find("Caster begins quietly muttering some strange, powerful words."), std::string::npos) << output;
}

TEST(SpellParser, MagicRoomMessageAcceptsBoundedTextAndStopsAtEmbeddedNull)
{
    ScopedTestWorld test_world;

    char_data caster {};
    char_data observer {};
    descriptor_data observer_descriptor = make_descriptor();
    observer_descriptor.output = observer_descriptor.small_outbuf;

    initialize_player_character(&caster, "caster");
    ScopedClearCharFields caster_cleanup { caster };
    initialize_player_character(&observer, "observer");
    ScopedClearCharFields observer_cleanup { observer };
    observer.desc = &observer_descriptor;

    // Caster at the head, observer behind it -- the chain the two
    // attach_character_to_room() calls plus the world[0].people write used to
    // build by hand, in the same order (LS-3a T3, test_placement.h). Declared
    // after initialize_player_character(), whose clear_char() would otherwise
    // wipe both the link and the location (idiom rule 9), and after the two
    // ScopedClearCharFields so it unwinds before them.
    ScopedRoomOccupants occupants { &test_world.room(), 0, { &caster, &observer } };

    const std::array<char, 20> message {
        '$', 'n', ' ', 'c', 'a', 's', 't', 's', '.', '\n', '\r', '\0',
        'i', 'g', 'n', 'o', 'r', 'e', 'd', '!'
    };
    send_magic_room_message(&caster, std::string_view(message.data(), message.size()));

    EXPECT_EQ(std::string(observer_descriptor.output), "Caster casts.\n\r\n\r");
}

// ---------------------------------------------------------------------------
// RR Wave R3 Task 1b -- do_cast()'s dispatch-invariant guard (owner ruling
// R3-O-1; docs/superpowers/specs/2026-08-21-rr3-combat-design.md section 2).
//
// do_cast is the ordinary cast path's `skills[].spell_pointer` door
// (spell_pa.cpp:883, dispatch census M-6 row 1): every ASPELL body reached
// through it receives `ch` as its caster without any placement check
// anywhere on the path.
//
// WHERE THE GUARD SITS, and why it is not where the dispatch is. do_cast
// also reads `room_of(ch)->room_flags` unguarded as its first substantive
// statement, and that read DOMINATES the dispatch. It is not a proof of it --
// room_of() is total and degrades silently, so an earlier unguarded resolve
// of the same id establishes nothing (coordinator ruling R3-C-1) -- but it is
// the right place to stand one guard that covers both. The pair below
// therefore observes the guard through the FIRST thing the body does after
// it, not through the spell dispatch itself.
//
// DISCRIMINATOR: a zero-initialized waiting_type (subcmd 0, targ1.type
// TARGET_NONE, targ1.ch_num -1 so no npc-self-cast entry matches) and an
// empty argument line drive do_cast's cheapest deterministic branch -- the
// "no spell named" arm, one literal line, no spell table lookup, no target
// resolution. Unplaced -> the guard returns before it; placed -> it arrives.
// ---------------------------------------------------------------------------

TEST(DoCastDispatchInvariant, RefusesToRunForAnUnplacedCaster)
{
    ScopedTestWorld test_world;
    test_world.room().room_flags = 0;

    char_data caster {};
    descriptor_data caster_descriptor = make_descriptor();
    caster_descriptor.output = caster_descriptor.small_outbuf;

    initialize_player_character(&caster, "caster");
    ScopedClearCharFields caster_cleanup { caster };
    caster.desc = &caster_descriptor;
    set_location(&caster, NOWHERE);

    waiting_type wtl {};
    wtl.targ1.ch_num = -1; // matches no npc_self_spells[] entry
    char argument[] = "";
    do_cast(&caster, argument, &wtl, 0, 0);

    EXPECT_STREQ(caster_descriptor.small_outbuf, "")
        << "Expected the dispatch-invariant guard to return before do_cast's body ran -- not even "
           "its no-spell-named line should reach the caster.";
}

TEST(DoCastDispatchInvariant, RunsForAPlacedCaster)
{
    ScopedTestWorld test_world;
    test_world.room().room_flags = 0;

    char_data caster {};
    descriptor_data caster_descriptor = make_descriptor();
    caster_descriptor.output = caster_descriptor.small_outbuf;

    initialize_player_character(&caster, "caster");
    ScopedClearCharFields caster_cleanup { caster };
    caster.desc = &caster_descriptor;
    set_location(&caster, 0);

    waiting_type wtl {};
    wtl.targ1.ch_num = -1;
    char argument[] = "";
    do_cast(&caster, argument, &wtl, 0, 0);

    EXPECT_STREQ(caster_descriptor.small_outbuf, "Cast which what where?\n\r")
        << "Expected the identical fixture with a PLACED caster to reach the real body -- the "
           "guard must not block a normal cast attempt.";
}

// ---------------------------------------------------------------------------
// RR Wave R3 Task 1d (coordinator ruling R3-C-7) -- the ADJACENCY test for
// do_cast's SECOND tripwire, the one immediately before the
// skills[].spell_pointer dispatch (spell_pa.cpp:928).
//
// The pair above enters do_cast with the caster already unplaced, so the
// entry guard at :499 alone satisfies both halves of it. This test enters
// PLACED -- the entry guard passes, exactly as it does on every live cast --
// and then unplaces the caster from inside a call do_cast makes BELOW that
// guard and ABOVE the dispatch. Only the pre-dispatch tripwire can refuse.
//
// The relocation vector is entity_hooks.h's target-valid hook, which do_cast
// invokes through rots::entity::dispatch_target_valid() at spell_pa.cpp:711.
// Production's implementation (big_brother::is_target_valid) is a pure
// predicate and relocates nobody; the stub below is not a claim about it. It
// is the cheapest controllable stand-in for the class of call R3-C-7 is
// about -- :516's complete_delay(ch), which re-enters command_interpreter and
// from there special()'s spec-proc fan-out; :721's appear(ch) and :895's
// check_hallucinate(ch, ..), both of which reach affect_total() and so
// affect_modify()'s APPLY_SPELL arm, an arbitrary ASPELL run with
// caster == victim == ch. All three sit between the two guards, and none of
// them promises to leave the caster placed on return.
//
// DISCRIMINATOR: with the pre-dispatch guard deleted and :499 left in place,
// the relocated half below dispatches the spell anyway and this test goes
// RED, while both tests above stay green.
// ---------------------------------------------------------------------------

// Swaps entity_hooks.h's target-valid hook and restores big_brother.cpp's
// REAL forwarder on scope exit -- copied from big_brother_hooks_tests.cpp's
// ScopedTargetValidHook (duplicated, not shared: each copy lives in a
// different TU's anonymous namespace, the reset_capturing_descriptor
// precedent).
namespace {

class ScopedTargetValidHook {
public:
    explicit ScopedTargetValidHook(rots::entity::target_valid_fn hook)
    {
        rots::entity::set_target_valid_hook(hook);
    }

    ~ScopedTargetValidHook() { register_target_valid_hook(); }

    ScopedTargetValidHook(const ScopedTargetValidHook&) = delete;
    ScopedTargetValidHook& operator=(const ScopedTargetValidHook&) = delete;
};

// The skill index the test casts. 200 is inside MAX_SKILLS but outside the
// 162 rows consts.cpp initializes, so the whole cell is zero and restoring
// it is a plain value-initialization -- no live spell is disturbed.
constexpr int kScratchSpellIndex = 200;

// Saves skills[kScratchSpellIndex] and restores it verbatim on scope exit,
// so nothing this test writes into the process-global spell table outlives
// it (the fixture-leak class LS-2's finalization repairs chased three times).
class ScopedScratchSpell {
public:
    ScopedScratchSpell()
        : previous_(skills[kScratchSpellIndex])
    {
    }

    ~ScopedScratchSpell() { skills[kScratchSpellIndex] = previous_; }

    ScopedScratchSpell(const ScopedScratchSpell&) = delete;
    ScopedScratchSpell& operator=(const ScopedScratchSpell&) = delete;

private:
    skill_data previous_;
};

bool g_scratch_spell_dispatched = false;

ASPELL(recording_scratch_spell) { g_scratch_spell_dispatched = true; }

char_data* g_relocating_hook_target = nullptr;

// Unplaces the caster and lets the cast proceed -- the relocation R3-C-7's
// adjacency rule exists to survive.
bool relocating_target_valid_stub(char_data* attacker, const char_data*, int)
{
    if (attacker == g_relocating_hook_target) {
        set_location(attacker, NOWHERE);
    }
    return true;
}

// The control: same call, same position in the body, no relocation.
bool permissive_target_valid_stub(char_data*, const char_data*, int) { return true; }

// Drives do_cast's SECOND phase (wtl->subcmd != 0 skips the spell-selection
// block entirely) with a TAR_CHAR_WORLD target, which is the one
// character-target flag whose validity switch at spell_pa.cpp:751 does NOT
// re-compare the caster's and victim's rooms -- so a relocation at :711
// reaches the dispatch instead of being caught by the "victim has fled" arm.
void drive_scratch_cast(char_data* caster, char_data* victim, waiting_type* wtl)
{
    wtl->subcmd = kScratchSpellIndex;
    wtl->targ2.type = TARGET_CHAR;
    wtl->targ2.choice = TAR_CHAR_WORLD;
    wtl->targ2.ptr.ch = victim;

    char argument[] = "";
    do_cast(caster, argument, wtl, 0, 0);
}

} // namespace

TEST(DoCastDispatchInvariant, RefusesTheSpellWhenTheCasterWasRelocatedAfterTheEntryGuard)
{
    ScopedTestWorld test_world;
    test_world.room().room_flags = 0;

    ScopedScratchSpell scratch_spell;
    std::strncpy(skills[kScratchSpellIndex].name, "scratch", sizeof(skills[0].name) - 1);
    skills[kScratchSpellIndex].name[sizeof(skills[0].name) - 1] = '\0';
    skills[kScratchSpellIndex].type = PROF_CLERIC; // keeps do_sense_magic's mage-only walk out
    skills[kScratchSpellIndex].min_usesmana = 0;
    skills[kScratchSpellIndex].spell_pointer = &recording_scratch_spell;

    char_data caster {};
    char_data victim {};
    initialize_player_character(&caster, "caster");
    ScopedClearCharFields caster_cleanup { caster };
    initialize_player_character(&victim, "victim");
    ScopedClearCharFields victim_cleanup { victim };
    ScopedRoomOccupants occupants { &test_world.room(), 0, { &caster, &victim } };

    // GET_KNOWLEDGE feeds the `number(0, 100) >= tmp` concentration roll at
    // spell_pa.cpp:838; clear_char() leaves the vector zeroed, which would
    // fail the roll every time. Seeded RNG plus full knowledge makes the
    // roll deterministic (act_format_tests.cpp:318's precedent).
    if (caster.knowledge.size() <= static_cast<std::size_t>(kScratchSpellIndex)) {
        caster.knowledge.resize(MAX_SKILLS, 0);
    }
    caster.knowledge[kScratchSpellIndex] = 100;

    g_relocating_hook_target = &caster;

    {
        ScopedTargetValidHook relocating_hook { relocating_target_valid_stub };
        g_scratch_spell_dispatched = false;
        rots_rng::seed(42u);

        waiting_type wtl {};
        drive_scratch_cast(&caster, &victim, &wtl);

        EXPECT_EQ(location_of(&caster), NOWHERE)
            << "The stub must really have unplaced the caster at spell_pa.cpp:711 -- otherwise "
               "the pre-dispatch guard has nothing to refuse and this test proves nothing.";
        EXPECT_FALSE(g_scratch_spell_dispatched)
            << "Expected the pre-dispatch dispatch-invariant guard (spell_pa.cpp:928) to refuse "
               "the skills[].spell_pointer dispatch for a caster relocated AFTER the entry "
               "guard at :499 already passed.";
    }

    // The control: the identical drive with a hook that leaves the caster
    // where it is reaches the real dispatch. The difference between the two
    // halves is the relocation, not the hook's presence.
    set_location(&caster, 0);
    {
        ScopedTargetValidHook permissive_hook { permissive_target_valid_stub };
        g_scratch_spell_dispatched = false;
        rots_rng::seed(42u);

        waiting_type wtl {};
        drive_scratch_cast(&caster, &victim, &wtl);

        EXPECT_EQ(location_of(&caster), 0);
        EXPECT_TRUE(g_scratch_spell_dispatched)
            << "Expected a placed caster to reach the real dispatch -- the guard must not block "
               "a normal cast.";
    }

    g_relocating_hook_target = nullptr;
}
