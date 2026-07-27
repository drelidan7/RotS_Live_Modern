#pragma once

// Shared RAII owner of the process-wide test world (room_data::BASE_WORLD /
// world[]). Unifies three independently-reimplemented `ensure_test_world_room`
// clones (interpre_account_menu_tests.cpp, db_loader_tests.cpp,
// spell_pa_tests.cpp) that all mutated the same process-global room 0 without
// any ownership/reset discipline: whichever suite ran first allocated
// BASE_WORLD via world.create_bulk(1), and every later suite's clone silently
// reused that same room 0, re-strdup()'ing .name/.description over the
// previous pointers (a leak) and never resetting .people. In the monolithic
// (non-ctest) `./ageland_tests` binary -- where all suites share one process
// -- a character left dangling in world[0].people by one test (freed at that
// test's end) survived into the next construction untouched, and a later
// suite's room-occupant walk (e.g. the account-menu reconnect path) then
// dereferenced the stale pointer: deterministic under --gtest_repeat=2,
// landing in InterpreAccountMenu.SelectingSameLinklessActiveCharacterReconnectsExistingBody's
// second pass, and ~50% of sampled --gtest_shuffle seeds. See
// docs/superpowers/phase4-seed-64bit-cosmetic.md ("Standalone monolithic
// ageland_tests cross-suite state pollution") for the full root-cause writeup.
// ctest is unaffected (gtest_discover_tests runs one test per process), so
// this is purely a monolithic-runner isolation fix, not a behavior change.
//
// Construction always leaves the whole test world in a known-good state.
// If no world exists yet this instance allocates one (the "own" branch); if a
// world already exists -- shared with other suites/instances in this process
// -- it reuses that allocation (the "reuse" branch). EITHER WAY the same
// reset_all_rooms() pass then runs (LS-3a T1 Stages A and B): every room in
// [0, room_data::BASE_LENGTH] gets its strdup()'d .name/.description freed
// and is re-run through dummy_room_data(), plus
//   * .level/.affected/.room_track[]/.bleed_track[], which dummy_room_data()
//     leaves alone but room_data's own constructor initializes, and
//   * .alignment/.bfs_dir/.bfs_next/.funct, which NEITHER initializes -- they
//     are raw bytes out of `new room_data[]` in an owning construction and
//     whatever an earlier suite left in a reused one.
// create_bulk()'s EXTENSION_ROOM_HEAD stamp on the last requested room is
// reproduced afterwards. Room 0's canonical "Testing Meadow"
// name/description are then re-established on top (that ordering is
// load-bearing: run the reset after it and room 0 would carry
// dummy_room_data()'s generic "New room" instead).
//
// The reset is what puts the monolithic (`./ageland_tests`) runner on the
// same per-room starting state ctest hands every test for free, and it closes
// the leaked-.people/.contents/.light/.dir_option/.room_flags classes
// structurally instead of by per-fixture enumeration. Running it in the own
// branch too retires three hand-rolled per-suite patch loops
// (mage_tests.cpp, pathfind_tests.cpp, act_wiz_format_tests.cpp) that each
// re-zeroed some subset of the uninitialized four. Note the reset
// deliberately does NOT change the world's SIZE: see the reuse-branch assert
// below.
//
// Destruction clears world[0].people again (defensive: nothing downstream
// should still be walking room 0's occupant list once this scope ends) and,
// only if this instance allocated the world, tears it fully down so the next
// construction starts from a clean slate.
//
// Mirrors the existing ScopedDescriptorListReset (interpre_account_menu_tests.cpp)
// / ScopedDescriptorList (act_wiz_tests.cpp) RAII convention used for the
// other process-global test fixtures (descriptor_list, player_table).
//
// Multi-room contract (added for mage_tests.cpp's ensure_test_world clone,
// the one Wave-1 Task 3 left unmigrated because this fixture was single-room
// only): pass `room_count` to guarantee rooms [0, room_count) are usable.
// world.create_bulk(room_count) only dummy_room_data()-initializes the
// trailing EXTENSION_SIZE rooms starting at index (room_count - 1); the
// constructor's reset_all_rooms() pass covers the whole allocation instead,
// so every room in [0, room_count) is safe to read/write. (Unset otherwise:
// heap garbage in people/contents/dir_option[]/funct/bfs_*/sector_type/
// room_flags/light -- the same hazard documented for the single-room case
// below, now closed for both branches by that pass.) Room 0 still gets the special
// "Testing Meadow" name/description; rooms [1, room_count) get
// dummy_room_data()'s generic "New room" naming, which is all mage_tests.cpp's
// ZoneGuard/RoomExitGuard/MageProcTest fixtures read (they only depend on
// zone/dir_option/room_flags/people, not on custom names, per-room). The
// default `room_count = 1` reproduces the original single-room behavior
// exactly (same create_bulk(1) call, same empty prefix loop, same
// top_of_world reset to 0) so the four Wave-1 single-room migrations are
// unaffected.

#include "rots/core/room.h"
#include "../utils.h"

#include <cassert>
#include <cstdlib>

extern struct room_data world;
extern int top_of_world;

// Production room initializer from db.cpp: zeroes people/contents/
// ex_description/dir_option and resets number/zone/sector_type/room_flags/
// light. Not exposed in any header; declared here (mirrors the identical
// forward declarations in mage_tests.cpp / damage_test_context.h) so
// reset_all_rooms() below can drive it over the whole world.
void dummy_room_data(room_data* room);

class ScopedTestWorld
{
public:
    // room_count: number of usable rooms, world[0..room_count). Defaults to
    // 1 to keep every existing single-room call site's behavior byte-for-byte
    // unchanged (see the multi-room contract comment above the class).
    explicit ScopedTestWorld(int room_count = 1)
        : room_count_(room_count)
    {
        if (room_data::BASE_WORLD == nullptr)
        {
            world.create_bulk(room_count_);
            owns_world_ = true;

            // No per-room work here: reset_all_rooms() below covers the whole
            // allocation. create_bulk() dummy_room_data()-initializes only
            // [room_count_ - 1, room_count_ - 1 + EXTENSION_SIZE), leaving the
            // prefix [0, room_count_ - 2] AND the very top index
            // (room_data::BASE_LENGTH) with nothing but room_data's default
            // constructor -- which itself sets 6 of the struct's ~20 members.
        }
        else
        {
            owns_world_ = false;

            // Reuse branch: a prior suite/instance already allocated
            // BASE_WORLD. reset_all_rooms() re-initializes every room in it,
            // but nothing here RESIZES it -- so we still rely on that existing
            // allocation already being large enough for the room_count we were
            // asked for. create_bulk(N)
            // allocates N + EXTENSION_SIZE rooms in one contiguous block, whose
            // valid base indices are [0, BASE_LENGTH] (BASE_LENGTH == N +
            // EXTENSION_SIZE - 1), so world[0..room_count_) stays in-bounds
            // exactly when room_count_ <= BASE_LENGTH + 1. Every current caller
            // satisfies this trivially (single-room callers need 1; mage_tests'
            // multi-room static needs 33, and its own
            // static_assert(kMageTestWorldRoomCount <= EXTENSION_SIZE) plus the
            // fact that BASE_LENGTH >= EXTENSION_SIZE for any allocation keeps
            // it safe). This assert guards ANY future multi-room caller against
            // silently overrunning an undersized reused world -- the runtime
            // analogue of damage_test_context.h's compile-time
            // static_assert(room_number < EXTENSION_SIZE - 1). NDEBUG test
            // builds compile it out, which is why mage_tests ALSO carries the
            // compile-time static_assert; the two together cover both the
            // always-on compile-time case for the known caller and the
            // debug-build runtime case for arbitrary future ones.
            assert(room_count_ <= room_data::BASE_LENGTH + 1
                && "ScopedTestWorld: requested room_count exceeds the already-allocated test world");
        }

        // Runs for BOTH branches, so an owned world and a reused one are the
        // same world as far as any test can tell.
        reset_all_rooms();

        // Every ensure_test_world_room() clone this fixture replaces forced
        // top_of_world back to a fixed value unconditionally on every call
        // (room_count_ - 1 is the highest room these tests need -- 0 for the
        // single-room default, preserving the original "reset to 0"
        // behavior verbatim); preserved verbatim so replaced call sites
        // behave identically.
        top_of_world = room_count_ - 1;

        // MUST stay after the reuse branch's reset loop: that loop
        // dummy_room_data()s room 0 along with every other room, which would
        // otherwise leave it named "New room" and break every test asserting
        // on "The Testing Meadow" (e.g. protocol_tests.cpp's
        // RoomUpdateImplSetsRoomNameVnumExitsAndTerrainWhenLocationIsNegative).
        room_data& room = world[0];
        std::free(room.name);
        std::free(room.description);
        room.name = str_dup("The Testing Meadow");
        room.description = str_dup("A quiet room used for account-menu tests.\n\r");
        room.people = nullptr;
    }

    ~ScopedTestWorld()
    {
        world[0].people = nullptr;

        if (owns_world_)
        {
            // Free every room's strdup'd name/description before dropping the
            // array: our create_bulk(room_count_) dummy_room_data()-initialized
            // every index except the very last one (each dummy'd index
            // strdup'ing both strings), and the constructor above re-strdup'd
            // room 0's. The array is sized room_count_ + EXTENSION_SIZE, so
            // the final valid index is room_count_ + EXTENSION_SIZE - 1; for
            // the single-room default (room_count_ == 1) that's EXTENSION_SIZE,
            // matching this loop's original hardcoded bound exactly. That
            // final index only ran room_data's constructor, which nulls both
            // pointers, so freeing the whole [0, room_count_ + EXTENSION_SIZE - 1]
            // range unconditionally is safe (free(nullptr) is a no-op).
            // Without this loop, every owning create/teardown cycle leaked
            // 49 rooms x 2 strings (~63 KB, measured with macOS `leaks`) --
            // per DamageTestContext construction in the monolithic runner.
            for (int index = 0; index <= room_count_ + EXTENSION_SIZE - 1; ++index)
            {
                room_data& room = room_data::BASE_WORLD[index];
                std::free(room.name);
                room.name = nullptr;
                std::free(room.description);
                room.description = nullptr;
            }

            delete[] room_data::BASE_WORLD;
            room_data::BASE_WORLD = nullptr;
            room_data::BASE_LENGTH = 0;
            room_data::TOTAL_LENGTH = 0;
            room_data::BASE_EXTENSION = nullptr;
            top_of_world = 0;
        }
    }

    ScopedTestWorld(const ScopedTestWorld&) = delete;
    ScopedTestWorld& operator=(const ScopedTestWorld&) = delete;

    // The canonical test room (world[0]); callers stamp their own .number
    // (and any other per-test fields) on it after construction.
    room_data& room()
    {
        return world[0];
    }

private:
    // Puts every room in the base world back to a known, uniform state.
    // Called from both constructor branches (LS-3a T1 Stages A and B).
    //
    // Without it, a REUSED world carries every mutation every earlier suite in
    // this process made -- occupant chains still pointing at freed stack
    // characters, .contents holding freed stack objects, leaked .light counts
    // (char_to_room() itself increments .light, so a fixture can leak one with
    // no .light token anywhere in its file), stale .room_flags/.dir_option[]/
    // .number. Enumerating those per fixture provably cannot close the class;
    // resetting the world structurally does. And an OWNED world is not clean
    // either: `new room_data[]` runs a constructor that sets only 6 of the
    // struct's ~20 members, and create_bulk()'s dummy_room_data() pass covers
    // neither all of them nor all of the array.
    //
    // The window is [0, BASE_LENGTH] INCLUSIVE and indexes
    // room_data::BASE_WORLD DIRECTLY, never world[index]: room_data's
    // operator[] routes any index >= BASE_LENGTH into the extension-chain
    // fallback (which mudlogs, and exit(0)s for one index), so the top of the
    // base array is simply not reachable through it. The inclusive bound
    // matches the destructor's own free range below -- create_bulk(N)'s
    // dummy_room_data() pass covers [N-1, N-1+EXTENSION_SIZE) and therefore
    // stops one short of BASE_LENGTH (== N+EXTENSION_SIZE-1), leaving that
    // last room constructor-only.
    void reset_all_rooms()
    {
        for (int index = 0; index <= room_data::BASE_LENGTH; ++index)
        {
            room_data& reset_room = room_data::BASE_WORLD[index];

            // dummy_room_data() str_dup()s a fresh name/description WITHOUT
            // freeing the old ones, so free first -- a bare call would leak
            // two strings per room per construction.
            std::free(reset_room.name);
            std::free(reset_room.description);
            dummy_room_data(&reset_room);

            // Set by room_data's constructor but not by dummy_room_data().
            // .level is live production state, not padding: do_stat_room and
            // do_where both read it.
            reset_room.level = 0;
            reset_room.affected = nullptr;
            for (int track = 0; track < NUM_OF_TRACKS; ++track)
            {
                reset_room.room_track[track] = room_track_data{};
            }
            for (int trail = 0; trail < NUM_OF_BLOOD_TRAILS; ++trail)
            {
                reset_room.bleed_track[trail] = room_bleed_data{};
            }

            // Set by NEITHER room_data's constructor nor dummy_room_data()
            // (LS-3a T1 Stage B). In an owning construction these are raw
            // bytes from `new room_data[]`; a plain macOS build's allocator
            // happens to hand back zeroed pages, but macOS ASan's does not --
            // act_wiz_format_tests.cpp's RoomStatContext hit exactly that as
            // do_stat_room reporting SpecProc "Exists" instead of "No" off a
            // garbage .funct. .bfs_dir/.bfs_next are find_first_step()'s BFS
            // scratch state, which its clear-marks loop walks over every room
            // in [0, top_of_world]. .alignment has no reader or writer
            // anywhere in the tree (declaration only, room.h) and is zeroed
            // here purely so the whole struct has one defined starting state.
            reset_room.alignment = 0;
            reset_room.bfs_dir = 0;
            reset_room.bfs_next = nullptr;
            reset_room.funct = nullptr;
        }

        // create_bulk(N) stamps its last requested room (index N-1, the head
        // of the trailing extension window) with EXTENSION_ROOM_HEAD instead
        // of dummy_room_data()'s -1. The loop above just undid that, so
        // reproduce it: DamageTestContext (which feeds the seed42
        // characterization golden) and PoisonRemovalScriptTest both depend on
        // the stamp being there.
        room_data::BASE_WORLD[room_count_ - 1].number = EXTENSION_ROOM_HEAD;
    }

    // Whether this instance allocated BASE_WORLD (owns teardown) or found an
    // existing world already shared with other suites in this process (in
    // which case the array itself is left alone -- only its contents are
    // reset).
    bool owns_world_;

    // Number of usable rooms requested (world[0..room_count_)); drives the
    // create_bulk() size, reset_all_rooms()'s EXTENSION_ROOM_HEAD stamp,
    // top_of_world, and the destructor's free-range bound. 1 for every
    // pre-existing single-room caller.
    int room_count_;
};
