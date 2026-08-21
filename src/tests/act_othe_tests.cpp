// act_othe_tests.cpp

// LS-3b Wave T2 (ruling R-3b-B; R-A5 characterization-first pattern from
// LS-3a's T0b findings). Characterizes do_knock() -- one of Table 1's two
// widest/highest-risk cursor windows (act_othe.cpp:1821 save / :1822 write /
// :1842 restore) -- BEFORE its cursor site converts onto
// rots::entity::ScopedRenderLocation. This suite must pass unchanged both
// before and after that conversion (the T2 report records both runs).
//
// What it pins: do_knock() renders its "you hear a knock" message to the
// room BEHIND the door -- i.e. while ch's location field is spoofed to that
// room -- not to ch's own room, and ch's real location is unchanged once
// the command returns. This is exactly the F15 persistence-spoof/cursor
// behavior ScopedRenderLocation must reproduce byte-for-byte.

#include "../comm.h"
#include "../handler.h"
#include "../interpre.h"
#include "../spells.h"
#include "../utils.h"
#include "rots/core/character.h"
#include "rots/core/descriptor.h"
#include "rots/core/object.h"
#include "rots/core/room.h"
#include "rots/core/types.h"
#include "test_placement.h"
#include "test_world.h"

#include <gtest/gtest.h>

#include <optional>

// do_knock has no header declaration anywhere in the tree (ACMD bodies are
// address-taken into combat_hooks.h's dispatch table, never called by name
// outside their own TU) -- forward-declared here with the ACMD() macro,
// mirroring act_offe_tests.cpp's `ACMD(do_rescue);` precedent.
ACMD(do_knock);
// Same gap, same treatment, for RR Wave R3 Task 1b's dispatch-invariant pair
// at the end of this file.
ACMD(do_use);
// consts.cpp's global skill table -- not declared in any header (matching
// every .cpp that references it, e.g. act_othe.cpp:51, and
// spell_registry_tests.cpp's own copy). The Task 5-fix probes below borrow
// one cell's spell_pointer to observe do_use's two dispatches directly.
extern struct skill_data skills[MAX_SKILLS];

namespace {

// Mirrors comm_act_tests.cpp's reset_capturing_descriptor() (duplicated,
// not shared -- that copy lives in a different TU's anonymous namespace).
void reset_capturing_descriptor(descriptor_data& descriptor, char_data* character)
{
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = 0; // CON_PLAYING
    descriptor.character = character;
}

// Two rooms (0 and 1) joined by a door ("gate") ch stands north of: room 0's
// NORTH exit leads to room 1, and room 1's SOUTH exit leads back, both
// EX_ISDOOR and sharing the same keyword so do_knock's reverse-exit branch
// (`EXIT(ch, rev_dir[tmp])->keyword` non-null) renders the
// "You hear a knock on the gate." line rather than the directionless
// "sounding {dir}ward" fallback. A bystander sits in each room with a
// capturing descriptor so the test can tell which room actually received
// each broadcast -- the whole point of characterizing the cursor.
struct DoKnockContext {
    ScopedTestWorld test_world { 2 };
    char_data ch { };
    char_data room0_bystander { };
    char_data room1_bystander { };
    descriptor_data ch_descriptor { };
    descriptor_data room0_descriptor { };
    descriptor_data room1_descriptor { };
    char door_keyword[8] = "gate";
    room_direction_data door_0_to_1 { };
    room_direction_data door_1_to_0 { };

    // Declared after every char_data/room_direction_data member so both
    // unwind (in reverse declaration order) before the characters and doors
    // they reference are destroyed.
    std::optional<ScopedRoomOccupants> room0_occupants;
    std::optional<ScopedRoomOccupants> room1_occupants;

    DoKnockContext()
    {
        reset_capturing_descriptor(ch_descriptor, &ch);
        reset_capturing_descriptor(room0_descriptor, &room0_bystander);
        reset_capturing_descriptor(room1_descriptor, &room1_bystander);
        ch.desc = &ch_descriptor;
        room0_bystander.desc = &room0_descriptor;
        room1_bystander.desc = &room1_descriptor;

        ch.player.name = const_cast<char*>("Frodo");
        ch.specials.position = POSITION_STANDING;
        ch.player.race = RACE_HUMAN;
        ch.player.sex = SEX_MALE; // pins $m/$s -> "him"/"his" in the no-target branch
        // PRF_HOLYLIGHT on everyone sidesteps CAN_SEE()'s darkness gate --
        // real light bookkeeping is out of scope for this cursor
        // characterization (comm_act_tests.cpp's RoomPairContext precedent).
        SET_BIT(ch.specials2.pref, PRF_HOLYLIGHT);
        room0_bystander.specials.position = POSITION_STANDING;
        room0_bystander.player.race = RACE_HUMAN;
        SET_BIT(room0_bystander.specials2.pref, PRF_HOLYLIGHT);
        room1_bystander.specials.position = POSITION_STANDING;
        room1_bystander.player.race = RACE_HUMAN;
        SET_BIT(room1_bystander.specials2.pref, PRF_HOLYLIGHT);

        door_0_to_1.keyword = door_keyword;
        door_0_to_1.exit_info = EX_ISDOOR;
        door_0_to_1.to_room = 1;
        door_0_to_1.exit_width = 4;
        door_0_to_1.key = -1;
        door_0_to_1.general_description = nullptr;

        door_1_to_0 = door_0_to_1;
        door_1_to_0.to_room = 0;

        room_by_id_total(0)->dir_option[NORTH] = &door_0_to_1;
        room_by_id_total(1)->dir_option[SOUTH] = &door_1_to_0;

        room0_occupants.emplace(room_by_id_total(0), 0,
            std::initializer_list<char_data*> { &ch, &room0_bystander });
        room1_occupants.emplace(room_by_id_total(1), 1,
            std::initializer_list<char_data*> { &room1_bystander });
    }

    ~DoKnockContext()
    {
        // The two room_direction_data members above are about to be
        // destroyed along with this context (they are plain members, not
        // RAII) -- clear the world's pointers to them FIRST, before that
        // happens, so no later test in the monolithic single-process runner
        // ever dereferences a dangling exit.
        room_by_id_total(0)->dir_option[NORTH] = nullptr;
        room_by_id_total(1)->dir_option[SOUTH] = nullptr;
    }
};

} // namespace

TEST(DoKnockCharacterization, KnockRendersTheEchoInTheRoomBehindTheDoorNotChsOwnRoom)
{
    DoKnockContext context;

    do_knock(&context.ch, mutable_arg("gate"), nullptr, 0, 0);

    // The two messages that render BEFORE the cursor is written (ch's real
    // room, room 0): the actor's own confirmation and the room-0 bystander's
    // "$n knocked" broadcast.
    EXPECT_STREQ(context.ch_descriptor.output, "You knocked on the gate.\n");
    EXPECT_STREQ(context.room0_descriptor.output, "Frodo knocked on the gate.\n\n\r");

    // The message that renders WHILE the cursor is parked on room 1 (the F15
    // persistence-spoof site): only room 1's bystander receives it, and room
    // 0's bystander output above shows no trace of it -- proving act()'s
    // TO_ROOM resolution followed the spoofed location, not ch's real one.
    EXPECT_STREQ(context.room1_descriptor.output, "You hear a knock on the gate.\n\n\r");

    // The cursor is restored: ch's real location is unchanged once the
    // command returns.
    EXPECT_EQ(location_of(&context.ch), 0);
}

TEST(DoKnockCharacterization, KnockWithNoTargetSendsTheSelfDirectedMessageAndNeverMovesTheCursor)
{
    // The `!*argument` early-return branch (act_othe.cpp:1804-1807) never
    // reaches the cursor window at all -- a negative control confirming the
    // cursor conversion cannot affect this path.
    DoKnockContext context;

    do_knock(&context.ch, mutable_arg(""), nullptr, 0, 0);

    EXPECT_STREQ(context.ch_descriptor.output, "You knocked yourself on your head.\n\r");
    EXPECT_STREQ(context.room0_descriptor.output, "Frodo knocked himself on his head.\n\r\n\r");
    EXPECT_STREQ(context.room1_descriptor.output, "");
    EXPECT_EQ(location_of(&context.ch), 0);
}

// ---------------------------------------------------------------------------
// RR Wave R3 Task 1b -- do_use()'s dispatch-invariant guard (owner ruling
// R3-O-1; docs/superpowers/specs/2026-08-21-rr3-combat-design.md section 2).
//
// do_use is the ENTIRE object-driven cast surface of this tree (the dispatch
// census verified scroll/potion casting does not exist here), and it carries
// TWO `skills[].spell_pointer` doors: the ITEM_STAFF arm (act_othe.cpp:805 in
// the census's numbering) and the ITEM_WAND arm (:825). Neither checks the
// caster.
//
// ONE guard covers both, placed immediately after `stick =
// ch->equipment[HOLD];` -- the single straight-line statement that dominates
// either arm -- rather than one guard per arm. It sits AFTER the
// not-holding-that-item early return, which resolves no room and is
// deliberately left alone.
//
// DISCRIMINATOR: a held ITEM_STAFF with zero charges. That drives the staff
// arm's two act() taps and its "powerless" line WITHOUT reaching the
// spell_pointer dispatch at all, so the pair needs no spell table. Unplaced
// -> the guard returns before the first tap; placed -> both lines arrive.
// ---------------------------------------------------------------------------

namespace {

// A character holding a charge-less staff in an otherwise empty one-room
// world. The room is empty of occupants, so the staff arm's act(...,
// TO_ROOM) tap has no recipient and only the TO_CHAR half is observable.
struct DoUseStaffContext {
    ScopedTestWorld test_world { 1 };
    char_data ch {};
    descriptor_data ch_descriptor {};
    obj_data staff {};
    char staff_name[8] = "staff";
    char staff_short_descr[16] = "a dull staff";

    DoUseStaffContext()
    {
        reset_capturing_descriptor(ch_descriptor, &ch);
        ch.desc = &ch_descriptor;
        ch.player.name = const_cast<char*>("Frodo");
        ch.player.race = RACE_HUMAN;
        ch.specials.position = POSITION_STANDING;
        SET_BIT(ch.specials2.pref, PRF_HOLYLIGHT);

        staff.name = staff_name;
        staff.short_description = staff_short_descr;
        staff.obj_flags.type_flag = ITEM_STAFF;
        staff.obj_flags.value[2] = 0; // no charges -> never reaches spell_pointer
        staff.obj_flags.value[3] = 0;
        ch.equipment[HOLD] = &staff;
    }

    ~DoUseStaffContext() { ch.equipment[HOLD] = nullptr; }

    DoUseStaffContext(const DoUseStaffContext&) = delete;
    DoUseStaffContext& operator=(const DoUseStaffContext&) = delete;
};

} // namespace

TEST(DoUseDispatchInvariant, RefusesToRunForAnUnplacedActor)
{
    DoUseStaffContext context;
    set_location(&context.ch, NOWHERE);

    do_use(&context.ch, mutable_arg("staff"), nullptr, 0, 0);

    EXPECT_STREQ(context.ch_descriptor.output, "")
        << "Expected the dispatch-invariant guard to return before either spell_pointer arm was "
           "reached -- not even the staff arm's tap message should be rendered.";
}

TEST(DoUseDispatchInvariant, RunsForAPlacedActor)
{
    DoUseStaffContext context;
    set_location(&context.ch, 0);

    do_use(&context.ch, mutable_arg("staff"), nullptr, 0, 0);

    EXPECT_STREQ(context.ch_descriptor.output,
        "You tap a dull staff three times on the ground.\n\rThe staff seems powerless.\n\r")
        << "Expected the identical fixture with a PLACED actor to reach the real staff arm -- the "
           "guard must not block a normal use.";
}

// ---------------------------------------------------------------------------
// RR Wave R3 Task 5-fix -- do_use()'s two R3-C-7 ADJACENCY tripwires
// (whole-branch review-1, finding M-4).
//
// The :806 entry guard above dominates both spell_pointer doors, but three
// real calls with `ch` as a participant run between it and them (act() twice
// on the staff arm; generic_find() and act() twice on the wand arm), so the
// six `dispatch-invariant` rows that inherit this entry were inheriting it by
// census EXHAUSTION -- which R3-C-7 forbids in terms. Task 5-fix therefore
// added a tripwire immediately before each dispatch.
//
// WHAT THESE FOUR TESTS PIN, precisely -- and what they do not. Nothing
// inside do_use() relocates the actor (verified: act_impl and generic_find
// contain no char_to_room/char_from_room/extract_char/special/issue_command/
// command_interpreter call), so there is NO fixture that can arrive at either
// dispatch with an unplaced actor while the :806 guard stands. A T1c
// "variant-2" test -- one that goes RED with the guard present but at the old
// placement -- is therefore not constructible here, and this is recorded as
// such rather than faked. What IS newly pinned:
//
//   * both dispatches are REACHED for a placed actor with a charged item
//     (nothing before this wave exercised either spell_pointer door at all),
//     so the two new tripwires cannot silently refuse a normal use; and
//   * neither dispatch runs for an unplaced actor -- an assertion that stays
//     GREEN when the :806 guard alone is neutered (the two new tripwires
//     carry it) and goes RED when the arm's own tripwire is neutered too.
//     That pair of sabotage runs is the report's evidence.
//
// The guards' ORDER is pinned MECHANICALLY instead, by the registry: do_use's
// two new guard literals each quote the tripwire AND the dispatch statement it
// protects, contiguously, so moving either below its dispatch fails
// `room_resolve_census.py --check`. That is the only adjacency witness in the
// registry today (see the ledger's "known reconciliation blind spots").
//
// DISCRIMINATOR: a charged item plus a probe spell installed in skills[], so
// "the dispatch happened" is directly observable as a counter rather than
// inferred from output text.
// ---------------------------------------------------------------------------

namespace {

// Invocation record for the probe spell installed below. File-static because
// a spell_pointer is a plain C function pointer with no user-data slot.
int g_probe_spell_calls = 0;
// The `type` argument the probe last received (SPELL_TYPE_STAFF or
// SPELL_TYPE_WAND) -- proves WHICH of do_use's two doors dispatched.
int g_probe_spell_type = 0;

void probe_spell(char_data*, char*, int type, char_data*, obj_data*, int, int)
{
    ++g_probe_spell_calls;
    g_probe_spell_type = type;
}

// Installs probe_spell into one skills[] cell for the life of the scope and
// restores whatever was there. The cell index is the one do_use reads,
// `stick->obj_flags.value[3]`.
struct ScopedProbeSpell {
    // The skills[] cell this scope borrowed; restored verbatim on teardown.
    int index;
    // The cell's previous handler (usually null in a unit-test process, since
    // assign_spell_pointers() has not necessarily run).
    void (*previous)(char_data*, char*, int, char_data*, obj_data*, int, int);

    explicit ScopedProbeSpell(int cell)
        : index(cell)
        , previous(skills[cell].spell_pointer)
    {
        skills[cell].spell_pointer = probe_spell;
        g_probe_spell_calls = 0;
        g_probe_spell_type = 0;
    }

    ~ScopedProbeSpell() { skills[index].spell_pointer = previous; }

    ScopedProbeSpell(const ScopedProbeSpell&) = delete;
    ScopedProbeSpell& operator=(const ScopedProbeSpell&) = delete;
};

// A character holding a CHARGED staff (or wand) in a one-room world, with a
// carried "orb" so the wand arm's generic_find() resolves a target and the
// dispatch is actually reached.
struct DoUseChargedContext {
    ScopedTestWorld test_world { 1 };
    char_data ch {};
    descriptor_data ch_descriptor {};
    obj_data stick {};
    obj_data orb {};
    char stick_name[8] = "wand";
    char stick_short_descr[16] = "a bone wand";
    char orb_name[8] = "orb";
    char orb_short_descr[16] = "a glass orb";

    explicit DoUseChargedContext(int type_flag, int skill_cell)
    {
        reset_capturing_descriptor(ch_descriptor, &ch);
        ch.desc = &ch_descriptor;
        ch.player.name = const_cast<char*>("Frodo");
        ch.player.race = RACE_HUMAN;
        ch.specials.position = POSITION_STANDING;
        SET_BIT(ch.specials2.pref, PRF_HOLYLIGHT);

        stick.name = stick_name;
        stick.short_description = stick_short_descr;
        stick.obj_flags.type_flag = type_flag;
        stick.obj_flags.value[2] = 3; // charges remaining -> the dispatch is reachable
        stick.obj_flags.value[3] = skill_cell;
        ch.equipment[HOLD] = &stick;

        orb.name = orb_name;
        orb.short_description = orb_short_descr;
        orb.carried_by = &ch;
        ch.carrying = &orb;
    }

    ~DoUseChargedContext()
    {
        ch.equipment[HOLD] = nullptr;
        ch.carrying = nullptr;
        orb.carried_by = nullptr;
    }

    DoUseChargedContext(const DoUseChargedContext&) = delete;
    DoUseChargedContext& operator=(const DoUseChargedContext&) = delete;
};

// The skills[] cell the probes borrow. 0 is the "reserved" slot -- never
// populated by assign_spell_pointers() (spell_registry_tests.cpp asserts the
// populated set starts at 41), so borrowing it cannot perturb another suite.
constexpr int kProbeSkillCell = 0;

} // namespace

TEST(DoUseDispatchInvariant, DispatchesTheStaffSpellForAPlacedActor)
{
    ScopedProbeSpell probe { kProbeSkillCell };
    DoUseChargedContext context { ITEM_STAFF, kProbeSkillCell };
    set_location(&context.ch, 0);

    do_use(&context.ch, mutable_arg("wand"), nullptr, 0, 0);

    EXPECT_EQ(g_probe_spell_calls, 1)
        << "Expected a placed actor with a charged staff to reach the ITEM_STAFF "
           "spell_pointer dispatch -- the new adjacency tripwire must not refuse a normal use.";
    EXPECT_EQ(g_probe_spell_type, SPELL_TYPE_STAFF);
}

TEST(DoUseDispatchInvariant, RefusesTheStaffSpellDispatchForAnUnplacedActor)
{
    ScopedProbeSpell probe { kProbeSkillCell };
    DoUseChargedContext context { ITEM_STAFF, kProbeSkillCell };
    set_location(&context.ch, NOWHERE);

    do_use(&context.ch, mutable_arg("wand"), nullptr, 0, 0);

    EXPECT_EQ(g_probe_spell_calls, 0)
        << "Expected the ITEM_STAFF spell_pointer dispatch to be refused for an unplaced actor. "
           "This assertion stays green when the :806 entry guard alone is neutered -- the "
           "adjacent tripwire carries it -- and goes red when both are.";
}

TEST(DoUseDispatchInvariant, DispatchesTheWandSpellForAPlacedActor)
{
    ScopedProbeSpell probe { kProbeSkillCell };
    DoUseChargedContext context { ITEM_WAND, kProbeSkillCell };
    set_location(&context.ch, 0);

    do_use(&context.ch, mutable_arg("wand orb"), nullptr, 0, 0);

    EXPECT_EQ(g_probe_spell_calls, 1)
        << "Expected a placed actor pointing a charged wand at a findable target to reach the "
           "ITEM_WAND spell_pointer dispatch -- generic_find() must resolve the carried orb.";
    EXPECT_EQ(g_probe_spell_type, SPELL_TYPE_WAND);
}

TEST(DoUseDispatchInvariant, RefusesTheWandSpellDispatchForAnUnplacedActor)
{
    ScopedProbeSpell probe { kProbeSkillCell };
    DoUseChargedContext context { ITEM_WAND, kProbeSkillCell };
    set_location(&context.ch, NOWHERE);

    do_use(&context.ch, mutable_arg("wand orb"), nullptr, 0, 0);

    EXPECT_EQ(g_probe_spell_calls, 0)
        << "Expected the ITEM_WAND spell_pointer dispatch to be refused for an unplaced actor, "
           "with the same two-sided sabotage story as the staff pair above.";
}
