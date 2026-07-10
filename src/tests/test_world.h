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

#include "../structs.h"
#include "../utils.h"

#include <cstdlib>

extern struct room_data world;
extern int top_of_world;

class ScopedTestWorld
{
public:
    ScopedTestWorld()
    {
        if (room_data::BASE_WORLD == nullptr)
        {
            world.create_bulk(1);
            owns_world_ = true;
        }
        else
        {
            owns_world_ = false;
        }

        // Every ensure_test_world_room() clone this fixture replaces forced
        // top_of_world back to 0 unconditionally on every call (room 0 is
        // the only room these single-room tests need); preserved verbatim so
        // replaced call sites behave identically.
        top_of_world = 0;

        room_data& room = world[0];
        std::free(room.name);
        std::free(room.description);
        room.name = str_dup("The Testing Meadow");
        room.description = str_dup("A quiet room used for account-menu tests.\n\r");
        room.people = nullptr;
    }

    ~ScopedTestWorld()
    {
        room_data& room = world[0];
        room.people = nullptr;

        if (owns_world_)
        {
            std::free(room.name);
            room.name = nullptr;
            std::free(room.description);
            room.description = nullptr;

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
    room_data& room() { return world[0]; }

private:
    // Whether this instance allocated BASE_WORLD (owns teardown) or found an
    // existing world already shared with other suites in this process (reset
    // room 0 only, leave the array itself alone).
    bool owns_world_;
};
