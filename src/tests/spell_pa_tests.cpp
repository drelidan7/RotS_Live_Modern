#include "../color.h"
#include "../interpre.h" /* For ACMD() -- RR Wave R3 Task 1b's do_cast pair below */
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
