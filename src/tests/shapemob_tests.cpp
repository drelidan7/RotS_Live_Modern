#include "../db.h"
#include "../handler.h"
#include "../protos.h"
#include "rots/core/character.h"
#include "rots/core/descriptor.h"
#include "rots/core/types.h"
#include "../utils.h"
#include "test_char_cleanup.h"
#include "test_world.h"
#include <gtest/gtest.h>

#include <cstdlib>
#include <new>

// Phase 2b final-review fix wave, Critical 1: src/shapemob.cpp's new_mob() and the
// shape-editor mob-file load path both did CREATE1(SHAPE_PROTO(ch)->proto, char_data)
// -- raw calloc'd storage, no constructor ever run -- and implement_proto() then
// bulk-memcpy'd that unconstructed proto over a CONSTRUCTED mob_proto[] element,
// zeroing its live std::map header (player_damage_details::damage_map). Any
// subsequent mutation of (or copy from) that corrupted map is undefined behavior --
// deterministic SIGSEGV under libc++/macOS. This test drives new_mob() +
// implement_proto() exactly as the real `shape mobile` -> `implement` command flow
// does, without booting the full server, and exercises both corruption symptoms the
// task brief called out: a direct mutation of the (post-implement) mob_proto slot,
// and a read_mobile()-style copy-assignment FROM it.
//
// This is a real RED/GREEN test: reverting the placement-new/copy-assignment fix in
// shapemob.cpp reproduces a crash here (confirmed manually during the fix); with the
// fix applied, this test passes cleanly.

extern void new_mob(struct char_data* ch);
extern void implement_proto(struct char_data* ch);
extern void clear_char(struct char_data* ch, int mode);
extern struct char_data* mob_proto;
extern int num_of_programs;
// The read_mobile()-driven test below's globals (mirrors
// load_room_placement_tests.cpp's ScopedFollowerPrototype -- that class
// lives in that file's own anonymous namespace, unreachable from here).
extern struct index_data* mob_index;
extern int top_of_mobt;
extern struct char_data* character_list;

namespace {

// RAII guard restoring the globals this test fakes out, on every exit path --
// mirrors ReleaseFlagGuard in characterization_combat_tests.cpp.
struct ShapeMobGlobalsGuard {
    struct char_data* saved_mob_proto = mob_proto;
    int saved_num_of_programs = num_of_programs;

    ~ShapeMobGlobalsGuard()
    {
        mob_proto = saved_mob_proto;
        num_of_programs = saved_num_of_programs;
    }
};

} // namespace

TEST(ShapeMob, ImplementProtoCopiesMobProtoWithoutCorruptingItsDamageMap)
{
    ShapeMobGlobalsGuard globals_guard;

    // A single-slot fake mob_proto[], constructed exactly like load_mobiles() does
    // via clear_char() -- i.e. a real, live target with a validly-constructed
    // std::map, standing in for the real per-boot prototype table.
    struct char_data fake_mob_proto[1];
    clear_char(&fake_mob_proto[0], MOB_ISNPC);
    // Releases fake_mob_proto[0].profs (clear_char()'s only heap allocation
    // for MOB_ISNPC) at scope exit (Phase 5 T6 leak sweep).
    ScopedClearCharFields fake_mob_proto_cleanup { fake_mob_proto[0] };
    mob_proto = fake_mob_proto;

    // real_program() is reached (new_mob() below never sets MOB_SPEC), but its
    // supporting mobile_program_zone[]/num_of_programs table is never booted in
    // this test binary. num_of_programs = -1 makes real_program()'s loop
    // condition (tmp <= num_of_programs) false on the very first check, so it
    // returns 0 without ever touching mobile_program_zone.
    num_of_programs = -1;

    // Give the pre-fix target something real to lose: an actual tree node, not
    // just an empty-but-valid map (corrupting an already-empty map wouldn't
    // reliably crash on the very next operation the way corrupting a non-empty
    // one does).
    fake_mob_proto[0].damage_details.add_damage(1, 5);

    // The shape-editor character driving `shape mobile` / `implement`.
    struct char_data editor;
    clear_char(&editor, MOB_VOID);
    // Releases editor.profs/skills/knowledge (clear_char() heap allocations)
    // at scope exit (Phase 5 T6 leak sweep).
    ScopedClearCharFields editor_cleanup { editor };

    struct shape_proto sp {
    };
    editor.temp = &sp;

    // Exercises the CREATE1(SHAPE_PROTO(ch)->proto, char_data) + placement-new
    // fix at the new_mob() call site.
    new_mob(&editor);
    ASSERT_NE(sp.proto, nullptr);

    // Simulate "loaded an existing mob" (the mob-file load path sets proto->nr =
    // real_mobile(vnum); here we set the resulting array index directly, since
    // implement_proto() only ever uses it as an index into mob_proto, never
    // re-resolving it) so implement_proto() doesn't bail out on the "can't
    // implement a fresh mob" guard.
    sp.proto->nr = 0;
    sp.flags = SHAPE_PROTO_LOADED;
    sp.permission = 1;

    // Exercises the memcpy (pre-fix) vs. copy-assignment (post-fix) fix in
    // implement_proto().
    implement_proto(&editor);

    // Symptom 1: mutating the just-implemented mob_proto slot. A raw memcpy of
    // SHAPE_PROTO(ch)->proto's zeroed-but-never-constructed map header would
    // have left fake_mob_proto[0].damage_details holding a corrupt std::map;
    // any insertion into it (operator[] via add_damage) dereferences the
    // corrupt tree and crashes. The fix's copy-assignment instead properly
    // clears/reconstructs the map from the (empty) source, so this succeeds.
    fake_mob_proto[0].damage_details.add_damage(2, 10);

    // Symptom 2, the exact mechanism the task brief named: "the next
    // read_mobile() copy-assignment copy FROM the corrupted map -> libc++
    // segfault". read_mobile() does `new (mob) char_data(); *mob =
    // mob_proto[i];` -- reproduce that shape directly.
    struct char_data spawned;
    new (&spawned) char_data();
    spawned = mob_proto[0];

    SUCCEED();
}

// LS-3b Wave T2 (ruling R-3b-B; ls3b-census-review.md Table 1c / F2 / F11 --
// P3/P4/P8). Evidence backing the shapemob.cpp:268 disposition: no reader
// anywhere in the tree (product or test) dereferences a mob_proto[] entry's
// `.in_room` -- confirmed by an exhaustive grep of every mob_proto[
// consumer (act_obj1.cpp/act_wiz.cpp/ban.cpp/db_world.cpp/graph.cpp; none
// read `.in_room`) -- and shapemob.cpp's OWN editor functions (list_proto,
// list_simple_proto, implement_proto) never read
// SHAPE_PROTO(ch)->proto->in_room either. The only path that turns a
// mob_proto[] slot into a live, placeable char_data is read_mobile()
// (db_world.cpp), which unconditionally re-stamps NOWHERE
// (`set_location(mob, NOWHERE);`, db_world.cpp:1064) BEFORE anything else
// touches the freshly copied mob -- so whatever new_mob() stamped into the
// proto's `in_room` at construction is provably unobservable downstream.
// This pair proves that claim two ways: the first pins the fixed
// production value directly; the second proves the FULLY SPAWNED mob's
// location is identical regardless of what the proto held, i.e. the
// characterization the disposition calls for.

TEST(ShapeMob, NewMobStampsTheProtoAtNowhereRegardlessOfTheEditorsRoom)
{
    ShapeMobGlobalsGuard globals_guard;

    struct char_data editor;
    clear_char(&editor, MOB_VOID);
    ScopedClearCharFields editor_cleanup { editor };

    // The editing immortal stands in a real, non-trivial room -- new_mob()
    // must not carry that room into the prototype it creates.
    set_location(&editor, 42);

    struct shape_proto sp {
    };
    editor.temp = &sp;

    new_mob(&editor);
    ASSERT_NE(sp.proto, nullptr);

    EXPECT_EQ(location_of(sp.proto), NOWHERE);

    // new_mob() CREATE1()s+placement-news sp.proto itself (not a mob_proto[]
    // slot) -- release it the same way implement_proto()'s real callers
    // eventually do, so this test leaks neither the char_data nor its heap
    // fields (player.name/short_descr/long_descr/description, all
    // CREATE()'d above).
    RELEASE(sp.proto->player.name);
    RELEASE(sp.proto->player.short_descr);
    RELEASE(sp.proto->player.long_descr);
    RELEASE(sp.proto->player.description);
    sp.proto->~char_data();
    RELEASE(sp.proto);
}

TEST(ShapeMob, ImplementProtoAndReadMobileSpawnAnIdenticallyLocationlessMobRegardlessOfTheProtoStamp)
{
    ShapeMobGlobalsGuard globals_guard;
    // mob_index/top_of_mobt/character_list also get faked out for this test
    // -- restored the same RAII way, mirroring
    // load_room_placement_tests.cpp's ScopedFollowerPrototype (this file's
    // TU can't reach that class directly -- it lives in that file's own
    // anonymous namespace -- so the minimal slice this test needs is
    // duplicated here rather than shared, matching this file's existing
    // convention of small per-TU test-only helpers).
    struct index_data* const previous_mob_index = mob_index;
    const int previous_top_of_mobt = top_of_mobt;
    struct char_data* const previous_character_list = character_list;

    struct char_data fake_mob_proto[1];
    clear_char(&fake_mob_proto[0], MOB_ISNPC);
    ScopedClearCharFields fake_mob_proto_cleanup { fake_mob_proto[0] };
    mob_proto = fake_mob_proto;
    num_of_programs = -1;

    struct index_data fake_mob_index[1] {};
    fake_mob_index[0].virt = 0;
    fake_mob_index[0].number = 0;
    fake_mob_index[0].func = nullptr;
    mob_index = fake_mob_index;
    top_of_mobt = 0;

    struct char_data editor;
    clear_char(&editor, MOB_VOID);
    ScopedClearCharFields editor_cleanup { editor };

    // Deliberately stamps the proto with a LIVE, non-trivial room -- the
    // pre-fix shape this test's name says is now unobservable. Bypasses
    // new_mob() (which as of this commit always stamps NOWHERE) to
    // reproduce the OLD stamp directly, so this test's invariance claim
    // does not depend on new_mob()'s own fix holding.
    struct char_data proto_storage;
    new (&proto_storage) char_data();
    proto_storage.player.name = const_cast<char*>("golem");
    proto_storage.player.short_descr = const_cast<char*>("golem");
    proto_storage.player.long_descr = const_cast<char*>("golem");
    proto_storage.player.description = const_cast<char*>("golem\n\r");
    proto_storage.specials2.act = MOB_ISNPC;
    proto_storage.nr = 0;
    // Non-zero abilities so read_mobile()'s "stats fixed" mudlog path stays
    // quiet and get_naked_perception()/get_naked_willpower() read sane
    // values (mirrors ScopedFollowerPrototype's own setup).
    proto_storage.abilities.str = 100;
    proto_storage.abilities.intel = 100;
    proto_storage.abilities.wil = 100;
    proto_storage.abilities.dex = 100;
    proto_storage.abilities.con = 100;
    proto_storage.abilities.lea = 100;
    proto_storage.abilities.hit = 100;
    proto_storage.tmpabilities = proto_storage.abilities;
    proto_storage.player.level = 5;
    set_location(&proto_storage, 42); // the live-rnum stamp under test

    struct shape_proto sp {
    };
    sp.proto = &proto_storage;
    sp.flags = SHAPE_PROTO_LOADED;
    sp.permission = 1;
    editor.temp = &sp;

    // LEAK HYGIENE (LS-3b T9b, CI sanitize-linux LSan: 349 bytes in 5
    // allocations). implement_proto()'s wholesale `*proto =
    // *SHAPE_PROTO(ch)->proto;` copy-assignment overwrites
    // fake_mob_proto[0].profs with proto_storage's (null) one, orphaning the
    // block clear_char() allocated above -- exactly the ownership transfer
    // sanitize.supp documents for the pre-existing
    // ImplementProtoCopiesMobProtoWithoutCorruptingItsDamageMap test.
    // release_profs_now() exists for this shape (see test_char_cleanup.h):
    // it frees the block NOW and disarms the guard's own destructor, which
    // would otherwise free the null the copy leaves behind.
    fake_mob_proto_cleanup.release_profs_now();

    implement_proto(&editor);
    // mob_proto[0].in_room now holds whatever proto_storage's did (42,
    // reproducing the pre-fix shape).

    // Drives the REAL read_mobile() (db_world.cpp) against that slot --
    // type REAL uses the index directly, no real_mobile() lookup needed.
    // This is the characterization the disposition calls for: a genuinely
    // SPAWNED mob's location must be NOWHERE regardless of what the proto
    // it was copied from held.
    struct char_data* spawned = read_mobile(0, REAL);
    ASSERT_NE(spawned, nullptr);

    EXPECT_EQ(location_of(spawned), NOWHERE);

    // Unwinds everything read_mobile() did: the character_list prepend and
    // the process-global char_exists bit register_npc_char() set (mirrors
    // load_room_placement_tests.cpp's release_spawned_follower(); spawned
    // was never room-linked, so there is no occupant chain to unlink here).
    // NOT RELEASE(spawned->profs): read_mobile()'s `*mob = mob_proto[i]` is
    // a copy-assignment, so spawned SHARES proto_storage's profs allocation
    // (proto_storage never allocated one -- clear_char() was never run on
    // it -- so there is nothing to double-free either way).
    remove_char_exists(spawned->abs_number);
    spawned->~char_data();
    std::free(spawned);

    // The other four LSan-reported blocks: implement_proto() CREATE()s a
    // fresh copy of each of the four name/descr strings into its
    // destination slot (shapemob.cpp), and nothing in the editor flow ever
    // frees them. fake_mob_proto[0] is a stack object that never reaches
    // free_char(), so this test owns their release -- and it must happen
    // AFTER `spawned` is torn down, since read_mobile()'s copy-assignment
    // left `spawned` sharing these very pointers. proto_storage's own four
    // fields are string literals (const_cast above) and are deliberately
    // NOT released.
    RELEASE(fake_mob_proto[0].player.name);
    RELEASE(fake_mob_proto[0].player.short_descr);
    RELEASE(fake_mob_proto[0].player.long_descr);
    RELEASE(fake_mob_proto[0].player.description);

    character_list = previous_character_list;
    mob_index = previous_mob_index;
    top_of_mobt = previous_top_of_mobt;
}

// ---------------------------------------------------------------------------
// RR Wave R3 Task 1b -- shape_center()'s dispatch-invariant guard (owner
// ruling R3-O-1; docs/superpowers/specs/2026-08-21-rr3-combat-design.md
// section 2).
//
// shape_center() is command_interpreter's OLC BYPASS: interpre.cpp:987 (the
// '/' prefix form) and :996 (the already-POSITION_SHAPING form) both call it
// and return, AHEAD of even the position check, so an actor arriving here has
// passed neither a placement nor a position test (dispatch census M-1b) --
// which is why it carries its own guard rather than inheriting
// command_interpreter's. Three of Wave R2's six deferred ledger rows sit
// under exactly this bypass.
//
// DISCRIMINATOR: `ch->temp` is null on a zero-initialized character, so the
// real body takes its else arm and sends one literal line. Unplaced -> the
// guard returns before that send; placed -> the line arrives. The pair needs
// no shape state, no editor table and no world beyond the one room the
// placed half stands the actor in.
// ---------------------------------------------------------------------------

namespace {

// Mirrors act_othe_tests.cpp's/mystic_tests.cpp's own per-file copy of this
// helper (duplicated, not shared -- each copy lives in a different TU's
// anonymous namespace): points a descriptor's output at its OWN small_outbuf
// so send_to_char() output can be inspected directly. CRITICAL: mutates the
// caller's descriptor_data in place -- never replace with a version that
// returns a descriptor_data by value (descriptor_data::output is a
// self-pointer into small_outbuf[]).
void reset_capturing_descriptor(descriptor_data& descriptor, char_data* character)
{
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = 0; // CON_PLAYING
    descriptor.character = character;
}

constexpr const char* kShapeCenterUsageLine
    = "Use 'shape <mob|obj|room|script> <number>' to start shaping.\n\r";

} // namespace

TEST(ShapeCenterDispatchInvariant, RefusesToDispatchForAnUnplacedActor)
{
    ScopedTestWorld test_world;
    char_data ch {};
    descriptor_data descriptor {};
    reset_capturing_descriptor(descriptor, &ch);
    ch.desc = &descriptor;
    ch.temp = nullptr;
    set_location(&ch, NOWHERE);

    shape_center(&ch, mutable_arg(""));

    EXPECT_STREQ(descriptor.small_outbuf, "")
        << "Expected the dispatch-invariant guard to return before shape_center's own body ran "
           "-- not even its no-shape-state usage line should reach the actor.";
}

TEST(ShapeCenterDispatchInvariant, DispatchesForAPlacedActor)
{
    ScopedTestWorld test_world;
    char_data ch {};
    descriptor_data descriptor {};
    reset_capturing_descriptor(descriptor, &ch);
    ch.desc = &descriptor;
    ch.temp = nullptr;
    set_location(&ch, 0);

    shape_center(&ch, mutable_arg(""));

    EXPECT_STREQ(descriptor.small_outbuf, kShapeCenterUsageLine)
        << "Expected the identical fixture with a PLACED actor to reach the real body's else arm "
           "-- the guard must not block a normal OLC entry.";
}
