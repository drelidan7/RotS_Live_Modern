#include "../db.h"
#include "../handler.h"
#include "../rots_rng.h"
#include "../spells.h"
#include "../utils.h"
#include "damage_test_context.h"
#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <new> // placement-new (reconstruct the stack victim after free_char, RAII T6 MSVC fix)
#include <sstream>
#include <string>

// Note on floating point: this test binary is built with -msse2 -mfpmath=sse
// (see src/tests/Makefile), so number()'s double arithmetic runs SSE, while
// the 32-bit game binary (src/Makefile) runs x87 (80-bit extended). rots_rng
// itself is an integer-valued std::mt19937, so the draws feeding dam/location
// below are identical either way; only number()'s float-to-range scaling
// differs, and that scaling is deterministic and platform-stable under SSE.
// Every future build of this repo (and any 64-bit successor) is SSE, so this
// golden is the correct long-term characterization basis, not a compromise.

int damage(char_data *attacker, char_data *victim, int dam, int attacktype, int hit_location);

extern char_data *combat_list;
extern char_data *combat_next_dude;
extern char_data *character_list;
extern obj_data *object_list;
extern int global_release_flag;
extern index_data *mob_index;
extern int top_of_mobt;

namespace {

// ROTS_GOLDEN_DIR (set by src/CMakeLists.txt on the ageland_tests target) anchors
// this to an absolute path so the compare works under ctest, whose
// gtest_discover_tests runs the binary with WORKING_DIRECTORY at the repo root
// rather than src/tests/. The src/tests/Makefile build doesn't define it, so it
// falls back to the plain relative path it has always used (cwd is src/tests/
// there).
#ifdef ROTS_GOLDEN_DIR
const char *const kGoldenPath = ROTS_GOLDEN_DIR "/combat_transcript_seed42.txt";
#else
const char *const kGoldenPath = "goldens/combat_transcript_seed42.txt";
#endif

std::string read_file(const char *path) {
    std::ifstream in(path);
    std::ostringstream contents;
    contents << in.rdbuf();
    return contents.str();
}

// RAII guard restoring global_release_flag on every exit path (including the
// early `return` in the UPDATE_GOLDENS branch below).
struct ReleaseFlagGuard {
    int saved_value = global_release_flag;

    explicit ReleaseFlagGuard(int temporary_value) { global_release_flag = temporary_value; }

    ~ReleaseFlagGuard() { global_release_flag = saved_value; }
};

// db_boot() normally populates mob_index (one slot per mob-file prototype)
// before anything can die; test builds never call it. The live death path
// (raw_kill() -> make_physical_corpse()) unconditionally indexes
// mob_index[victim->nr] for any IS_NPC victim, regardless of nr, so a victim
// that might die needs a real (if fabricated) slot rather than nr == -1.
// Idempotent/leaked-on-purpose: this is a one-time, process-lifetime table,
// exactly like the real db_boot() call it stands in for.
void ensure_test_mob_index() {
    if (mob_index) {
        return;
    }

    CREATE(mob_index, index_data, 1);
    mob_index[0].virt = 0;
    mob_index[0].number = 0;
    mob_index[0].func = nullptr;
    top_of_mobt = 0;
}

} // namespace

class CharacterizationCombatTest : public ::testing::Test {
  protected:
    void TearDown() override {
        combat_list = nullptr;
        combat_next_dude = nullptr;
        character_list = nullptr;

        // Safety net: if the victim never died (rots_rng behavior drifted so
        // the seed-42 sequence no longer kills within 100 rounds), release
        // its claimed char_control_array slot so it doesn't leak across the
        // rest of this test binary's process lifetime. If it did die, the
        // live extract_char()/free_char() path already released it and this
        // is a harmless no-op (remove_char_exists() just clears a bit).
        if (claimed_abs_number >= 0) {
            remove_char_exists(claimed_abs_number);
        }

        // Frees the corpse make_physical_corpse() (fight.cpp) built, if the
        // transcript killed the victim. Already unlinked from object_list /
        // world[room].contents by the test body -- nothing in production
        // still references it -- so this is a plain deallocation, not a
        // scaled-down extract_obj(): CREATE()/str_dup() route through
        // create_function(), which is calloc(); rots_asprintf() (used by
        // get_corpse_desc() for description/short_description) is malloc();
        // both are plain free()-compatible, and corpse->contains/ex_description
        // are guaranteed null here (DamageTestContext's victim carries
        // nothing and owns no wear-slot equipment, so make_physical_corpse()
        // never populates them). Test-only: production's make_corpse()/
        // make_physical_corpse() path is untouched.
        if (corpse_to_free != nullptr) {
            std::free(corpse_to_free->name);
            std::free(corpse_to_free->description);
            std::free(corpse_to_free->short_description);
            std::free(corpse_to_free);
            corpse_to_free = nullptr;
        }
    }

    int claimed_abs_number = -1;

    // Corpse make_physical_corpse() (fight.cpp) allocated during the test, if
    // the seed-42 transcript killed the victim; set by the test body once
    // it's unlinked from object_list/world[room].contents, freed above.
    obj_data *corpse_to_free = nullptr;
};

// Characterization, not specification: this pins CURRENT behavior of the live
// damage path (fight.cpp::damage()) under a fixed PRNG seed. If a refactor
// changes this transcript, the refactor changed game behavior.
TEST_F(CharacterizationCombatTest, DamageTranscriptSeed42) {
    // Reuse the exact context type from damage_tests.cpp, lifted into
    // damage_test_context.h so both TUs share one definition.
    DamageTestContext context;

    // The victim is a stack-resident char_data (DamageTestContext's design,
    // shared with damage_tests.cpp's non-lethal cases). If this transcript's
    // rolls kill the victim, the live death path (die() -> raw_kill() ->
    // make_physical_corpse()/extract_char() -> free_char()) will run against
    // it. Four adaptations keep that safe without touching production code:
    //  - nr = 0 plus a fabricated one-slot mob_index table (below) gives the
    //    victim a valid mob-prototype index, since make_physical_corpse()
    //    indexes mob_index[victim->nr] unconditionally for any IS_NPC victim.
    //  - global_release_flag = 0 is an existing production knob (see
    //    act_wiz.cpp's copyover path) that makes RELEASE() skip its free()
    //    call; without it, free_char() would call free() on this
    //    non-heap-allocated char_data, which is undefined behavior.
    //  - character_list must contain the victim: extract_char() unlinks the
    //    dying character from this global roster and SYSERR-aborts if it
    //    can't find it there (every live character is always on this list;
    //    DamageTestContext only wires up the room-level people list).
    //  - abs_number must come from register_npc_char(), not the char_data{}
    //    default of 0: extract_char()/free_char() call
    //    remove_char_exists(victim->abs_number) unconditionally on death,
    //    which clears that slot's bit in the process-lifetime,
    //    test-binary-wide char_control_array. abs_number 0 is exactly the
    //    slot other tests' registered characters tend to claim first, so
    //    reusing the char_data{} default would clear a live character's
    //    "exists" bit out from under an unrelated, later-running test —
    //    exactly the cross-test corruption this call avoids.
    ensure_test_mob_index();
    context.victim.nr = 0;
    context.victim.next = nullptr;
    character_list = &context.victim;
    claimed_abs_number = register_npc_char(&context.victim);
    ReleaseFlagGuard release_flag_guard(0);

    rots_rng::seed(42u);

    std::ostringstream transcript;
    // Set once the live death path frees the stack victim; drives the
    // in-place reconstruction after the loop (RAII T6 MSVC double-dtor fix).
    bool victim_perished = false;
    for (int round = 0; round < 100; ++round) {
        // Rolls drive dam and hit_location through the same public RNG the
        // game uses, so the transcript covers armor/location/death handling.
        // TYPE_HIT (not SKILL_BAREHANDED) is used deliberately: fight.cpp's
        // IS_PHYSICAL(attacktype) macro only recognizes attacktype values in
        // [TYPE_HIT, TYPE_CRUSH], and damage_tests.cpp already exercises the
        // live path via TYPE_HIT for the same reason (it drives the
        // Beorning/wild-resistance/hallucination branches that a
        // physical-melee golden should cover).
        int dam = number(1, 60);
        int location = number(0, MAX_BODYPARTS - 1);
        int result = damage(&context.attacker, &context.victim, dam, TYPE_HIT, location);
        // NOTE: when this call kills the victim it runs the live death path
        // (die -> raw_kill -> extract_char -> free_char) on the STACK victim,
        // so by the time damage() returns `context.victim` has ALREADY had its
        // ~char_data() run (global_release_flag=0 skips only free(), not the
        // RAII T6a explicit destructor). The GET_HIT/GET_POSITION reads below
        // touch only POD members, which survive intact in the un-free()'d
        // storage -- but see victim_perished handling after the loop.
        transcript << round << ' ' << dam << ' ' << location << ' ' << result << ' '
                   << GET_HIT(&context.victim) << '\n';
        if (GET_POSITION(&context.victim) == POSITION_DEAD) {
            transcript << "victim dead at round " << round << '\n';
            victim_perished = true;
            break;
        }
    }

    // RAII T6 MSVC fix: if the fight killed the victim, free_char() already ran
    // ~char_data() on `context.victim` (destroying its owning members --
    // damage_details' std::map, the alias/poof/skills members, etc.). The
    // victim is a stack member of DamageTestContext (damage_test_context.h),
    // so scope exit will run ~char_data() on it a SECOND time -- a double
    // destruction. glibc/libc++ tolerated it because the members were logically
    // empty on this path, but MSVC's std::map keeps a heap-allocated sentinel
    // node even when empty, so the second destructor double-frees it (observed
    // as SEH 0xc0000005 on the windows-msvc CI job; ASan/glibc stayed silent).
    // Reconstruct the object in place so scope exit destroys a fresh, validly
    // constructed empty char_data exactly once -- the destruction-side mirror
    // of clear_char()/read_mobile()'s placement-new construction contract, and
    // the symmetric partner to the explicit ~char_data() T6a added to
    // free_char(). Guarded on victim_perished: if the victim never died,
    // free_char() never ran, the object is still live, and reconstructing over
    // it would leak -- so we leave it untouched for its single normal dtor.
    if (victim_perished) {
        new (&context.victim) char_data();
    }

    // The death path built a corpse object and pushed it onto two globals
    // that outlive this test: object_list (make_physical_corpse) and
    // world[room].contents (obj_to_room). Unlink it from both so no later
    // test in this binary observes our leftovers; nothing else allocates
    // objects between the kill and here, so the corpse is at the head of
    // both lists when it exists. Handed to TearDown() (corpse_to_free) for
    // the actual deallocation.
    obj_data *corpse = world[DamageTestContext::room_number].contents;
    if (corpse != nullptr && corpse == object_list) {
        world[DamageTestContext::room_number].contents = corpse->next_content;
        object_list = corpse->next;
        corpse_to_free = corpse;
    }

    if (std::getenv("UPDATE_GOLDENS") != nullptr) {
        std::ofstream out(kGoldenPath);
        out << transcript.str();
        SUCCEED() << "golden updated";
        return;
    }

    EXPECT_EQ(read_file(kGoldenPath), transcript.str())
        << "Combat transcript drifted from golden. If the change is intentional, "
           "rerun with UPDATE_GOLDENS=1 and commit the new golden.";
}
