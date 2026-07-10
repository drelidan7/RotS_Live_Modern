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
// Construction always leaves world[0] in a known-good state: allocates a
// fresh 1-room world if none exists yet, or -- if a world already exists
// (shared with other suites/instances in this process) -- frees any prior
// strdup()'d .name/.description before reassigning canonical ones, and always
// clears .people. Destruction clears .people again (defensive: nothing
// downstream should still be walking room 0's occupant list once this scope
// ends) and, only if this instance allocated the world, tears it fully down
// so the next construction starts from a clean slate.
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
// constructor dummy_room_data()-initializes the remaining prefix
// [0, room_count - 2] itself so every room in [0, room_count) is safe to
// read/write (unset otherwise: heap garbage in people/contents/
// dir_option[]/funct/bfs_*/sector_type/room_flags/light, same hazard
// documented for the single-room case below). Room 0 still gets the special
// "Testing Meadow" name/description; rooms [1, room_count) get
// dummy_room_data()'s generic "New room" naming, which is all mage_tests.cpp's
// ZoneGuard/RoomExitGuard/MageProcTest fixtures read (they only depend on
// zone/dir_option/room_flags/people, not on custom names, per-room). The
// default `room_count = 1` reproduces the original single-room behavior
// exactly (same create_bulk(1) call, same empty prefix loop, same
// top_of_world reset to 0) so the four Wave-1 single-room migrations are
// unaffected.

#include "../structs.h"
#include "../utils.h"

#include <cassert>
#include <cstdlib>

extern struct room_data world;
extern int top_of_world;

// Production room initializer from db.cpp: zeroes people/contents/
// ex_description/dir_option and resets number/zone/sector_type/room_flags/
// light. Not exposed in any header; declared here (mirrors the identical
// forward declarations in mage_tests.cpp / damage_test_context.h) so the
// multi-room constructor below can dummy-initialize the prefix
// create_bulk() itself skips.
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

            // create_bulk(room_count_) dummy_room_data()-initializes indices
            // [room_count_ - 1, room_count_ - 1 + EXTENSION_SIZE) itself; the
            // prefix [0, room_count_ - 2] gets only room_data's default
            // constructor otherwise. For room_count_ == 1 this range is
            // empty, so this loop is a no-op and behavior matches the prior
            // single-room-only constructor exactly.
            for (int index = 0; index < room_count_ - 1; ++index)
            {
                dummy_room_data(&world[index]);
            }
        }
        else
        {
            owns_world_ = false;

            // Reuse branch: a prior suite/instance already allocated
            // BASE_WORLD, and we deliberately neither resize nor re-dummy-init
            // it here -- so we rely on that existing allocation already being
            // large enough for the room_count we were asked for. create_bulk(N)
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

        // Every ensure_test_world_room() clone this fixture replaces forced
        // top_of_world back to a fixed value unconditionally on every call
        // (room_count_ - 1 is the highest room these tests need -- 0 for the
        // single-room default, preserving the original "reset to 0"
        // behavior verbatim); preserved verbatim so replaced call sites
        // behave identically.
        top_of_world = room_count_ - 1;

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
    // Whether this instance allocated BASE_WORLD (owns teardown) or found an
    // existing world already shared with other suites in this process (reset
    // room 0 only, leave the array itself alone).
    bool owns_world_;

    // Number of usable rooms requested (world[0..room_count_)); drives the
    // create_bulk() size, the dummy-init prefix loop, top_of_world, and the
    // destructor's free-range bound. 1 for every pre-existing single-room
    // caller.
    int room_count_;
};
