#pragma once

// Shared RAII fixtures for the character-location representation, LS-3a Wave
// T1 (.superpowers/sdd/ls3a-global-constraints.md rulings R-B1/R-B3, census B).
//
// LS-3a routes every production location MUTATION through the Stage-1
// Placement API so that after the wave nothing outside rots_entity's
// placement/containment core touches the location representation at all --
// read or write, production or test. The test tier is the larger half of that
// job: census B counted 388 raw representation sites across 23 test files that
// collapse into roughly ten raw lines here. THIS HEADER IS THE INTENDED HOME
// FOR THOSE LINES -- T4 allow-lists it as a whole file when it retires
// tools/location_read_census.py's src/tests deferral, exactly as
// entity/placement.cpp and entity/containment.cpp are allow-listed on the
// production side. Every raw representation touch below therefore carries its
// own `// LS1-ALLOW:` annotation as well, so the file reads correctly under
// either regime and no future reader has to guess which lines were deliberate.
//
// Everything a helper here can express through the Placement API, it does:
// character locations are written with set_location() (handler.h), never with
// a bare `ch->in_room = X`. Per ruling R-B2 there is deliberately NO test-tier
// alias or wrapper around set_location() -- it is the exact symbol LS-3b
// re-implements, and a shadow copy would let the two drift and hide the whole
// test tier from the representation swap. Only the intrusive chain linkage
// itself (room_data::people / char_data::next_in_room), which the Stage-1 API
// deliberately does not expose a mutator for this wave, is raw.
//
// These fixtures follow the established test-tier RAII convention
// (ScopedTestWorld in test_world.h, ScopedDescriptorList, ScopedZoneTable,
// ScopedVnumWorld): construction installs, destruction unconditionally
// restores -- including when a test body fails an assertion and unwinds early
// -- so no later test in the monolithic single-process ./ageland_tests binary
// ever observes a fixture still installed. That matters more than usual here:
// THE FIXTURE-HYGIENE RULE this wave operates under exists because ctest is
// structurally blind to a leaked pointer (it runs every test in its own
// process), and leaving a stack char_data linked into a process-global room's
// occupant chain cost LS-2 three separate incidents.

#include "../handler.h"
#include "../zone.h"
#include "rots/core/character.h"
#include "rots/core/room.h"
#include "rots/core/types.h"

#include <initializer_list>
#include <vector>

// Publishes an ordered set of characters as a room's occupant chain for the
// duration of a scope, and takes them back out again afterwards.
//
// PUBLICATION ORDER IS THE CONTRACT: the characters appear in the chain IN THE
// ORDER GIVEN -- the first argument becomes room->people, the second follows
// it, and so on, with the last one's next_in_room nulled. That matches the
// head-publish idiom all 22 hand-rolled fixtures this helper replaces already
// use, so their assertions carry over unchanged.
//
// IT DELIBERATELY DOES NOT CALL char_to_room() (ruling R-B3, two independent
// reasons, both verified):
//   (i)  char_to_room() dispatches zone_by_id() for every non-NPC character,
//        and top_of_zone_table is 0 in nearly every fixture in this binary, so
//        zone_by_id(0) hands back nullptr and the increment segfaults. Only a
//        fixture that stands a zone table up -- ScopedZoneTableOwner below --
//        can drive char_to_room() with a PC at all.
//   (ii) char_to_room() appends at the TAIL. Publishing through it would flip
//        the occupant order every one of those 22 fixtures asserts on.
// It also performs no light or zone white/dark-power bookkeeping, for the same
// reason: those belong to char_to_room()/detach_char_from_room(), and a
// fixture that silently ran them would make every light/power delta a test
// measures unreadable. A test that wants the real bookkeeping should call the
// real primitive (see ScopedZoneTableOwner's contract below); this helper is
// for tests that just need characters to BE somewhere.
//
// The room id is a constructor parameter rather than something derived from
// the room pointer, deliberately: most fixtures in this binary use a stub
// room_data that lives on the stack and is not in world[] at all, so there is
// no rnum to recover from it, and room_data::number is a VNUM even when there
// is one.
class ScopedRoomOccupants
{
public:
    // room must be non-null. occupants may be empty, in which case the room is
    // simply published empty for the scope (and its prior chain restored after).
    ScopedRoomOccupants(room_data* room, int room_id, std::initializer_list<char_data*> occupants)
        : m_room(room)
        , m_room_id(room_id)
        , m_saved_people(room->people) // LS1-ALLOW: representation-impl (fixture helper -- saving the chain head it is about to displace)
        , m_occupants(occupants)
    {
        char_data* previous = nullptr;
        for (char_data* occupant : m_occupants)
        {
            if (previous == nullptr)
            {
                m_room->people = occupant; // LS1-ALLOW: representation-impl (fixture helper -- publishing the chain head, the idiom char_to_room() cannot express here per R-B3)
            }
            else
            {
                previous->next_in_room = occupant; // LS1-ALLOW: representation-impl (fixture helper -- linking the chain in the order given)
            }

            // The Placement API, not a bare field write: R-B2.
            set_location(occupant, m_room_id);
            previous = occupant;
        }

        if (previous == nullptr)
        {
            m_room->people = nullptr; // LS1-ALLOW: representation-impl (fixture helper -- empty occupant set publishes an empty chain)
        }
        else
        {
            previous->next_in_room = nullptr; // LS1-ALLOW: representation-impl (fixture helper -- terminating the published chain)
        }
    }

    ~ScopedRoomOccupants()
    {
        m_room->people = m_saved_people; // LS1-ALLOW: representation-impl (fixture helper -- restoring the displaced chain head verbatim)

        // Unlink every managed character, so nothing this fixture published can
        // outlive the scope inside a process-global room's chain (THE
        // FIXTURE-HYGIENE RULE). NOWHERE, not the room id: a character this
        // fixture has taken back out is nowhere, which is what
        // detach_char_from_room() also stamps.
        for (char_data* occupant : m_occupants)
        {
            occupant->next_in_room = nullptr; // LS1-ALLOW: representation-impl (fixture helper -- unlinking a managed character on teardown)
            set_location(occupant, NOWHERE);
        }
    }

    ScopedRoomOccupants(const ScopedRoomOccupants&) = delete;
    ScopedRoomOccupants& operator=(const ScopedRoomOccupants&) = delete;

private:
    // The room whose occupant chain this fixture owns for its lifetime: the
    // target of every publish in the constructor and of the restore in the
    // destructor. Never null (a null room has no chain to own).
    room_data* m_room;

    // Room id stamped into each managed character's location via
    // set_location(). Supplied explicitly because a stub room_data is not in
    // world[] and so cannot be reverse-mapped to an rnum.
    int m_room_id;

    // The chain head displaced at construction, put back verbatim on
    // destruction -- so a room that already had occupants (which this fixture
    // does NOT relocate, only shadow) comes back exactly as it was.
    char_data* m_saved_people;

    // The managed characters in publication order. Held by value rather than as
    // the std::initializer_list itself, whose backing array does not outlive the
    // constructor's full-expression; the destructor walks this to unlink each.
    std::vector<char_data*> m_occupants;
};

// Stands up a one-zone zone_table for the duration of a scope, and restores
// whatever was there before.
//
// ANY TEST THAT DRIVES char_to_room() OR detach_char_from_room() WITH A NON-NPC
// CHARACTER NEEDS ONE OF THESE. Both primitives look their room's zone up with
// zone_by_id(r->zone) and then dereference the result unconditionally to move
// white_power/dark_power (placement.cpp) -- a deliberate no-new-null-checks
// decision, since a loaded world's r->zone is always in range. In this test
// binary no world is ever booted, so zone_by_id(0) returns nullptr in nearly
// every fixture and that dereference segfaults. IS_NPC characters skip the
// power block entirely, which is why so much of the existing suite gets away
// without a zone table.
//
// THE top_of_zone_table TRAP, and why the value below is 1 and not 0:
// zone_by_id_impl() (world/zone_load.cpp:57) bounds-checks with
// `znum >= top_of_zone_table` -- it reads that global as a COUNT, not as a top
// index, which is the "boundary-symmetry caveat" its own contract comment in
// zone.h flags and the opposite of top_of_world's inclusive convention next
// door. Setting it to 0 here would leave zone_by_id(0) returning nullptr and
// reintroduce exactly the segfault this fixture exists to prevent.
// (act_wiz_format_tests.cpp's ScopedZoneTable sets 0 quite correctly for its
// own purpose: it reads zone_table[] directly through Check_zone_authority and
// never goes through the resolver. load_room_placement_tests.cpp's
// ScopedVnumWorld, which does drive char_to_room() with a PC, sets 1 and
// documents the same trap.)
//
// Zone white/dark-power accounting is deliberately NOT reset per test here
// beyond the value-initialized zeros the fresh table starts with: the power
// deltas a test measures belong to that test, and a fixture that quietly
// re-zeroed them mid-scope would erase the very signal it is standing up the
// table to make observable. A test that moves a character in should move it
// back out (detach_char_from_room()) if it cares that the counters end level.
class ScopedZoneTableOwner
{
public:
    explicit ScopedZoneTableOwner(int zone_number = 0)
        : m_previous_zone_table(zone_table)
        , m_previous_top_of_zone_table(top_of_zone_table)
    {
        zone_table = new zone_data[1] { };
        zone_table[0].number = zone_number;

        // A COUNT of addressable zones, not a top index -- see the class
        // comment above. One zone, so 1.
        top_of_zone_table = 1;
    }

    ~ScopedZoneTableOwner()
    {
        delete[] zone_table;
        zone_table = m_previous_zone_table;
        top_of_zone_table = m_previous_top_of_zone_table;
    }

    ScopedZoneTableOwner(const ScopedZoneTableOwner&) = delete;
    ScopedZoneTableOwner& operator=(const ScopedZoneTableOwner&) = delete;

    // The single zone this fixture owns -- the one zone_by_id(0) resolves to.
    // Value-initialized apart from .number, so name/description/map are null;
    // a test that needs them stamps them here itself.
    zone_data& zone() { return zone_table[0]; }

private:
    // Whatever zone_table pointed at before this fixture ran (nullptr in a
    // freshly started process), restored verbatim on destruction so no later
    // test in the monolithic runner sees this fixture's table.
    zone_data* m_previous_zone_table;

    // The companion count saved alongside m_previous_zone_table; the two are
    // only ever meaningful together.
    int m_previous_top_of_zone_table;
};
