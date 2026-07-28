// LS-3b Task 1 (ls3b-global-constraints.md's T0 CLOSE-OUT, ruling R-3b-C):
// the wave's occupant-order regression net. R-3b-C adopts census-review
// finding F3, which overturns census C's belief that the boot golden
// witnesses zone-reset occupant order: scripts/boot-golden.sh's
// normalize() is a strict allow-list, so no occupant-order/count/light/
// zone-power line can ever enter either boot golden, and the seed42
// characterization golden has zero location references (M7). O-5's
// "golden-observable => STOP" safety net therefore CANNOT fire for
// occupant-order drift anywhere in this wave -- these tests are the
// wave's ONLY regression net for that failure class, and must exist
// BEFORE the store swap (T5) lands.
//
// Each TEST below pins TODAY's tail-append, oldest-first chain order
// (verified at src/entity/placement.cpp:337-348 before writing a line)
// by constructing a chain in one order, observing a selection/emission
// result, then REBUILDING THE SAME CHAIN IN REVERSE and observing the
// result flip. That inversion is the non-vacuity proof census-review F3
// demands: a swap that silently changed order (e.g. a naive head-first
// publish) would make these tests fail exactly where the goldens cannot.
//
// Witnesses required by R-3b-C (naming census-review's own labels):
//   (a) get_char_room_vis ordinal resolution (visibility.cpp)
//   (b) list_char_to_char listing order (act_info.cpp)
//   (c) zone.cpp:290 ('L' case 0, last_mob) and :442 ('A' case 4, follow
//       target) ordinal selection -- both driven through the real
//       reset_zone() entry point (no smaller function contains either
//       walk; see the brief's own fallback clause)
//   (d) limits.cpp:814/:815's invisible chain-HEAD anchor for the
//       "$p decays into dust." message (census C section 2.2(e)) --
//       driven through point_update(), again the narrowest containing
//       function
// Witness (e), extending olog_hai.cpp's existing room_target order pin
// (F10), lives in olog_hai_tests.cpp instead of here: it reuses that
// file's OlogHaiTestContext fixture rather than duplicating ~100 lines
// of setup for one test (see this wave's T1 report's DEVIATIONS section).

#include "../comm.h"
#include "../db.h"
#include "../handler.h"
#include "../player_limits.h"
#include "../utils.h"
#include "../zone.h"
#include "rots/core/character.h"
#include "rots/core/descriptor.h"
#include "rots/core/object.h"
#include "rots/core/room.h"
#include "rots/core/types.h"
#include "test_char_cleanup.h"
#include "test_placement.h"
#include "test_platform_compat.h"
#include "test_world.h"

#include <gtest/gtest.h>

#include <cstring>
#include <optional>
#include <string>

// No public header declares recount_light_room() (it is forward-declared
// locally in both limits.cpp and handler.cpp) -- mirrors that convention.
void recount_light_room(int room);

// combat_hooks.h ALSO declares a `list_char_to_char` (inside namespace
// rots::combat -- the hook dispatcher act_info.cpp registers its real body
// under), so this file deliberately does not include that header and
// instead forward-declares the real global-namespace function directly,
// mirroring act_info_format_tests.cpp's identical declaration.
void list_char_to_char(struct char_data *list, struct char_data *ch, int mode);

void clear_char(struct char_data *ch, int mode);

extern struct char_data *character_list;
extern struct obj_data *object_list;
extern struct zone_data *zone_table;
extern int top_of_zone_table;
extern int global_release_flag;

namespace {

// Mirrors act_wiz_format_tests.cpp's / act_format_tests.cpp's identical
// helper: points a descriptor's output at its OWN small_outbuf so
// send_to_char()/act() output can be inspected directly. See those files'
// banner comment for why the descriptor must be declared then reset in
// place, never returned by value.
void reset_capturing_descriptor(descriptor_data &descriptor, char_data *character) {
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = 0; // CON_PLAYING
    descriptor.character = character;
}

// A single-zone table sized and indexed the way recalc_zone_power()
// actually reads top_of_zone_table (db_world.cpp: `for (tmp = 0; tmp <=
// top_of_zone_table; tmp++)` -- an INCLUSIVE TOP INDEX), NOT the way
// zone_by_id_impl() reads the same global (a COUNT; see
// test_placement.h's ScopedZoneTableOwner class comment for the
// documented contradiction between the two conventions). Neither of
// this file's point_update()/reset_zone() call paths ever calls
// zone_by_id() -- add_follower()/stop_follower() don't move anyone's
// location, and reset_zone()'s 'L'/'A' cases used below index
// zone_table[] directly -- so the top-index convention is the only one
// that matters here, and ScopedZoneTableOwner's count-shaped 1 would
// make recalc_zone_power() write one element past this table's end.
struct ScopedSingleZoneTable {
    // The one zone this fixture stands up; its address is installed into
    // the global zone_table pointer below.
    zone_data owned_zone{};
    // Whatever zone_table pointed at before this fixture ran, restored on
    // destruction so no later test in the monolithic runner sees this
    // fixture's table.
    zone_data *previous_zone_table;
    // The companion count/index saved alongside previous_zone_table.
    int previous_top_of_zone_table;

    ScopedSingleZoneTable()
        : previous_zone_table(zone_table), previous_top_of_zone_table(top_of_zone_table) {
        owned_zone.number = 0;
        zone_table = &owned_zone;
        top_of_zone_table = 0;
    }

    ~ScopedSingleZoneTable() {
        zone_table = previous_zone_table;
        top_of_zone_table = previous_top_of_zone_table;
    }

    ScopedSingleZoneTable(const ScopedSingleZoneTable &) = delete;
    ScopedSingleZoneTable &operator=(const ScopedSingleZoneTable &) = delete;
};

} // namespace

// ---------------------------------------------------------------------------
// (a) get_char_room_vis ordinal resolution -- src/combat/visibility.cpp:617
// ---------------------------------------------------------------------------
//
// "2.guard" resolves to the SECOND matching occupant in CHAIN order (not
// insertion/allocation order, not alphabetical). Two identically-named
// mobs share room 0; reversing which physical occupant is published
// first flips which one "2.guard" reaches, proving the ordinal walk reads
// the chain's actual link order rather than anything else observable
// from the test.
TEST(GetCharRoomVisOrdinal, ResolvesTheSecondMatchAndInversionFlipsTheSelection) {
    ScopedTestWorld test_world;
    room_by_id_total(0)->room_flags = 0;
    room_by_id_total(0)->light = 1;

    char_data viewer{};
    descriptor_data viewer_descriptor{};
    ScopedClearCharFields viewer_cleanup{viewer};
    ScopedDescriptorLargeOutbufReturn viewer_outbuf_cleanup{viewer_descriptor};
    clear_char(&viewer, MOB_VOID);
    reset_capturing_descriptor(viewer_descriptor, &viewer);
    viewer.desc = &viewer_descriptor;
    viewer.specials.position = POSITION_STANDING;
    viewer.player.race = RACE_HUMAN;
    SET_BIT(viewer.specials2.pref, PRF_HOLYLIGHT);
    // viewer is deliberately NOT one of room 0's occupants -- get_char_room_vis
    // walks occupants(room_of(ch)), which depends only on ch's own location
    // field (room_of()), never on ch's own chain membership.
    set_location(&viewer, 0);

    char_data guard_a{};
    char_data guard_b{};
    ScopedClearCharFields guard_a_cleanup{guard_a};
    ScopedClearCharFields guard_b_cleanup{guard_b};
    clear_char(&guard_a, MOB_VOID);
    clear_char(&guard_b, MOB_VOID);
    guard_a.player.name = const_cast<char *>("guard");
    guard_b.player.name = const_cast<char *>("guard");
    guard_a.specials.position = POSITION_STANDING;
    guard_b.specials.position = POSITION_STANDING;
    guard_a.player.race = RACE_HUMAN;
    guard_b.player.race = RACE_HUMAN;

    char query[] = "2.guard";

    {
        // Chain order: guard_a then guard_b -- guard_b is the SECOND match.
        ScopedRoomOccupants occupants{room_by_id_total(0), 0, {&guard_a, &guard_b}};
        EXPECT_EQ(get_char_room_vis(&viewer, query, 0), &guard_b)
            << "Expected \"2.guard\" to resolve to the SECOND occupant in chain "
               "order (guard_a, guard_b).";
    }
    {
        // Same two characters, chain order REVERSED -- guard_a is now the
        // second match. Nothing about either character changed; only their
        // link order did.
        ScopedRoomOccupants occupants{room_by_id_total(0), 0, {&guard_b, &guard_a}};
        EXPECT_EQ(get_char_room_vis(&viewer, query, 0), &guard_a)
            << "Expected the chain-order inversion to flip which occupant "
               "\"2.guard\" resolves to (guard_a, guard_b).";
    }
}

// ---------------------------------------------------------------------------
// (b) list_char_to_char listing order -- src/app/act_info.cpp:1038
// ---------------------------------------------------------------------------
//
// The room-listing walk (do_look's "Chars present", "who is here", etc.)
// emits occupants to one descriptor in CHAIN order. Three distinctly
// named PCs are published, then reversed; the emitted substrings' offsets
// in the captured output must follow suit both times.
TEST(ListCharToCharOrder, EmitsOccupantsInChainOrderAndInversionFlipsTheOrder) {
    ScopedTestWorld test_world;
    room_by_id_total(0)->room_flags = 0;
    room_by_id_total(0)->light = 1;

    char_data viewer{};
    descriptor_data viewer_descriptor{};
    ScopedClearCharFields viewer_cleanup{viewer};
    ScopedDescriptorLargeOutbufReturn viewer_outbuf_cleanup{viewer_descriptor};
    clear_char(&viewer, MOB_VOID);
    reset_capturing_descriptor(viewer_descriptor, &viewer);
    viewer.desc = &viewer_descriptor;
    viewer.specials.position = POSITION_STANDING;
    viewer.player.race = RACE_HUMAN;
    SET_BIT(viewer.specials2.pref, PRF_HOLYLIGHT);
    // viewer is a bystander, not one of the published occupants:
    // list_char_to_char is driven directly with an explicit chain head,
    // exactly as first_occupant(room_of(ch)) would supply at
    // act_info.cpp:1194. It still needs a real location (clear_char()
    // leaves it at NOWHERE) so CAN_SEE()'s room_of(sub) resolves room 0
    // instead of falling back on the negative-room-number path.
    set_location(&viewer, 0);

    char_data occ_a{};
    char_data occ_b{};
    char_data occ_c{};
    ScopedClearCharFields occ_a_cleanup{occ_a};
    ScopedClearCharFields occ_b_cleanup{occ_b};
    ScopedClearCharFields occ_c_cleanup{occ_c};
    clear_char(&occ_a, MOB_VOID);
    clear_char(&occ_b, MOB_VOID);
    clear_char(&occ_c, MOB_VOID);
    for (char_data *occupant : {&occ_a, &occ_b, &occ_c}) {
        occupant->specials.position = POSITION_STANDING;
        occupant->player.race = RACE_HUMAN;
        // Empty (not null) title: show_char_to_char's PERS()-based branch
        // formats " {}" with GET_TITLE(i) whenever long_descr is null (the
        // default here) -- std::format() on a null C-string is undefined
        // behavior, so every occupant needs a real, non-null title string.
        occupant->player.title = const_cast<char *>("");
    }
    occ_a.player.name = const_cast<char *>("Alpha");
    occ_b.player.name = const_cast<char *>("Bravo");
    occ_c.player.name = const_cast<char *>("Charlie");

    {
        ScopedRoomOccupants occupants{room_by_id_total(0), 0, {&occ_a, &occ_b, &occ_c}};
        list_char_to_char(rots::entity::first_occupant(room_by_id_total(0)), &viewer, 0);
        const std::string output = viewer_descriptor.output;
        const auto pos_a = output.find("Alpha");
        const auto pos_b = output.find("Bravo");
        const auto pos_c = output.find("Charlie");
        ASSERT_NE(pos_a, std::string::npos) << output;
        ASSERT_NE(pos_b, std::string::npos) << output;
        ASSERT_NE(pos_c, std::string::npos) << output;
        EXPECT_LT(pos_a, pos_b) << "Expected chain order (Alpha, Bravo, Charlie) "
                                   "to be reflected in the emitted listing order.\n"
                                << output;
        EXPECT_LT(pos_b, pos_c) << output;
    }

    reset_capturing_descriptor(viewer_descriptor, &viewer);
    viewer.desc = &viewer_descriptor;

    {
        // Same three characters, chain order REVERSED.
        ScopedRoomOccupants occupants{room_by_id_total(0), 0, {&occ_c, &occ_b, &occ_a}};
        list_char_to_char(rots::entity::first_occupant(room_by_id_total(0)), &viewer, 0);
        const std::string output = viewer_descriptor.output;
        const auto pos_a = output.find("Alpha");
        const auto pos_b = output.find("Bravo");
        const auto pos_c = output.find("Charlie");
        ASSERT_NE(pos_a, std::string::npos) << output;
        ASSERT_NE(pos_b, std::string::npos) << output;
        ASSERT_NE(pos_c, std::string::npos) << output;
        EXPECT_LT(pos_c, pos_b) << "Expected the chain-order inversion to flip "
                                   "the emitted listing order.\n"
                                << output;
        EXPECT_LT(pos_b, pos_a) << output;
    }
}

// ---------------------------------------------------------------------------
// (c) zone.cpp:290 -- 'L' case 0 (sets last_mob) ordinal selection
// ---------------------------------------------------------------------------
//
// reset_zone() has no smaller extracted function around this walk -- it is
// the narrowest real production entry point, per this task's own fallback
// clause. Driven with a real two-command reset list: cmd[0] ('L', arg1=0)
// selects the 2nd occupant in room 0 whose nr==77 as `last_mob`; cmd[1]
// ('A', arg1=1, "Set gold") stamps a marker gold value onto whichever
// character `last_mob` ended up pointing at, making the otherwise-local
// selection directly observable.
TEST(ZoneResetLastMobOrdinal, SelectsTheNthMatchAndInversionFlipsTheSelection) {
    ScopedTestWorld test_world;

    zone_data *previous_zone_table = zone_table;
    int previous_top_of_zone_table = top_of_zone_table;
    zone_data owned_zone{};
    owned_zone.number = 0;
    zone_table = &owned_zone;
    top_of_zone_table = 0;

    reset_com cmds[2]{};
    cmds[0].command = 'L';
    cmds[0].if_flag = 0;
    cmds[0].arg1 = 0;  // sets last_mob
    cmds[0].arg2 = 0;  // room rnum
    cmds[0].arg3 = 77; // nr filter
    cmds[0].arg4 = 2;  // pick the 2nd match
    cmds[1].command = 'A';
    cmds[1].if_flag = 0;
    cmds[1].arg1 = 1;   // "Set gold"
    cmds[1].arg2 = 555; // marker value
    owned_zone.cmd = cmds;
    owned_zone.cmdno = 2;

    char_data candidate_1{};
    char_data candidate_2{};
    ScopedClearCharFields candidate_1_cleanup{candidate_1};
    ScopedClearCharFields candidate_2_cleanup{candidate_2};
    clear_char(&candidate_1, MOB_ISNPC);
    clear_char(&candidate_2, MOB_ISNPC);
    candidate_1.specials2.act = MOB_ISNPC;
    candidate_2.specials2.act = MOB_ISNPC;
    candidate_1.nr = 77;
    candidate_2.nr = 77;

    {
        ScopedRoomOccupants occupants{room_by_id_total(0), 0, {&candidate_1, &candidate_2}};
        reset_zone(0);
        EXPECT_EQ(GET_GOLD(&candidate_2), 555)
            << "Expected the 2nd nr==77 match in chain order (candidate_1, "
               "candidate_2) to become last_mob.";
        EXPECT_EQ(GET_GOLD(&candidate_1), 0);
    }

    GET_GOLD(&candidate_1) = 0;
    GET_GOLD(&candidate_2) = 0;

    {
        // Same two characters, chain order REVERSED.
        ScopedRoomOccupants occupants{room_by_id_total(0), 0, {&candidate_2, &candidate_1}};
        reset_zone(0);
        EXPECT_EQ(GET_GOLD(&candidate_1), 555)
            << "Expected the chain-order inversion to flip which occupant "
               "becomes last_mob.";
        EXPECT_EQ(GET_GOLD(&candidate_2), 0);
    }

    zone_table = previous_zone_table;
    top_of_zone_table = previous_top_of_zone_table;
}

// ---------------------------------------------------------------------------
// (c) zone.cpp:442 -- 'A' case 4 ("Set to follow") ordinal selection
// ---------------------------------------------------------------------------
//
// cmd[0] deterministically selects a single, uniquely-nr'd "leader"
// occupant as last_mob (its own ordinal is order-INsensitive on purpose,
// isolating this test to the :442 walk alone); cmd[1] then walks
// room_of(last_mob)'s occupants (the SAME room) selecting the 2nd
// occupant whose nr < 0 as the follow target and calls
// add_follower(last_mob, target, FOLLOW_MOVE) -- observable afterward as
// last_mob->master.
TEST(ZoneResetFollowOrdinal, SelectsTheNthMatchAndInversionFlipsTheSelection) {
    ScopedTestWorld test_world;

    zone_data *previous_zone_table = zone_table;
    int previous_top_of_zone_table = top_of_zone_table;
    zone_data owned_zone{};
    owned_zone.number = 0;
    zone_table = &owned_zone;
    top_of_zone_table = 0;

    reset_com cmds[2]{};
    cmds[0].command = 'L';
    cmds[0].if_flag = 0;
    cmds[0].arg1 = 0;
    cmds[0].arg2 = 0;  // room rnum -- the leader's own room
    cmds[0].arg3 = 42; // unique nr -- single deterministic match
    cmds[0].arg4 = 1;
    cmds[1].command = 'A';
    cmds[1].if_flag = 0;
    cmds[1].arg1 = 4;    // "Set to follow"
    cmds[1].arg2 = 2;    // pick the 2nd nr<0 match as the follow target
    cmds[1].arg3 = -999; // never matches a real nr -- only nr<0 counts
    owned_zone.cmd = cmds;
    owned_zone.cmdno = 2;

    char_data leader{};
    char_data follow_candidate_1{};
    char_data follow_candidate_2{};
    ScopedClearCharFields leader_cleanup{leader};
    ScopedClearCharFields follow_candidate_1_cleanup{follow_candidate_1};
    ScopedClearCharFields follow_candidate_2_cleanup{follow_candidate_2};
    clear_char(&leader, MOB_ISNPC);
    clear_char(&follow_candidate_1, MOB_ISNPC);
    clear_char(&follow_candidate_2, MOB_ISNPC);
    leader.specials2.act = MOB_ISNPC;
    follow_candidate_1.specials2.act = MOB_ISNPC;
    follow_candidate_2.specials2.act = MOB_ISNPC;
    leader.nr = 42; // excluded from the follow-candidate filter (not < 0, != -999)
    follow_candidate_1.nr = -1;
    follow_candidate_2.nr = -1;

    {
        ScopedRoomOccupants occupants{
            room_by_id_total(0), 0, {&leader, &follow_candidate_1, &follow_candidate_2}};
        reset_zone(0);
        EXPECT_EQ(leader.master, &follow_candidate_2)
            << "Expected the 2nd nr<0 match in chain order (leader, "
               "follow_candidate_1, follow_candidate_2) to become the follow "
               "target.";
        if (leader.master) {
            stop_follower(&leader, FOLLOW_MOVE);
        }
    }

    {
        // Same three characters, the two follow candidates REVERSED.
        ScopedRoomOccupants occupants{
            room_by_id_total(0), 0, {&leader, &follow_candidate_2, &follow_candidate_1}};
        reset_zone(0);
        EXPECT_EQ(leader.master, &follow_candidate_1)
            << "Expected the chain-order inversion to flip which occupant "
               "becomes the follow target.";
        if (leader.master) {
            stop_follower(&leader, FOLLOW_MOVE);
        }
    }

    zone_table = previous_zone_table;
    top_of_zone_table = previous_top_of_zone_table;
}

// ---------------------------------------------------------------------------
// (d) limits.cpp:814/:815 -- the invisible chain-HEAD anchor
// ---------------------------------------------------------------------------
//
// point_update()'s object-decay sweep anchors "$p decays into dust." on
// the room's chain HEAD with hide_invisible == TRUE (TO_ROOM) and always
// delivers a second copy to the head itself (TO_CHAR). Whichever
// character happens to be head is therefore load-bearing: if a
// non-head occupant cannot see the head, they receive NEITHER copy of
// the message (TO_ROOM's CAN_SEE gate excludes them, TO_CHAR only ever
// targets the head). Reordering the SAME two characters so the
// formerly-non-head one becomes head flips whether they are told
// anything at all -- the census-review F9(e) "anchor identity leaks"
// finding, pinned here as a chain-order-sensitivity witness since
// nothing else in the tree does (F18).
TEST(PointUpdateInvisibleAnchorGate, HeadVisibilityGatesDeliveryAndInversionFlipsIt) {
    ScopedTestWorld test_world;
    room_by_id_total(0)->room_flags = 0;
    room_by_id_total(0)->light = 1;

    ScopedSingleZoneTable zone_table_scope;

    // point_update() unconditionally walks character_list (regen/hunger/
    // zone-power bookkeeping unrelated to this test) and object_list (the
    // decay sweep under test). Emptying character_list keeps the object
    // loop's observable output limited to exactly what this test sets up;
    // neither invisible_head nor recipient needs to be on it (the room
    // walk below is next_in_room-based, not character_list-based).
    char_data *previous_character_list = character_list;
    character_list = nullptr;

    obj_data *previous_object_list = object_list;

    // The decay path's `if (obj->obj_flags.timer == 0)` block unconditionally
    // calls extract_obj() afterward (limits.cpp:842), REGARDLESS of item
    // type -- it is not gated on ITEM_CONTAINER the way the corpse-specific
    // sub-block a few lines above it is. extract_obj() -> obj_from_room()
    // requires the object to actually be linked into the room's `contents`
    // chain (obj_to_room() below), or it null-derefs walking an empty/
    // non-matching chain; and extract_obj() -> free_obj() -> RELEASE() calls
    // free_function() on the object whenever global_release_flag is nonzero
    // (tables.h's default is 1) -- forcing it to 0 for this scope (restored
    // below) is what makes it safe to let a STACK obj_data go through this
    // production destruction path twice, matching the established
    // ScopedGlobalReleaseFlag idiom (act_wiz_format_tests.cpp) rather than
    // heap-allocating a real object just to satisfy this one code path.
    const int previous_global_release_flag = global_release_flag;
    global_release_flag = 0;

    char_data invisible_head{};
    char_data recipient{};
    descriptor_data recipient_descriptor{};
    ScopedClearCharFields invisible_head_cleanup{invisible_head};
    ScopedClearCharFields recipient_cleanup{recipient};
    ScopedDescriptorLargeOutbufReturn recipient_outbuf_cleanup{recipient_descriptor};
    clear_char(&invisible_head, MOB_ISNPC);
    clear_char(&recipient, MOB_VOID);
    invisible_head.specials2.act = MOB_ISNPC;
    invisible_head.specials.position = POSITION_STANDING;
    SET_BIT(invisible_head.specials.affected_by, AFF_INVISIBLE);
    reset_capturing_descriptor(recipient_descriptor, &recipient);
    recipient.desc = &recipient_descriptor;
    recipient.specials.position = POSITION_STANDING;
    recipient.player.race = RACE_HUMAN;

    obj_data decaying_item{};
    decaying_item.obj_flags.type_flag = ITEM_TRASH;
    decaying_item.carried_by = nullptr;
    // -1 (never a real obj_index slot) so extract_obj()'s
    // `if (obj->item_number >= 0) (obj_index_by_id(...)->number)--;` skips
    // entirely -- matches act_wiz_format_tests.cpp's identical precedent for
    // a fixture obj_data with no real prototype behind it.
    decaying_item.item_number = -1;

    {
        // Chain order: invisible_head first (the room's chain HEAD), then
        // recipient. recipient cannot see the invisible head, so neither
        // the TO_ROOM copy (CAN_SEE-gated) nor the TO_CHAR copy (head-only)
        // ever reaches them.
        ScopedRoomOccupants occupants{room_by_id_total(0), 0, {&invisible_head, &recipient}};
        decaying_item.obj_flags.timer = 1;
        object_list = &decaying_item;
        decaying_item.next = nullptr;
        // Links decaying_item into room 0's (empty) contents chain AND
        // stamps its in_room field -- extract_obj()'s obj_from_room() call
        // needs the former, the decay branch's own in_room read needs the
        // latter.
        obj_to_room(&decaying_item, 0);

        point_update();

        const std::string output = recipient_descriptor.output;
        EXPECT_EQ(output.find("decays into dust"), std::string::npos)
            << "Expected recipient to receive NEITHER copy of the decay message "
               "while the invisible occupant is the chain head.\n"
            << output;
    }

    reset_capturing_descriptor(recipient_descriptor, &recipient);
    recipient.desc = &recipient_descriptor;

    {
        // Same two characters, chain order REVERSED: recipient is now the
        // chain HEAD. The TO_CHAR copy always targets the head regardless
        // of anyone's visibility, so recipient now receives it.
        ScopedRoomOccupants occupants{room_by_id_total(0), 0, {&recipient, &invisible_head}};
        decaying_item.obj_flags.timer = 1;
        object_list = &decaying_item;
        decaying_item.next = nullptr;
        // Phase 1's point_update() call already extracted (unlinked +
        // "freed", global_release_flag == 0 above making that a no-op on
        // this stack object's actual storage) decaying_item from room 0's
        // contents chain -- re-link it for this second pass.
        room_by_id_total(0)->contents = nullptr;
        obj_to_room(&decaying_item, 0);

        point_update();

        const std::string output = recipient_descriptor.output;
        EXPECT_NE(output.find("decays into dust"), std::string::npos)
            << "Expected the chain-order inversion (recipient now the chain "
               "head) to flip recipient into receiving the TO_CHAR anchor copy "
               "of the decay message.\n"
            << output;
    }

    object_list = previous_object_list;
    character_list = previous_character_list;
    global_release_flag = previous_global_release_flag;
}
