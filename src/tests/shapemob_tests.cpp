#include "../protos.h"
#include "../structs.h"
#include "../utils.h"
#include <gtest/gtest.h>

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
